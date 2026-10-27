// SPDX-License-Identifier: GPL-3.0-or-later
#include "model_source.hpp"
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <algorithm>
#include <cctype>
#include <stdexcept>
namespace csol
{
namespace
{
struct StaticRedirect {const char* Requested;const char* Model;const char* TexturePrefix;};
constexpr StaticRedirect redirects[]{
#include "model_redirects.inc"
};
std::string Lower(std::string text){for(auto& c:text)c=char(std::tolower(static_cast<unsigned char>(c)));return text;}
bool Safe(std::string_view text,bool path=false,bool empty=false)
{
    if(text.empty())return empty;
    if(text.size()>128 || text.front()=='/' || text.back()=='.' || text.back()==' ' || text.find("..")!=text.npos)return false;
    for(unsigned char c:text)if(c<32 || c==':' || c=='\\' || c=='"' || c=='*' || c=='?' || c=='|' || c=='<' || c=='>' || (!path && (c=='/' || c==' ')))return false;
    return true;
}
std::string Field(const rapidjson::Value& object,const char* key,bool path=false,bool empty=false)
{
    if(!object.HasMember(key) || !object[key].IsString())throw std::runtime_error(std::string("invalid model source field: ")+key);
    const auto& v=object[key];std::string value(v.GetString(),v.GetStringLength());
    if(!Safe(value,path,empty))throw std::runtime_error("invalid model source name");return value;
}
std::string Descriptor(std::string_view name)
{
    const int at=name.starts_with('@')?0:name.size()>=7 && name[3]=='@'?3:-1;
    if(at<0 || name.size()<at+4)return {};
    std::string descriptor(name.substr(at,4));return descriptor=="@00a"?"@000":descriptor;
}
}
std::vector<ModelRedirect> ParseWeaponRedirects(std::string_view text,const std::string& configuration)
{
    if(text.size()>1024*1024 || !Safe(configuration,true))throw std::runtime_error("weapon alias configuration exceeds limits");
    rapidjson::Document doc;doc.Parse<rapidjson::kParseCommentsFlag>(text.data(),text.size());
    if(doc.HasParseError() || !doc.IsObject())throw std::runtime_error("invalid weapon alias JSON");
    if(!doc.HasMember("texPrefix"))return {};
    const auto prefix=Field(doc,"texPrefix",false,true),base=Field(doc,"baseWpnName"),name=Field(doc,doc.HasMember("modelName")?"modelName":"resourceName");
    constexpr const char* keys[]{"vModelName","pModelName","wModelName","sModelName"};
    constexpr const char* stems[]{"models/v_","models/p_","models/w_","models/shop/s_weapon/s_"};
    std::vector<ModelRedirect> result;
    for(int i=0;i<4;++i)
    {
        auto texture=prefix;
        if(i==0 && doc.HasMember("vModelTexPrefix")){texture=Field(doc,"vModelTexPrefix",false,true);if(texture.empty())continue;}
        const auto requested=doc.HasMember(keys[i])?Field(doc,keys[i]):name;
        result.push_back({std::string(stems[i])+requested+".mdl",std::string(stems[i])+base+".mdl",texture,configuration});
    }
    return result;
}
std::string ModelSourceDescriptor(const std::string& model){return model.substr(0,model.size()-4)+"_csol.json";}
std::string SerializeModelSource(const ModelSource& source)
{
    rapidjson::StringBuffer buffer;rapidjson::Writer writer(buffer);writer.StartObject();
    writer.Key("version");writer.Int(1);
    auto field=[&](const char* name,const std::string& value){writer.Key(name);writer.String(value.c_str(),rapidjson::SizeType(value.size()));};
    field("requested",source.Requested);field("physical",source.PhysicalName);field("texturePrefix",source.TexturePrefix);field("configuration",source.Configuration);
    writer.EndObject();return {buffer.GetString(),buffer.GetSize()};
}
ModelSource ResolveModelSource(const std::string& model,const ResourceReader& read,const ResourceExists& exists,const std::vector<ModelRedirect>& configured)
{
    if(!Safe(model,true) || !Lower(model).starts_with("models/") || !Lower(model).ends_with(".mdl"))throw std::runtime_error("invalid model source path");
    ModelSource result{model,model,model};
    const auto descriptor=ModelSourceDescriptor(model);
    if(exists(descriptor))
    {
        const auto bytes=read(descriptor);if(bytes.size()>4096)throw std::runtime_error("model source descriptor exceeds limits");
        rapidjson::Document doc;doc.Parse((const char*)bytes.data(),bytes.size());
        if(doc.HasParseError() || !doc.IsObject() || !doc.HasMember("version") || !doc["version"].IsInt() || doc["version"].GetInt()!=1 || Field(doc,"requested",true)!=model)throw std::runtime_error("model source descriptor identity mismatch");
        result.PhysicalName=Field(doc,"physical",true);result.TexturePrefix=Field(doc,"texturePrefix",false,true);result.Configuration=Field(doc,"configuration",true,true);
        if(!result.PhysicalName.starts_with("models/") || !result.PhysicalName.ends_with(".mdl"))throw std::runtime_error("invalid physical model name");
        result.Redirected=true;return result; // Prepared bytes are stored at the requested name.
    }
    const auto key=Lower(model);const StaticRedirect* fixed=nullptr;
    for(const auto& entry:redirects)if(Lower(entry.Requested)==key){fixed=&entry;break;}
    ModelRedirect selected;
    if(fixed)selected={model,fixed->Model,fixed->TexturePrefix,{}};
    else for(const auto& entry:configured)if(Lower(entry.Requested)==key){selected=entry;break;}
    if(!selected.Model.empty() && exists(selected.Model))
    {result.PhysicalName=result.File=selected.Model;result.TexturePrefix=selected.TexturePrefix;result.Configuration=selected.Configuration;result.Redirected=true;}
    return result;
}
TextureSource ResolveTextureSource(const ModelSource& model,const std::string& original,const std::string& replacement,const ResourceExists& exists)
{
    // Real studio textures may contain internal spaces (including before the
    // extension). Keep their exact names; still reject separators/traversal.
    auto textureName=[](std::string_view name){return Safe(name,true) && name.find('/')==name.npos;};
    if(!textureName(original) || (!replacement.empty() && !textureName(replacement)))throw std::runtime_error("invalid texture source name");
    TextureSource source;source.Name=replacement.empty()?original:replacement;source.Policy=ParseMaterialPolicy(source.Name);source.Descriptor=Descriptor(source.Name);
    auto probe=[&](const std::string& name)
    {
        std::string resource="models/texture/"+name;
        if(name.starts_with('$')){auto tga="models/texture/"+name.substr(0,name.find_last_of('.'))+".tga";if(exists(tga))resource=tga;}
        if(!exists(resource))return false;
        if(std::find(source.Probes.begin(),source.Probes.end(),resource)==source.Probes.end())source.Probes.push_back(resource);return true;
    };
    if(model.Redirected && probe(model.TexturePrefix+source.Name))
    {
        const auto draw=model.TexturePrefix+source.Name;const auto dollar=draw.find('$');
        if(dollar!=draw.npos && dollar+4<draw.size())
        {
            const auto flags=ParseMaterialPolicy(draw.substr(dollar,3)+(source.Name.size()>3?source.Name.substr(3,1):"")+"_");
            if(flags.special){source.Policy.special=true;source.Policy.cull=flags.cull;source.Policy.depth=flags.depth;source.Policy.blend=flags.blend;}
        }
        const auto at=draw.find('@');source.Descriptor.clear();
        if(at!=draw.npos && at+4<draw.size() && int((unsigned char)draw[at+2])*10+int((unsigned char)draw[at+3])-528<100)source.Descriptor=Descriptor(draw.substr(at,4));
    }
    std::string lookup=original;bool prefixImage=false;
    if(model.Redirected && probe(model.TexturePrefix+original)){lookup=model.TexturePrefix+original;source.External=true;prefixImage=true;}
    if(!replacement.empty()){lookup=replacement;source.External=true;prefixImage=false;}
    source.External|=lookup.size()>7 && lookup[0]=='#' && std::all_of(lookup.begin()+1,lookup.begin()+7,[](char c){return c>='0'&&c<='9';});
    if(!source.External)
    {
        if(lookup.starts_with('$') && lookup.size()>=3){source.External=true;source.Resource=model.PhysicalName.substr(0,model.PhysicalName.find_last_of('/')+1)+lookup.substr(0,lookup.size()-3)+"tga";source.EmbeddedFallback=!exists(source.Resource);}
        return source;
    }
    if(lookup.starts_with('$') && !probe(lookup) && lookup.size()>=3)lookup=lookup.substr(0,lookup.size()-3)+"tga";
    source.Resource="models/texture/"+lookup;
    source.EmbeddedFallback=prefixImage && !exists(source.Resource) && !lookup.starts_with('#') && !lookup.starts_with('@');
    return source;
}
}
