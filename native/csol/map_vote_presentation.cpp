// SPDX-License-Identifier: GPL-3.0-or-later
#include "map_vote_presentation.hpp"
#include "scenario/map_vote.hpp"
#include <rapidjson/document.h>
#include <algorithm>
#include <stdexcept>

namespace csol::scenario::map_vote {
namespace {
using Value=rapidjson::Value;
const Value& Field(const Value& object,const char* name)
{
    if (!object.IsObject() || !object.HasMember(name)) throw std::runtime_error("missing map-vote field");
    return object[name];
}
int Integer(const Value& value,int low,int high)
{
    if (!value.IsInt() || value.GetInt()<low || value.GetInt()>high) throw std::runtime_error("invalid map-vote number");
    return value.GetInt();
}
std::string Text(const Value& value,std::size_t maximum)
{
    if (!value.IsString() || value.GetStringLength()>maximum ||
        std::string_view(value.GetString(),value.GetStringLength()).find('\0')!=std::string_view::npos)
        throw std::runtime_error("invalid map-vote text");
    return {value.GetString(),value.GetStringLength()};
}
std::string Path(const Value& value)
{
    auto text=Text(value,160);
    if (text.empty() || text.front()=='/' || text.find("..")!=text.npos ||
        text.find_first_of("\\:\r\n\t")!=text.npos ||
        !(text.starts_with("gfx/csol/mapvote/") || text.starts_with("resource/csol/mapvote/")))
        throw std::runtime_error("unsafe map-vote resource path");
    return text;
}
const Value& Array(const Value& value,std::size_t size)
{
    if (!value.IsArray() || value.Size()!=size) throw std::runtime_error("invalid map-vote array");
    return value;
}
MenuRect Rect(const Value& value)
{
    Array(value,4);
    return {Integer(value[0],0,4096),Integer(value[1],0,4096),
        Integer(value[2],1,4096),Integer(value[3],1,4096)};
}
MenuTexture Texture(const Value& value)
{
    auto path=Path(Field(value,"path"));
    if (!path.ends_with(".tga")) throw std::runtime_error("map-vote image must be TGA");
    return {std::move(path),Integer(Field(value,"width"),1,4096),Integer(Field(value,"height"),1,4096)};
}
template<std::size_t N> void Images(const Value& value,std::array<MenuTexture,N>& output)
{
    Array(value,N);for(std::size_t i=0;i<N;++i)output[i]=Texture(value[rapidjson::SizeType(i)]);
}
}
const MapArtwork* Presentation::Find(std::uint16_t id) const
{
    const auto it=std::find_if(maps.begin(),maps.end(),[&](const auto& map){return map.id==id;});
    return it==maps.end()?nullptr:&*it;
}
Presentation ParsePresentation(std::string_view json)
{
    if (json.size()>1024*1024) throw std::runtime_error("map-vote presentation too large");
    rapidjson::Document root;root.Parse<rapidjson::kParseValidateEncodingFlag>(json.data(),json.size());
    if (root.HasParseError() || !root.IsObject() || Integer(Field(root,"version"),1,1)!=1)
        throw std::runtime_error("invalid map-vote presentation");
    Presentation p;
    p.width=Integer(Field(root,"width"),320,4096);p.height=Integer(Field(root,"height"),240,4096);
    p.font=Text(Field(root,"font"),100);p.fontResource=Path(Field(root,"fontResource"));
    if (!p.fontResource.ends_with(".ttf")) throw std::runtime_error("invalid map-vote font resource");
    p.fontSize=Integer(Field(root,"fontSize"),8,48);p.fontWeight=Integer(Field(root,"fontWeight"),0,1000);
    p.columns=Integer(Field(root,"columns"),1,4);p.rows=Integer(Field(root,"rows"),1,4);
    p.strideX=Integer(Field(root,"strideX"),1,1024);p.strideY=Integer(Field(root,"strideY"),1,1024);
    p.list=Rect(Field(root,"list"));p.info=Rect(Field(root,"info"));p.preview=Rect(Field(root,"preview"));
    p.confirm=Rect(Field(root,"confirm"));p.close=Rect(Field(root,"close"));
    p.scrollbar=Rect(Field(root,"scrollbar"));
    p.card=Rect(Field(root,"card"));p.cardImage=Rect(Field(root,"cardImage"));p.cardLabel=Rect(Field(root,"cardLabel"));
    for(const auto& r:{p.list,p.info,p.preview,p.confirm,p.close,p.scrollbar})
        if(r.x+r.w>p.width||r.y+r.h>p.height)throw std::runtime_error("map-vote control outside panel");
    if (p.cardImage.x+p.cardImage.w>p.card.w || p.cardImage.y+p.cardImage.h>p.card.h ||
        p.cardLabel.x+p.cardLabel.w>p.card.w || p.cardLabel.y+p.cardLabel.h>p.card.h ||
        (p.columns-1)*p.strideX+p.card.w>p.list.w || (p.rows-1)*p.strideY+p.card.h>p.list.h)
        throw std::runtime_error("map-vote cards outside list");
    if(p.scrollbar.x<p.list.x||p.scrollbar.y<p.list.y||p.scrollbar.x+p.scrollbar.w>p.list.x+p.list.w||
        p.scrollbar.y+p.scrollbar.h>p.list.y+p.list.h)throw std::runtime_error("map-vote scrollbar outside list");
    const auto& cards=Array(Field(root,"cards"),3);
    for(unsigned i=0;i<3;++i)Images(cards[i],p.cards[i]);
    Images(Field(root,"frame"),p.frame);Images(Field(root,"infoBackground"),p.infoBackground);
    const auto& buttons=Array(Field(root,"buttons"),3);
    for(unsigned i=0;i<3;++i)Images(buttons[i],p.buttons[i]);
    Images(Field(root,"closeImages"),p.closeImages);
    Images(Field(root,"scrollThumb"),p.scrollThumb);
    const auto& maps=Field(root,"maps");
    if(!maps.IsArray()||maps.Empty()||maps.Size()>MaxChoices)throw std::runtime_error("invalid map-vote map count");
    for(const auto& item:maps.GetArray())
    {
        MapArtwork map;map.id=std::uint16_t(Integer(Field(item,"id"),1,65535));
        map.mapName=Text(Field(item,"map"),31);map.name=Text(Field(item,"name"),128);
        map.description=Text(Field(item,"description"),2048);
        if(!ValidMapName(map.mapName)||map.name.empty()||p.Find(map.id)||
            std::any_of(p.maps.begin(),p.maps.end(),[&](const auto& prior){return prior.mapName==map.mapName;}))
            throw std::runtime_error("invalid map-vote map identity");
        map.thumbnail=Texture(Field(item,"thumbnail"));map.detail=Texture(Field(item,"detail"));
        p.maps.push_back(std::move(map));
    }
    return p;
}
std::vector<std::string> Resources(const Presentation& p)
{
    std::vector<std::string> paths{PresentationResource,p.fontResource};
    const auto add=[&](const auto& group){for(const auto& image:group)paths.push_back(image.path);};
    add(p.frame);add(p.infoBackground);add(p.closeImages);add(p.scrollThumb);
    for(const auto& group:p.cards)add(group);for(const auto& group:p.buttons)add(group);
    for(const auto& map:p.maps){paths.push_back(map.thumbnail.path);paths.push_back(map.detail.path);}
    std::sort(paths.begin(),paths.end());paths.erase(std::unique(paths.begin(),paths.end()),paths.end());return paths;
}
}
