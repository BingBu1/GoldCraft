// SPDX-License-Identifier: GPL-3.0-or-later
#include "dependencies.hpp"
#include "model_codec.hpp"
#include "model_json.hpp"
#include "events.hpp"
#include "parts.hpp"
#include <rapidjson/document.h>
#include <algorithm>
#include <cstring>
#include <set>
#include <sstream>
#include <stdexcept>
namespace csol
{
namespace
{
std::string Lower(std::string s){for(auto& c:s)c=char(std::tolower(static_cast<unsigned char>(c)));return s;}
bool Safe(const std::string& s)
{
    if(s.empty() || s.front()=='/' || s.front()=='\\' || s.find("..")!=s.npos)return false;
    for(unsigned char c:s)if(c<32 || c==':' || c=='\\' || c=='"' || c=='*' || c=='?')return false;
    return s.size()<64;
}
int Word(const std::vector<std::byte>& b,size_t at)
{if(at>b.size() || b.size()-at<4)throw std::runtime_error("truncated studio dependency record");int n{};memcpy(&n,b.data()+at,4);return n;}
std::string Name(const std::vector<std::byte>& b,size_t at,size_t count)
{if(at>b.size() || count>b.size()-at)throw std::runtime_error("truncated studio dependency name");const char* s=(const char*)b.data()+at;return {s,strnlen(s,count)};}
std::string Text(const ResourceReader& read,const std::string& file){const auto b=read(file);return {(const char*)b.data(),b.size()};}
size_t Record(const std::vector<std::byte>& b,int at,int index,int count,size_t stride)
{
    if(at<0 || count<0 || count>4096 || index<0 || index>=count || size_t(at)>b.size() || size_t(count)>(b.size()-at)/stride)throw std::runtime_error("studio dependency table out of bounds");
    return size_t(at)+size_t(index)*stride;
}
}
std::string ExternalTexture(const std::string& model,std::string texture,bool replacement,const ResourceExists& exists)
{
    bool dimensions=texture.size()>7 && texture[0]=='#' && std::all_of(texture.begin()+1,texture.begin()+7,[](char c){return c>='0'&&c<='9';});
    if(!Safe(texture))throw std::runtime_error("invalid texture dependency");
    auto tga=texture.substr(0,texture.find_last_of('.'))+".tga";
    if(replacement || dimensions)
    {
        if(texture.starts_with('$') && exists("models/texture/"+tga))return "models/texture/"+tga;
        if(exists("models/texture/"+texture))return "models/texture/"+texture;
    }
    else if(texture.starts_with('$'))
    {
        const auto path=model.substr(0,model.find_last_of('/')+1)+tga;
        if(exists(path))return path;
    }
    return {};
}
std::vector<std::string> WeaponDependencies(const std::string& model,const ResourceReader& read,const ResourceExists& exists)
{
    std::set<std::string> files,visited;std::vector<std::string> queue;
    auto add=[&](const std::string& path,bool required=true)
    {
        if(!Safe(path))throw std::runtime_error("invalid weapon dependency: "+path);
        if(!exists(path)){if(required)throw std::runtime_error("missing weapon dependency: "+path);return;}
        if(files.insert(path).second && Lower(path).ends_with(".mdl"))queue.push_back(path);
    };
    add(model);
    rapidjson::Document common;
    if(exists("materials/common.json")){const auto json=Text(read,"materials/common.json");common.Parse(json.data(),json.size());}
    while(!queue.empty())
    {
        const auto name=queue.back();queue.pop_back();if(!visited.insert(name).second)continue;
        if(visited.size()>64 || files.size()>8192)throw std::runtime_error("weapon dependency budget exceeded");
        auto bytes=read(name);DecodeStudioModel(bytes);
        if(Word(bytes,0)!=0x54534449 || Word(bytes,4)!=10)throw std::runtime_error("unsupported dependency model");
        const int length=Word(bytes,72);if(length<244 || size_t(length)>bytes.size())throw std::runtime_error("invalid dependency model length");bytes.resize(length);
        const int textures=Word(bytes,180),textureAt=Word(bytes,184),sequences=Word(bytes,164),sequenceAt=Word(bytes,168);
        if(textures<0 || textures>4096 || sequences<1 || sequences>2048)throw std::runtime_error("invalid dependency table count");
        ModelJSON config;const auto json=name.substr(0,name.size()-4)+".json";
        if(exists(json)){add(json);config=ParseModelJSON(Text(read,json),textures,sequences);}
        for(const auto& sprite:config.Sprites)add(sprite.Sprite);
        for(int i=0;i<textures;++i)
        {
            const auto at=Record(bytes,textureAt,i,textures,80);std::string texture=Name(bytes,at,64);bool replacement=false;
            for(const auto& binding:config.Textures)if(binding.Texture==i){texture=binding.Name;replacement=true;}
            const auto external=ExternalTexture(name,texture,replacement,exists);if(!external.empty())add(external);
            const auto key=Lower(name+"/"+texture);
            if(common.IsObject() && common.HasMember("Materials") && common["Materials"].IsArray())for(const auto& row:common["Materials"].GetArray())
            {
                if(!row.IsObject() || !row.HasMember("OriginalTexture") || !row["OriginalTexture"].IsString())continue;
                auto original=Lower(row["OriginalTexture"].GetString());std::replace(original.begin(),original.end(),'\\','/');if(original!=key)continue;
                add("materials/common.json");
                for(const auto* field:{"BaseTexture","NormalTexture","SpecularTexture","EmissiveTexture","DispTexture"})
                    if(row.HasMember(field) && row[field].IsString() && row[field].GetStringLength() && std::string_view(row[field].GetString())!="OriginalTexture")add(row[field].GetString());
            }
            const int env=texture.starts_with('@')?0:texture.size()>=7 && texture[3]=='@'?3:-1;
            if(env>=0)
            {
                std::string descriptor=texture.substr(env,4);if(descriptor=="@00a")descriptor="@000";
                const auto file="shader/"+descriptor+".txt";add(file);add("shader/env.txt");
                std::istringstream parameters(Text(read,file));std::string key;float value;int p,index=0;
                parameters>>key>>value;while(parameters>>p>>key>>value)if(p==0)index=int(value);
                std::istringstream mapping(Text(read,"shader/env.txt"));std::string line;
                while(std::getline(mapping,line)){std::istringstream row(line);int n;std::string base,extra;if((row>>n>>base) && !(row>>extra) && n==index)for(const auto* face:{"rt","bk","lf","ft","up","dn"})add("gfx/env/"+base+face+".tga");}
            }
            if(texture.starts_with('#') && texture.size()>7)texture.erase(0,7);
            constexpr const char* hands[]{"cso_highq_hand__long_512.bmp","cso_highq_hand_512.bmp","cso_girl_hand_long.bmp","cso_girl_hand.bmp","hand.bmp"};
            constexpr const char* suffix[]{"MaleLong.tga","MaleShort.tga","FemaleLong.tga","FemaleShort.tga","MaleOrg.tga"};
            for(int h=0;h<5;++h)if(Lower(texture)==hands[h]){add("gfx/tattoo/"+std::string(suffix[h]));add("gfx/tattoo/tattoo.cso",false);}
        }
        for(int i=0;i<sequences;++i)
        {
            const auto at=Record(bytes,sequenceAt,i,sequences,176);const int count=Word(bytes,at+48),events=Word(bytes,at+52);
            for(int e=0;e<count;++e)
            {
                const auto event=Record(bytes,events,e,count,76);const auto options=Name(bytes,event+12,64);const int id=Word(bytes,event+4);
                if(id>=5000 && id<=5639 && options.starts_with('#'))
                {if(const auto rule=ParseSpriteEvent(id,options)){add(rule->ResourceName());if(rule->Alternate)add("sprites/muzzleflash"+std::to_string(rule->Alternate)+".spr");}}
                else if(Lower(options).ends_with(".wav"))add("sound/"+options);
            }
        }
        for(int slot=0;slot<2;++slot)
        {
            const auto definition=PartsProfile(name,slot);if(definition.Actions.empty())continue;
            std::string part=name.substr(0,name.size()-4)+"_parts";if(definition.Behavior!=PartsBehavior::Standard)part+=slot==0?"01":"02";add(part+".mdl");
        }
        if(name=="models/v_revivegun.mdl")for(const auto* action:{"idle","draw"})for(const auto* variant:{"1","2"})for(const auto* hand:{"","_left"})add("sprites/ef_revivegun_"+std::string(action)+variant+hand+".spr");
    }
    return {files.begin(),files.end()};
}
}
