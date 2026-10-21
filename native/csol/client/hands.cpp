// SPDX-License-Identifier: GPL-3.0-or-later
#include "assets.hpp"
#include <algorithm>
#include <sstream>
#include <unordered_set>
namespace csol::client {
namespace {
cvar_t* appearanceCvar{};
std::string selected;
bool initialized{};
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
    handles={};selected.clear();initialized=false;stylesLoaded=false;tattooStyles.clear();
}
void UpdateHands()
{
    std::string appearance=appearanceCvar?appearanceCvar->string:"";
    if(appearance.empty())
    {
        hud_player_info_t player{};engine.pfnGetPlayerInfo(view.playernum+1,&player);
        if(player.model)appearance=player.model;
    }
    if(appearance.size()>128 || !std::all_of(appearance.begin(),appearance.end(),[](unsigned char c){return std::isalnum(c)||c=='_'||c=='-';}))appearance.clear();
    if(initialized && appearance==selected)return;
    selected=appearance;initialized=true;
    try { Styles(); }
    catch(const std::exception& error){engine.Con_Printf("CSOL: %s\n",error.what());}
    for(int i=0;i<5;++i)
    {
        const auto identifier="csol/hand/"+std::to_string(i);
        if(handles[i])glDeleteTextures(1,&handles[i]);
        handles[i]=0;
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
