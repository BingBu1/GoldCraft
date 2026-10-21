// SPDX-License-Identifier: GPL-3.0-or-later
#include "assets.hpp"
#include "../model_codec.hpp"
#include "../parts.hpp"
#include "../dependencies.hpp"
#include <rapidjson/document.h>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <unordered_set>

namespace csol::client
{
namespace
{
std::unordered_map<std::string,std::unique_ptr<Asset>> assets;
std::unordered_map<const model_t*,Asset*> modelAssets;
struct CachedImage {GLuint texture{};int width{},height{};};
std::unordered_map<std::string,CachedImage> images;
struct Environment { GLuint texture{}; float add{}; };
std::unordered_map<std::string,Environment> environments;
rapidjson::Document common;
bool commonLoaded{};
std::string Lower(std::string text) { for(auto& c:text)c=char(std::tolower((unsigned char)c));return text; }
std::string Text(const char* name) { const auto bytes=ReadResource(name);return {(const char*)bytes.data(),bytes.size()}; }
std::string Directory(const std::string& name) { return name.substr(0,name.find_last_of("/\\")+1); }
bool SafePath(std::string_view path) { return !path.empty() && path.front()!='/' && path.front()!='\\' && path.find(':')==path.npos && path.find("..") == path.npos; }

GLuint LoadImage(const std::string& path, int& width, int& height)
{
    if(!SafePath(path))throw std::runtime_error("invalid texture path");
    if(auto it=images.find(path);it!=images.end()){width=it->second.width;height=it->second.height;return it->second.texture;}
    const auto bytes=ReadResource(path.c_str());
    auto image=Lower(path).ends_with(".tga")?DecodeTGA(bytes):DecodeBMP(bytes);
    width=image.Width;height=image.Height;
    const auto id=UploadImage(image);images.emplace(path,CachedImage{id,width,height});return id;
}
Environment LoadEnvironment(std::string_view name)
{
    const int at=name.starts_with('@')?0:name.size()>=7 && name[3]=='@'?3:-1;
    if(at<0 || name.size()<at+4)return {};
    std::string descriptor(name.substr(at,4));if(descriptor=="@00a")descriptor="@000";
    if(!SafePath(descriptor))throw std::runtime_error("invalid environment path");
    if(auto it=environments.find(descriptor);it!=environments.end())return it->second;
    std::istringstream input(Text(("shader/"+descriptor+".txt").c_str()));
    Environment result;std::string key;float value{};int parameter{},index{};
    if(!(input>>key>>value) || key!="type" || value!=0)throw std::runtime_error("unsupported environment type");
    while(input>>parameter>>key>>value)
    {
        if(!std::isfinite(value) || std::abs(value)>1024 || parameter<0 || parameter>2)throw std::runtime_error("invalid environment parameter");
        if(parameter==0){if(value<0 || value!=std::floor(value))throw std::runtime_error("invalid environment index");index=int(value);}
        if(parameter==2)result.add=value;
    }
    std::istringstream mapping(Text("shader/env.txt"));std::string line,selected;
    while(std::getline(mapping,line)){std::istringstream row(line);int number{};std::string file,extra;if((row>>number>>file) && !(row>>extra) && number==index)selected=file;}
    if(!SafePath(selected))throw std::runtime_error("missing environment mapping");
    constexpr const char* suffix[]{"rt","bk","lf","ft","up","dn"};
    constexpr GLenum targets[]{GL_TEXTURE_CUBE_MAP_POSITIVE_X,GL_TEXTURE_CUBE_MAP_POSITIVE_Y,GL_TEXTURE_CUBE_MAP_NEGATIVE_X,GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,GL_TEXTURE_CUBE_MAP_POSITIVE_Z,GL_TEXTURE_CUBE_MAP_NEGATIVE_Z};
    std::array<Image,6> faces;
    for(int f=0;f<6;++f)
    {
        const auto image=DecodeTGA(ReadResource(("gfx/env/"+selected+suffix[f]+".tga").c_str()));
        if(image.Width!=image.Height || image.Width<128 || image.Width%128)throw std::runtime_error("invalid cubemap size");
        faces[f]={128,128,std::vector<uint8_t>(128*128*4)};const unsigned step=image.Width/128;
        for(int y=0;y<128;++y)for(int x=0;x<128;++x)
        {
            int dx=y,dy=x;if(f==1){dx=x;dy=127-y;}else if(f==2){dx=127-y;dy=127-x;}else if(f==3){dx=127-x;dy=y;}
            memcpy(faces[f].RGBA.data()+(dy*128+dx)*4,image.RGBA.data()+(y*step*image.Width+x*step)*4,4);
        }
    }
    glGenTextures(1,&result.texture);glBindTexture(GL_TEXTURE_CUBE_MAP,result.texture);
    for(int f=0;f<6;++f)glTexImage2D(targets[f],0,GL_RGBA8,128,128,0,GL_RGBA,GL_UNSIGNED_BYTE,faces[f].RGBA.data());
    glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    for(auto p:{GL_TEXTURE_WRAP_S,GL_TEXTURE_WRAP_T,GL_TEXTURE_WRAP_R})glTexParameteri(GL_TEXTURE_CUBE_MAP,p,GL_CLAMP_TO_EDGE);
    environments.emplace(descriptor,result);return result;
}

void CommonMaterials(Asset& asset,const std::vector<std::string>& names)
{
    if(!commonLoaded){commonLoaded=true;if(Exists("materials/common.json")){const auto text=Text("materials/common.json");common.Parse(text.data(),text.size());}}
    if(common.IsNull())return;
    if(common.HasParseError() || !common.IsObject() || !common.HasMember("Materials") || !common["Materials"].IsArray())throw std::runtime_error("invalid common materials");
    const auto prefix=Lower(asset.name)+"/";
    for(const auto& row:common["Materials"].GetArray())
    {
        if(!row.IsObject() || !row.HasMember("OriginalTexture") || !row["OriginalTexture"].IsString())continue;
        if(row.HasMember("SubKey") && (!row["SubKey"].IsInt() || row["SubKey"].GetInt()))continue;
        auto key=Lower(row["OriginalTexture"].GetString());std::replace(key.begin(),key.end(),'\\','/');
        if(!key.starts_with(prefix))continue;
        for(size_t i=0;i<names.size();++i)if(key==prefix+Lower(names[i]))
        {
            auto& m=asset.materials[i];asset.special=true;
            if(row.HasMember("BlendMode") && row["BlendMode"].IsString())
            {const std::string_view mode=row["BlendMode"].GetString();m.commonAlpha=mode=="Translucent";m.commonAdd=mode=="Additive";}
            if(row.HasMember("BaseColor"))
            {
                const auto& color=row["BaseColor"];if(!color.IsArray() || color.Size()!=4)throw std::runtime_error("invalid material color");
                for(unsigned c=0;c<4;++c)if(!color[c].IsNumber() || !std::isfinite(color[c].GetDouble()))throw std::runtime_error("invalid material color value");
                m.commonOpacity=float(color[3].GetDouble());
                for(unsigned c=0;c<3;++c)m.commonColor[c]=float(color[c].GetDouble());
            }
        }
    }
}
int HandType(std::string_view name)
{
    if(name.size()>=7 && name.front()=='#')name.remove_prefix(7);
    constexpr const char* names[]{"cso_highq_hand__long_512.bmp","cso_highq_hand_512.bmp","cso_girl_hand_long.bmp","cso_girl_hand.bmp","hand.bmp"};
    for(int i=0;i<5;++i)if(Lower(std::string(name))==names[i])return i;return -1;
}
void Materials(Asset& asset)
{
    const auto& h=asset.Header();auto textures=asset.Data<mstudiotexture_t>(h.textureindex,h.numtextures);
    auto json=asset.name.substr(0,asset.name.size()-4)+".json";
    if(Exists(json.c_str())){asset.config=ParseModelJSON(Text(json.c_str()),h.numtextures,h.numseq);asset.special=true;}
    std::vector<std::string> names;
    for(int i=0;i<h.numtextures;++i)
    {
        const auto& src=textures[i];Material m;m.width=src.width;m.height=src.height;m.flags=src.flags;
        if(src.width<1 || src.height<1 || src.width>4096 || src.height>4096)throw std::runtime_error("invalid model texture size");
        const auto original=Name(src.name);std::string replacement;
        for(const auto& bind:asset.config.Textures)if(bind.Texture==i)replacement=bind.Name;
        const auto source=ResolveTextureSource(asset.source,original,replacement,[](const std::string& file){return Exists(file.c_str());});
        const auto& name=source.Name;
        names.push_back(name);
        static_cast<MaterialPolicy&>(m)=source.Policy;asset.special|=m.special;
        bool dimensions=false;
        if(name.size()>7 && name[0]=='#' && std::all_of(name.begin()+1,name.begin()+7,[](char c){return c>='0'&&c<='9';}))
        {
            const int w=atoi(name.substr(1,3).c_str()),height=atoi(name.substr(4,3).c_str());
            if(w && height){m.width=w;m.height=height;dimensions=true;asset.special=true;}
        }
        m.originalHeight=m.height;m.hand=HandType(name);if(m.hand==4 && m.originalHeight!=252)m.hand=-1;
        asset.special|=m.hand>=0;
        const auto embedded=asset.Data<uint8_t>(src.index,src.width*src.height+768);const auto* palette=embedded.data()+src.width*src.height;
        Image image{unsigned(src.width),unsigned(src.height),std::vector<uint8_t>(size_t(src.width)*src.height*4)};
        for(size_t p=0;p<image.RGBA.size()/4;++p){const auto index=embedded[p];memcpy(image.RGBA.data()+p*4,palette+index*3,3);image.RGBA[p*4+3]=(src.flags&0x40)&&index==255?0:255;}
        const auto& path=source.Resource;
        if(!path.empty() && Exists(path.c_str()))m.texture=LoadImage(path,m.width,m.height);
        else {m.texture=UploadImage(image);images.emplace(asset.name+"#"+std::to_string(i),CachedImage{m.texture,m.width,m.height});if(source.External && !source.EmbeddedFallback)engine.Con_Printf("CSOLSupport: missing external texture %s/%s\n",asset.name.c_str(),name.c_str());}
        const auto env=LoadEnvironment(source.Descriptor);m.environment=env.texture;m.environmentAdd=env.add;asset.special|=env.texture!=0;
        asset.materials.push_back(m);
    }
    CommonMaterials(asset,names);
    for(const auto& binding:asset.config.Sprites){asset.materials[binding.Texture].sprite=int(asset.sprites.size());asset.sprites.push_back(LoadEngineModel(binding.Sprite.c_str()));}
    for(size_t i=0;i<asset.config.Fades.size();++i)asset.materials[asset.config.Fades[i].Texture].fade=int(i);
}
void Geometry(Asset& asset)
{
    const auto& h=asset.Header();std::vector<Vertex> vertices;std::vector<uint32_t> indices;
    auto groups=asset.Data<mstudiobodyparts_t>(h.bodypartindex,h.numbodyparts);
    for(int g=0;g<h.numbodyparts;++g)
    {
        const auto& group=groups[g];if(group.base<1 || group.nummodels<1)throw std::runtime_error("invalid bodygroup");
        auto models=asset.Data<mstudiomodel_t>(group.modelindex,group.nummodels);
        for(int s=0;s<group.nummodels;++s)
        {
            const auto& model=models[s];auto positions=asset.Data<std::array<float,3>>(model.vertindex,model.numverts);
            auto normals=asset.Data<std::array<float,3>>(model.normindex,model.numnorms);
            auto bones=asset.Data<uint8_t>(model.vertinfoindex,model.numverts);auto normalBones=asset.Data<uint8_t>(model.norminfoindex,model.numnorms);
            for(const auto& mesh:asset.Data<mstudiomesh_t>(model.meshindex,model.nummesh))
            {
                if(mesh.skinref<0 || mesh.skinref>=h.numskinref)throw std::runtime_error("invalid mesh skin reference");
                Batch batch{unsigned(indices.size()),0,g,s,mesh.skinref};int cursor=mesh.triindex;
                for(;;)
                {
                    const int command=asset.Data<int16_t>(cursor)[0];cursor+=2;if(!command)break;
                    const int count=std::abs(command);if(count<3)throw std::runtime_error("invalid studio triangle command");
                    auto stream=asset.Data<int16_t>(cursor,count*4);cursor+=count*8;const auto first=uint32_t(vertices.size());
                    if(vertices.size()+count>2*1024*1024)throw std::runtime_error("studio mesh exceeds vertex budget");
                    for(int v=0;v<count;++v)
                    {
                        const int p=stream[v*4],n=stream[v*4+1];if(p<0 || p>=positions.size() || n<0 || n>=normals.size() || bones[p]>=h.numbones || normalBones[n]>=h.numbones)throw std::runtime_error("invalid studio vertex index");
                        Vertex vert{};memcpy(vert.position,positions[p].data(),12);memcpy(vert.normal,normals[n].data(),12);vert.uv[0]=stream[v*4+2];vert.uv[1]=stream[v*4+3];vert.bone=bones[p];vert.normalBone=normalBones[n];vertices.push_back(vert);
                        if(v>=2){indices.push_back(command<0?first:first+v-2);indices.push_back(first+(command>0 && (v&1)?v:v-1));indices.push_back(first+(command>0 && (v&1)?v-1:v));}
                    }
                }
                batch.count=unsigned(indices.size())-batch.offset;asset.batches.push_back(batch);
            }
        }
    }
    glGenVertexArrays(1,&asset.vao);glGenBuffers(1,&asset.vbo);glGenBuffers(1,&asset.ibo);glBindVertexArray(asset.vao);
    glBindBuffer(GL_ARRAY_BUFFER,asset.vbo);glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(Vertex),vertices.data(),GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,asset.ibo);glBufferData(GL_ELEMENT_ARRAY_BUFFER,indices.size()*4,indices.data(),GL_STATIC_DRAW);
    for(int i=0;i<3;++i){glEnableVertexAttribArray(i);glVertexAttribPointer(i,i==2?2:3,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)(size_t(i)*12));}
    glEnableVertexAttribArray(3);glVertexAttribIPointer(3,2,GL_INT,sizeof(Vertex),(void*)offsetof(Vertex,bone));
}
}

