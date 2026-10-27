// SPDX-License-Identifier: GPL-3.0-or-later
#include "assets.hpp"
#include "characters.hpp"
#include <algorithm>
#include <sstream>
#include <unordered_set>
namespace csol::client {
namespace {
cvar_t* appearanceCvar{};
std::string selected;
bool initialized{};
int selectedGender{-1};
std::array<GLuint,5> handles{};
constexpr const char* suffixes[]{"MaleLong.tga","MaleShort.tga","FemaleLong.tga","FemaleShort.tga","MaleOrg.tga"};
std::unordered_set<std::string> tattooStyles;
bool stylesLoaded{};
std::string Lower(std::string value)
{
    std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return std::tolower(c);});return value;
}
void Styles()
{
    if(stylesLoaded)return;
    stylesLoaded=true;
    if(!Exists("gfx/tattoo/tattoo.cso"))return;
    auto bytes=ReadResource("gfx/tattoo/tattoo.cso");
    if(bytes.size()>1024*1024 || bytes.size()%8)throw std::runtime_error("invalid tattoo table size");
    constexpr uint32_t key[]{0xb38520fd,0x5f30e75f,0xe2eb67e0,0xfc67a521},delta=0x9e3779b9;
    for(size_t i=0;i<bytes.size();i+=8)
    {
        uint32_t left,right;memcpy(&left,bytes.data()+i,4);memcpy(&right,bytes.data()+i+4,4);
        uint32_t sum=delta*32;
        for(int round=0;round<32;++round)
        {
            right-=((left<<4)+key[2])^(left+sum)^((left>>5)+key[3]);
            left-=((right<<4)+key[0])^(right+sum)^((right>>5)+key[1]);sum-=delta;
        }
        memcpy(bytes.data()+i,&left,4);memcpy(bytes.data()+i+4,&right,4);
    }
    while(!bytes.empty() && bytes.back()==std::byte{})bytes.pop_back();
    std::istringstream input(std::string((const char*)bytes.data(),bytes.size()));std::string line;
    std::getline(input,line);if(!line.starts_with("#gameid,resource,tattooAble"))throw std::runtime_error("invalid tattoo table header");
    while(std::getline(input,line))
    {
        const auto first=line.find(','),last=line.rfind(',');
        if(first==line.npos || first==last)continue;
        if(atoi(line.c_str()+last+1))tattooStyles.insert(Lower(line.substr(first+1,last-first-1)));
    }
}
}
void InitializeHands(){appearanceCvar=engine.pfnRegisterVariable("csol_hand","",FCVAR_ARCHIVE);}
void ResetHands(bool deleteGraphics)
{
    for(int i=0;i<5;++i)
    {
        if(deleteGraphics && handles[i]) { glDeleteTextures(1,&handles[i]); }
    }
    handles={};selected.clear();selectedGender=-1;initialized=false;stylesLoaded=false;tattooStyles.clear();
}
void UpdateHands()
{
    const auto* character=ViewCharacter();
    // Authoritative fixed styles take priority. Style0 deliberately keeps the
    // weapon's embedded hands; it is not the generic tattoo base image.
    std::string appearance=character?character->hand:(appearanceCvar?appearanceCvar->string:"");
    if(appearance.size()>128 || !std::all_of(appearance.begin(),appearance.end(),[](unsigned char c){return std::isalnum(c)||c=='_'||c=='-';}))appearance.clear();
    const int gender=character?int(character->male):-1;
    if(initialized && appearance==selected && selectedGender==gender)return;
    selected=appearance;selectedGender=gender;initialized=true;
    try { Styles(); }
    catch(const std::exception& error){engine.Con_Printf("CSOL: %s\n",error.what());}
    for(int i=0;i<5;++i)
    {
        const auto identifier="csol/hand/"+std::to_string(i);
        if(handles[i])glDeleteTextures(1,&handles[i]);
        handles[i]=0;
        if(appearance.empty() || (character && (character->male?(i==2 || i==3):(i!=2 && i!=3))))continue;
        const std::string base="gfx/tattoo/"+std::string(suffixes[i]);
        std::string path=appearance.empty()?base:"gfx/tattoo/"+appearance+"_"+suffixes[i];
        if(!Exists(path.c_str()))path=base;
        if(!Exists(path.c_str()))continue;
        try
        {
            auto image=csol::DecodeTGA(ReadResource(path.c_str()));
            if(path!=base && tattooStyles.contains(Lower(appearance)))
            {
                auto background=csol::DecodeTGA(ReadResource(base.c_str()));
                if(image.Width>background.Width || image.Height>background.Height)throw std::runtime_error("tattoo exceeds hand size");
                for(unsigned y=0;y<image.Height;++y)
                    memcpy(background.RGBA.data()+((background.Height-image.Height+y)*background.Width)*4,
                        image.RGBA.data()+y*image.Width*4,image.Width*4);
                image=std::move(background);
            }
            handles[i]=UploadImage(image);
        }
        catch(const std::exception& error){engine.Con_Printf("CSOL: hand %s: %s\n",path.c_str(),error.what());}
    }
}
void FindHandModels(Asset& asset)
{
    const auto& h=asset.Header();const auto groups=asset.Data<mstudiobodyparts_t>(h.bodypartindex,h.numbodyparts);
    const auto skins=asset.Data<int16_t>(h.skinindex,h.numskinref*h.numskinfamilies);
    asset.handModels.assign(groups.size(),{-1,-1});
    for(size_t g=0;g<groups.size();++g){
        const auto& group=groups[g];std::array<int,2> byMaterial{-1,-1},matches{};
        const auto models=asset.Data<mstudiomodel_t>(group.modelindex,group.nummodels);
        for(int sub=0;sub<group.nummodels;++sub){
            const auto& model=models[sub];const auto name=Lower(Name(model.name));
            if(name.find("cso_hand_male")!=name.npos)asset.handModels[g][0]=sub;
            if(name.find("cso_hand_female")!=name.npos)asset.handModels[g][1]=sub;
            int gender=-1;bool onlyHands=model.nummesh>0 && h.numskinfamilies>0;
            for(const auto& mesh:asset.Data<mstudiomesh_t>(model.meshindex,model.nummesh)){
                if(mesh.skinref<0 || mesh.skinref>=h.numskinref)throw std::runtime_error("invalid hand mesh skin reference");
                for(int skin=0;skin<h.numskinfamilies;++skin){const int type=asset.materials.at(skins[skin*h.numskinref+mesh.skinref]).hand;
                    const int next=type<0?-1:(type==2 || type==3?1:0);
                    if(next<0 || (gender>=0 && next!=gender))onlyHands=false;gender=next;}
            }
            if(onlyHands && gender>=0){byMaterial[gender]=sub;++matches[gender];}
        }
        if(matches[0]==1 && matches[1]==1)for(int gender=0;gender<2;++gender)if(asset.handModels[g][gender]<0)asset.handModels[g][gender]=byMaterial[gender];
    }
}
int CharacterHandSubmodel(const Asset& asset,int group,int original,int body)
{
    const auto* c=ViewCharacter();if(!c || group<0 || group>=asset.handModels.size())return original;
    if(!asset.handBodygroups.empty() && group==asset.Header().numbodyparts-1) {
        int mask{};
        for(const auto& source:asset.handBodygroups)
            mask|=source.visibility[(std::max(body,0)/source.base)%source.visibility.size()];
        return (c->male?0:4)+mask;
    }
    const int selected=asset.handModels[group][c->male?0:1];return selected>=0?selected:original;
}
void ApplyHand(const Material& material,GLuint& texture,float& width,float& height,float* uv)
{
    const int type=material.hand;
    if(type<0 || type>=5 || !handles[type])return;
    texture=handles[type];
    constexpr int heights[]{502,505,505,506,252},x[]{3,2,4,2,0},y[]{4,1,3,1,2};
    if(material.originalHeight==heights[type])
    {
        width=height=type==4?256.f:512.f;
        uv[0]+=x[type]/width;uv[1]+=y[type]/height;
    }
}
}
