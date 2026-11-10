// SPDX-License-Identifier: GPL-3.0-or-later
#include "character_menu.hpp"
#include <rapidjson/document.h>
#include <stdexcept>

namespace csol {
namespace {
using Value=rapidjson::Value;
void Require(bool ok){if(!ok)throw std::runtime_error("invalid CSOL class menu presentation");}
const Value& Field(const Value& v,const char* name){Require(v.IsObject() && v.HasMember(name));return v[name];}
int Number(const Value& v,int low=0,int high=2048){Require(v.IsInt() && v.GetInt()>=low && v.GetInt()<=high);return v.GetInt();}
std::string Text(const Value& v,size_t maximum){Require(v.IsString() && v.GetStringLength()<=maximum);std::string s(v.GetString(),v.GetStringLength());Require(s.find('\0')==s.npos);return s;}
MenuRect Rect(const Value& v){Require(v.IsArray() && v.Size()==4);return {Number(v[0]),Number(v[1]),Number(v[2],1),Number(v[3],1)};}
MenuTexture Texture(const Value& v){MenuTexture t{Text(Field(v,"path"),63),Number(Field(v,"width"),1),Number(Field(v,"height"),1)};
    Require(t.path.starts_with("gfx/csol/ui/") && t.path.ends_with(".tga") && t.path.find("..") == t.path.npos && t.path.find_first_of(":\\\"\r\n\t ")==t.path.npos);return t;}
template<class T,size_t N,class F>void Array(const Value& v,std::array<T,N>& out,F parse){Require(v.IsArray() && v.Size()==N);for(size_t i=0;i<N;++i)out[i]=parse(v[rapidjson::SizeType(i)]);}
}
CharacterMenu ParseCharacterMenu(std::string_view json)
{
    Require(!json.empty() && json.size()<=1024*1024);rapidjson::Document d;d.Parse(json.data(),json.size());Require(!d.HasParseError());
    const int version=Number(Field(d,"version"));Require(version>=1 && version<=5);CharacterMenu m;
    m.width=Number(Field(d,"width"),320,1280);m.height=Number(Field(d,"height"),240,960);
    if(version>=5){
        m.normalizedLayout=true;m.root=Rect(Field(d,"root"));
        Require(m.root.w==m.width && m.root.h==m.height);
        m.scenarioImage=Rect(Field(Field(d,"scenario"),"image"));
        m.scenarioDescription=Rect(Field(Field(d,"scenario"),"description"));
    }
    m.info=Rect(Field(d,"info"));m.previous=Rect(Field(d,"previous"));m.next=Rect(Field(d,"next"));
    Array(Field(d,"slots"),m.slots,Rect);if(version<5)Array(Field(d,"teams"),m.teams,Rect);Array(Field(d,"frame"),m.frame,Texture);Array(Field(d,"close"),m.close,Texture);
    const auto& buttons=Field(d,"buttons");Require(buttons.IsArray() && buttons.Size()==3);for(int i=0;i<3;++i)Array(buttons[i],m.buttons[i],Texture);
    if(version>=5){const auto& tabs=Field(d,"tabs");Require(tabs.IsArray() && tabs.Size()==6);for(int i=0;i<6;++i)Array(tabs[i],m.tabs[i],Texture);}
    const auto& labels=Field(d,"labels");Array(Field(labels,"teams"),m.teamNames,[](const Value& v){return Text(v,128);});
    m.previousLabel=Text(Field(labels,"previous"),128);m.nextLabel=Text(Field(labels,"next"),128);m.font=Text(Field(Field(d,"font"),"name"),128);
    if(version>=2){m.pin=Texture(Field(d,"pin"));m.currentLabel=Text(Field(d,"currentLabel"),128);}
    if(version>=3){Array(Field(d,"slotBackground"),m.slotBackground,Texture);m.keyboard=Texture(Field(d,"keyboard"));}
    const auto& colors=Field(d,"colors");for(const char* key:{"basetext","brightcontroltext","labeldimtext","windowbg","borderbright"})
        Array(Field(colors,key),m.colors[key],[](const Value& v){return Number(v,0,255);});
    if(version>=4){
        const auto& layout=Field(d,"layout");const auto& full=Field(layout,"fullScreenFrame");Require(full.IsBool());m.fullScreenFrame=full.GetBool();
        if(version<5){m.title=Rect(Field(layout,"title"));m.pageLabel=Rect(Field(layout,"pageLabel"));m.closeButton=Rect(Field(layout,"closeButton"));}
        const auto& font=Field(d,"font");m.fontSize=Number(Field(font,"size"),6,32);m.fontWeight=Number(Field(font,"weight"),0,1000);
        m.titleSize=Number(Field(font,"titleSize"),6,48);m.titleWeight=Number(Field(font,"titleWeight"),0,1000);
        const auto& stats=Field(d,"stats");m.stats.heading=Rect(Field(stats,"heading"));m.stats.headingText=Text(Field(stats,"headingText"),128);
        if(version>=5)m.stats.gauge=Texture(Field(stats,"gauge"));
        m.stats.maximum=Number(Field(stats,"maximum"),1,255);
        Array(Field(stats,"labels"),m.stats.labels,Rect);Array(Field(stats,"bars"),m.stats.bars,Rect);Array(Field(stats,"values"),m.stats.values,Rect);
        Array(Field(stats,"names"),m.stats.names,[](const Value& v){return Text(v,128);});
        for(const char* key:{"statheading","statbar","statvalue"})Array(Field(colors,key),m.colors[key],[](const Value& v){return Number(v,0,255);});
    }
    const auto& descriptions=Field(d,"descriptions");Require(descriptions.IsObject() && descriptions.MemberCount()<=512);
    for(auto it=descriptions.MemberBegin();it!=descriptions.MemberEnd();++it){
        const std::string key=Text(it->name,5);Require(!key.empty() && key.find_first_not_of("0123456789")==key.npos);
        const unsigned id=std::stoul(key);Require(id>0 && id<=65535 && !m.descriptions.contains(id));
        CharacterDescription detail{Text(Field(it->value,"text"),8192),Rect(Field(it->value,"image")),Rect(Field(it->value,"description"))};
        if(version>=4 && it->value.HasMember("defaultCaps")){
            std::array<int,5> caps;Array(it->value["defaultCaps"],caps,[](const Value& v){return Number(v,0,255);});detail.defaultCaps=caps;
        }
        m.descriptions.emplace(id,std::move(detail));
    }
    for(const auto& r:m.slots)Require(r.x+r.w<=m.width && r.y+r.h<=m.height);
    if(version>=4){
        const auto inside=[&](const MenuRect& r){Require(r.x+r.w<=m.width && r.y+r.h<=m.height);};
        for(const auto& r:m.teams)inside(r);inside(m.previous);inside(m.next);inside(m.title);inside(m.pageLabel);inside(m.closeButton);
        // CSO's ClassInfo extends past its parent (263+400 > 606). It is
        // clipped by that parent, not resized to fit a guessed content area.
        if(version<5)inside(m.info);
        else{
            Require(m.info.x<m.width && m.info.y<m.height);
            for(const auto& r:{m.scenarioImage,m.scenarioDescription})Require(r.x+r.w<=m.info.w && r.y+r.h<=m.info.h);
        }
        const auto statsInside=[&](const MenuRect& r){
            if(version<5)inside(r);
            else Require(r.x+r.w<=m.info.w && r.y+r.h<=m.info.h);
        };
        statsInside(m.stats.heading);
        for(const auto& r:m.stats.labels)statsInside(r);for(const auto& r:m.stats.bars)statsInside(r);for(const auto& r:m.stats.values)statsInside(r);
        for(const auto& [id,detail]:m.descriptions){
            for(auto r:{detail.image,detail.description}){Require(r.x+r.w<=m.info.w && r.y+r.h<=m.info.h);if(version<5){r.x+=m.info.x;r.y+=m.info.y;inside(r);}}
        }
    }
    return m;
}
}