GLuint UploadImage(const Image& image)
{
    GLuint id{};glGenTextures(1,&id);glBindTexture(GL_TEXTURE_2D,id);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,image.Width,image.Height,0,GL_RGBA,GL_UNSIGNED_BYTE,image.RGBA.data());
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);return id;
}
Asset::~Asset(){if(vao)glDeleteVertexArrays(1,&vao);if(vbo)glDeleteBuffers(1,&vbo);if(ibo)glDeleteBuffers(1,&ibo);}
std::pair<bool,Asset*> CachedAsset(model_t* model)
{
    if(!model)return {true,nullptr};
    const auto found=modelAssets.find(model);
    return found!=modelAssets.end()?std::pair{true,found->second}:std::pair<bool,Asset*>{false,nullptr};
}
Asset* GetAsset(model_t* model)
{
    if(!model)return nullptr;std::string name=model->name;
    if(auto found=assets.find(name);found!=assets.end()){auto* result=found->second.get();modelAssets[model]=result;return result;}
    auto asset=std::make_unique<Asset>();asset->name=name;
    asset->source=ResolveModelSource(name,[](const std::string& file){return ReadResource(file.c_str());},[](const std::string& file){return Exists(file.c_str());});
    asset->bytes=ReadResource(asset->source.File.c_str());
    asset->special=DecodeStudioModel(asset->bytes) || asset->source.Redirected;const auto& h=asset->Header();
    if(h.id!=0x54534449 || h.version!=10 || h.length<sizeof(studiohdr_t) || h.length>asset->bytes.size() || h.numbones<1 || h.numbones>MAXSTUDIOBONES || h.numtextures<0 || h.numtextures>4096 || h.numseq<1 || h.numseq>2048 || h.numbodyparts<0 || h.numbodyparts>256 || h.numattachments<0 || h.numattachments>64)throw std::runtime_error("unsupported studio layout");
    // Stock models may use a separate T.mdl. Leave their loading to the host.
    if(!asset->special && !h.numtextures){assets.emplace(name,nullptr);modelAssets[model]=nullptr;return nullptr;}
    if(h.numskinref<1 || h.numskinref>4096 || h.numskinfamilies<1 || h.numskinfamilies>4096 || !h.numtextures)throw std::runtime_error("invalid skin dimensions");
    asset->bytes.resize(h.length);
    asset->Data<mstudiobone_t>(h.boneindex,h.numbones);asset->Data<mstudioattachment_t>(h.attachmentindex,h.numattachments);
    const auto skin=asset->Data<int16_t>(h.skinindex,h.numskinref*h.numskinfamilies);for(auto i:skin)if(i<0 || i>=h.numtextures)throw std::runtime_error("invalid skin table");
    const auto sequences=asset->Data<mstudioseqdesc_t>(h.seqindex,h.numseq);
    for(const auto& seq:sequences)for(const auto& event:asset->Data<mstudioevent_t>(seq.eventindex,seq.numevents))asset->special|=event.options[0]=='#';
    asset->profile=FindBodyEffects(name);asset->special|=asset->profile!=BodyEffectProfile::None || !PartsProfile(name).Actions.empty();
    asset->revive=h.numseq==13 && h.numattachments>5 && Name(asset->Data<mstudiobone_t>(h.boneindex)[0].name)=="revivegun_root";
    asset->special|=asset->revive;Materials(*asset);
    if(asset->special)Geometry(*asset);
    auto* result=asset.get();assets.emplace(name,std::move(asset));modelAssets[model]=result;return result;
}
EngineSpriteFrame* SpriteFrameAt(model_t* model,int frame,double time)
{
    if(!model || model->type!=mod_sprite)return nullptr;auto* sprite=(EngineSprite*)model->cache.data;
    if(!sprite || sprite->frames<1)return nullptr;const auto& desc=sprite->frame[std::clamp(frame,0,sprite->frames-1)];
    if(!desc.type)return desc.frame;
    struct Group{int count;float* intervals;EngineSpriteFrame* frames[1];};auto* group=(Group*)desc.frame;
    if(!group || group->count<1)return nullptr;const double full=group->intervals[group->count-1];if(full<=0)return nullptr;
    const double target=time-std::floor(time/full)*full;int i=0;while(i+1<group->count && target>=group->intervals[i])++i;return group->frames[i];
}
size_t AssetCount(){return assets.size();}
void EnsureGeometry(Asset& asset){if(!asset.vao)Geometry(asset);}
void ClearAssets(bool deleteGraphics)
{
    if(!deleteGraphics)for(auto& row:assets)if(row.second)row.second->vao=row.second->vbo=row.second->ibo=0;
    modelAssets.clear();assets.clear();if(deleteGraphics)for(const auto& row:images)glDeleteTextures(1,&row.second.texture);images.clear();
    if(deleteGraphics)for(const auto& row:environments)glDeleteTextures(1,&row.second.texture);environments.clear();common.SetNull();commonLoaded=false;ResetHands(deleteGraphics);
}
}
