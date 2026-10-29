// SPDX-License-Identifier: GPL-3.0-or-later
#include "buy_menu.hpp"
#include <algorithm>
#include <bit>
#include <rapidjson/document.h>
#include <stdexcept>

namespace csol::buy {
namespace {
constexpr Item items[]{
    {220,1,"tknifeex2","三重战术飞刀","tknifeex2",30},
    {1,1,"p228","P228","p228",0},{10,1,"elites","双持精英","elites",0},
    {11,1,"fn57","Five-seveN","fn57",0},{16,1,"usp","USP","usp",0},
    {17,1,"glock","Glock 18","glock18",0},{26,1,"deagle","沙漠之鹰","deagle",0},
    {5,2,"xm1014","XM1014","xm1014",0},{21,2,"m3","M3","m3",0},
    {7,3,"mac10","MAC-10","mac10",0},{12,3,"ump45","UMP45","ump45",0},
    {19,3,"mp5","MP5","mp5",0},{23,3,"tmp","TMP","tmp",0},{30,3,"p90","P90","p90",0},
    {3,4,"scout","Scout","scout",0},{8,4,"aug","AUG","aug",0},{13,4,"sg550","SG550","sg550",0},
    {14,4,"galil","Galil","galil",0},{15,4,"famas","FAMAS","famas",0},{18,4,"awp","AWP","awp",0},
    {22,4,"m4a1","M4A1","m4a1",0},{24,4,"g3sg1","G3SG1","g3sg1",0},
    {27,4,"sg552","SG552","sg552",0},{28,4,"ak47","AK-47","ak47",0},
    {20,5,"m249","M249","m249",0},
    {144,6,"cannon","黑龙炮","cannon",4100},{725,6,"beamgun","赤血龙骧","beamgun",6100},
    {726,6,"beamgunle","御天绝影","beamgunle",6100},
    {1001,7,"vest","防弹衣","kevlar",650},{1002,7,"vesthelm","防弹衣与头盔","assaultsuit",1000},
    {1003,7,"hegren","高爆手雷","hegrenade",300},{1004,7,"flash","闪光弹","flashbang",200},
    {1005,7,"sgren","烟雾弹","smokegrenade",300},
    {2001,8,"primammo","主武器弹药","",0},{2002,9,"secammo","副武器弹药","",0}
};
using Value=rapidjson::Value;
void Require(bool ok){if(!ok)throw std::runtime_error("invalid CSOL buy menu presentation");}
const Value& Field(const Value& v,const char* name){Require(v.IsObject()&&v.HasMember(name));return v[name];}
int Number(const Value& v,int low=0,int high=2048){Require(v.IsInt()&&v.GetInt()>=low&&v.GetInt()<=high);return v.GetInt();}
std::string Text(const Value& v,std::size_t max){Require(v.IsString()&&v.GetStringLength()<=max);std::string s(v.GetString(),v.GetStringLength());Require(s.find('\0')==s.npos);return s;}
MenuRect Rect(const Value& v){Require(v.IsArray()&&v.Size()==4);return{Number(v[0]),Number(v[1]),Number(v[2],1),Number(v[3],1)};}
MenuTexture Texture(const Value& v){MenuTexture t{Text(Field(v,"path"),63),Number(Field(v,"width"),1),Number(Field(v,"height"),1)};
    Require(t.path.starts_with("gfx/csol/buy/")&&t.path.ends_with(".tga")&&t.path.find("..")==t.path.npos&&t.path.find_first_of(":\\\"\r\n\t ")==t.path.npos);return t;}
template<class T,std::size_t N,class F>void Array(const Value& v,std::array<T,N>& out,F parse){Require(v.IsArray()&&v.Size()==N);for(std::size_t i=0;i<N;++i)out[i]=parse(v[rapidjson::SizeType(i)]);}
}
std::span<const Item> Catalog(){return items;}
const Item* Find(int id){for(const auto& i:items)if(i.id==id)return &i;return nullptr;}
std::array<std::uint8_t,MessageBytes> Encode(const Packet& p){
    std::array<std::uint8_t,MessageBytes> b{1,std::uint8_t(p.kind)};
    const auto put=[&](int o,std::uint32_t n,int count){for(int i=0;i<count;++i)b[o+i]=std::uint8_t(n>>(i*8));};
    put(2,p.request,2);put(4,p.id,2);put(6,std::bit_cast<std::uint32_t>(p.price),4);put(10,std::bit_cast<std::uint32_t>(p.balance),4);b[14]=std::uint8_t(p.result);b[15]=p.team;return b;
}
bool Decode(std::span<const std::uint8_t> b,Packet& p){
    if(b.size()!=MessageBytes||b[0]!=1||b[1]>std::uint8_t(Kind::Result)||b[14]>std::uint8_t(Result::Failed)||b[15]>2)return false;
    const auto get=[&](int o,int count){std::uint32_t n=0;for(int i=0;i<count;++i)n|=std::uint32_t(b[o+i])<<(i*8);return n;};
    Packet c{Kind(b[1]),std::uint16_t(get(2,2)),std::uint16_t(get(4,2)),std::bit_cast<std::int32_t>(get(6,4)),std::bit_cast<std::int32_t>(get(10,4)),Result(b[14]),b[15]};
    if(c.price<0||c.balance<0||(c.kind==Kind::Item&&!Find(c.id)))return false;p=c;return true;
}
Presentation ParsePresentation(std::string_view json){
    Require(!json.empty()&&json.size()<=128*1024);rapidjson::Document d;d.Parse(json.data(),json.size());Require(!d.HasParseError());
    Require(Number(Field(d,"version"))==1);Presentation p;p.width=Number(Field(d,"width"),320,1280);p.height=Number(Field(d,"height"),240,960);
    p.title=Text(Field(d,"title"),128);p.font=Text(Field(d,"font"),128);p.info=Rect(Field(d,"info"));p.previous=Rect(Field(d,"previous"));p.next=Rect(Field(d,"next"));p.quit=Rect(Field(d,"quit"));p.account=Rect(Field(d,"account"));
    Array(Field(d,"slots"),p.slots,Rect);Array(Field(d,"frame"),p.frame,Texture);Array(Field(d,"close"),p.close,Texture);
    const auto& buttons=Field(d,"buttons");Require(buttons.IsArray()&&buttons.Size()==3);for(int i=0;i<3;++i)Array(buttons[i],p.buttons[i],Texture);
    for(const auto* key:{"basetext","brightcontroltext","labeldimtext","windowbg","borderbright"})Array(Field(Field(d,"colors"),key),p.colors[key],[](const Value& v){return Number(v,0,255);});
    const auto& images=Field(d,"images");Require(images.IsArray()&&images.Size()<=Catalog().size());
    for(const auto& v:images.GetArray()){const int id=Number(Field(v,"id"),1,65535);Require(Find(id)&&!p.images.contains(id));p.images.emplace(id,Texture(v));}
    for(const auto& r:p.slots)Require(r.x+r.w<=p.width&&r.y+r.h<=p.height);
    for(const auto& i:Catalog())if(i.id==144||i.id==220||i.id==725||i.id==726)Require(p.images.contains(i.id));
    return p;
}
std::vector<std::string> Resources(const Presentation& p){
    std::vector<std::string> r{"models/csol/buymenu.json"};for(const auto& t:p.frame)r.push_back(t.path);for(const auto& row:p.buttons)for(const auto& t:row)r.push_back(t.path);for(const auto& t:p.close)r.push_back(t.path);for(const auto& [id,t]:p.images)r.push_back(t.path);
    std::sort(r.begin(),r.end());r.erase(std::unique(r.begin(),r.end()),r.end());return r;
}
}
