// SPDX-License-Identifier: GPL-3.0-or-later
#include "buy_menu.hpp"
#include <algorithm>
#include <bit>
#include <charconv>
#include <rapidjson/document.h>
#include <stdexcept>

namespace csol::buy {
namespace {
constexpr Item items[]{
    // Authored guest placement/price, separate from original CSO item records.
    {896,1,"cof_glock","客串武器·Glock 19","cof_glock",650},
    {220,1,"tknifeex2","三重战术飞刀","tknifeex2",30},
    {130,1,"skull1","噬魂SKULL-1","skull1",1200},
    {312,1,"skull2","噬血狂袭SKULL-2","skull2",1550},
    {1,1,"p228","P228","p228",0},{10,1,"elites","双持精英","elites",0},
    {11,1,"fn57","Five-seveN","fn57",0},{16,1,"usp","USP","usp",0},
    {17,1,"glock","Glock 18","glock18",0},{26,1,"deagle","沙漠之鹰","deagle",0},
    {5,2,"xm1014","XM1014","xm1014",0},{21,2,"m3","M3","m3",0},
    {203,2,"balrog11","龙炎Balrog-Ⅺ","balrog11",5750},
    {7,3,"mac10","MAC-10","mac10",0},{12,3,"ump45","UMP45","ump45",0},
    {142,3,"skull3","喋血SKULL-3","skull3",4000},
    {19,3,"mp5","MP5","mp5",0},{23,3,"tmp","TMP","tmp",0},{30,3,"p90","P90","p90",0},
    {3,4,"scout","Scout","scout",0},{8,4,"aug","AUG","aug",0},{13,4,"sg550","SG550","sg550",0},
    {14,4,"galil","Galil","galil",0},{15,4,"famas","FAMAS","famas",0},{18,4,"awp","AWP","awp",0},
    {22,4,"m4a1","M4A1","m4a1",0},{24,4,"g3sg1","G3SG1","g3sg1",0},
    {27,4,"sg552","SG552","sg552",0},{28,4,"ak47","AK-47","ak47",0},
    {112,4,"skull5","死亡骑士SKULL-5","skull5",6000},
    {20,5,"m249","M249","m249",0},{197,5,"k3","K3","k3",4750},
    // Original IGBuyMenuMgr: mode 15 has no override for IDs 96 or 203.
    {96,5,"m249ex","碎魂者SKULL-7","m249ex",9000},
    {144,8,"cannon","黑龙炮","cannon",4100},{725,8,"beamgun","赤血龙骧","beamgun",6100},
    {726,8,"beamgunle","御天绝影","beamgunle",6100},
    {1001,8,"vest","防弹衣","kevlar",650},{1002,8,"vesthelm","防弹衣与头盔","assaultsuit",1000},
    {1003,8,"hegren","高爆手雷","hegrenade",300},{1004,8,"flash","闪光弹","flashbang",200},
    {1005,8,"sgren","烟雾弹","smokegrenade",300},
    // MP 1140D370: the mode-15 knife selection path has no money debit.
    // HW 380D4738 maps default knife index 0 to item 161 / InGameID 4000.
    {29,9,"knife","海豹短刀","defaultknife",0,WeaponSlot::Melee},
    // Original CSOL equips knives from the inventory; the HW 40-byte knife
    // records (380D48C8/380D49B8) are not 0x30 price records. The human chose
    // $0 on 2026-11-04 until an original purchase price is found.
    {1022,9,"dragonsword","青龙偃月刀","dragonsword",0,WeaponSlot::Melee},
    {1023,9,"skullaxe","旋风SKULL-9","skullaxe",0,WeaponSlot::Melee},
    {2001,6,"primammo","主武器弹药","",0},{2002,7,"secammo","副武器弹药","",0}
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
Favorites ParseFavorites(std::string_view text){
    Favorites result{};std::size_t index=0;
    while(!text.empty()&&index<result.size()){
        const auto end=text.find(',');const auto token=text.substr(0,end);unsigned id{};
        const auto parsed=std::from_chars(token.data(),token.data()+token.size(),id);
        if(parsed.ec!=std::errc{}||parsed.ptr!=token.data()+token.size())return {};
        if(id&&id<2000&&Find(int(id))&&std::find(result.begin(),result.end(),id)==result.end())result[index]=std::uint16_t(id);
        ++index;if(end==text.npos)break;text.remove_prefix(end+1);
    }
    return result;
}
std::string SerializeFavorites(const Favorites& favorites){
    std::string text;for(const auto id:favorites){if(!text.empty())text+=',';text+=std::to_string(id);}return text;
}
bool InTeamList(const Item& item,int team){
    switch(item.id){
    case 8:case 11:case 13:case 15:case 22:case 23:return team==2;
    case 7:case 10:case 14:case 24:case 27:case 28:return team==1;
    default:return team==1||team==2;
    }
}
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
    const int version=Number(Field(d,"version"),1,3);Presentation p;p.width=Number(Field(d,"width"),320,1280);p.height=Number(Field(d,"height"),240,960);
    p.title=Text(Field(d,"title"),128);p.font=Text(Field(d,"font"),128);p.info=Rect(Field(d,"info"));p.previous=Rect(Field(d,"previous"));p.next=Rect(Field(d,"next"));p.quit=Rect(Field(d,"quit"));p.account=Rect(Field(d,"account"));
    Array(Field(d,"slots"),p.slots,Rect);Array(Field(d,"frame"),p.frame,Texture);Array(Field(d,"close"),p.close,Texture);
    const auto& buttons=Field(d,"buttons");Require(buttons.IsArray()&&buttons.Size()==3);for(int i=0;i<3;++i)Array(buttons[i],p.buttons[i],Texture);
    if(version>=3){
        p.normalizedLayout=true;p.root=Rect(Field(d,"root"));p.listPanel=Rect(Field(d,"listPanel"));p.favoritePanel=Rect(Field(d,"favoritePanel"));
        Require(p.root.w==p.width && p.root.h==p.height);
        p.listTitle=Text(Field(d,"listTitle"),128);p.favoriteDescription=Text(Field(d,"favoriteDescription"),256);
        Array(Field(d,"teamNames"),p.teamNames,[](const Value& v){return Text(v,128);});
        const auto& tabs=Field(d,"tabs");Require(tabs.IsArray()&&tabs.Size()==6);for(int i=0;i<6;++i)Array(tabs[i],p.tabs[i],Texture);
    }
    for(int i=0;i<8;++i)p.favorites[i]={374+(i%2)*110,52+(i/2)*35,107,31};
    if(version>=2){
        if(version<3)p.search=Rect(Field(d,"search"));p.rebuy=Rect(Field(d,"rebuy"));
        if(version<3)Array(Field(d,"teams"),p.teams,Rect);Array(Field(d,"favorites"),p.favorites,Rect);
        Array(Field(d,"slotBackground"),p.slotBackground,Texture);
        p.keyboard=Texture(Field(d,"keyboard"));p.favoriteBackground=Texture(Field(d,"favoriteBackground"));
    }
    for(const auto* key:{"basetext","brightcontroltext","labeldimtext","windowbg","borderbright"})Array(Field(Field(d,"colors"),key),p.colors[key],[](const Value& v){return Number(v,0,255);});
    if(version>=3){
        auto& info=p.weaponInfo;const auto& value=Field(d,"weaponInfo");
        info.priceLabel=Text(Field(value,"priceLabel"),128);
        Array(Field(value,"attributeLabels"),info.attributeLabels,[](const Value& v){return Text(v,128);});
        Array(Field(value,"gradeLabels"),info.gradeLabels,[](const Value& v){return Text(v,128);});
        info.background=Texture(Field(value,"background"));info.divider=Texture(Field(value,"divider"));
        const auto& scroll=Field(value,"scroll");
        const auto& colors=Field(scroll,"colors");
        for(const auto* key:{"richTextFg","richTextBg","barBg","sliderFg","sliderBg",
                             "arrowFg","arrowBg","arrowArmedFg","arrowArmedBg",
                             "arrowDepressedFg","arrowDepressedBg"})
            Array(Field(colors,key),info.scroll.colors[key],[](const Value& v){return Number(v,0,255);});
        Array(Field(scroll,"slider"),info.scroll.slider,Texture);
        const auto& grades=Field(value,"grades");Require(grades.IsObject()&&grades.MemberCount()<=9);
        for(auto it=grades.MemberBegin();it!=grades.MemberEnd();++it){
            const auto token=Text(it->name,2);int grade{};const auto result=std::from_chars(token.data(),token.data()+token.size(),grade);
            Require(result.ec==std::errc{}&&result.ptr==token.data()+token.size()&&grade>=0&&grade<=8&&!info.grades.contains(grade));
            info.grades.emplace(grade,Texture(it->value));
        }
        const auto& items=Field(value,"items");Require(items.IsArray()&&items.Size()<=Catalog().size());
        for(const auto& item:items.GetArray()){
            const int id=Number(Field(item,"id"),1,65535);Require(Find(id)&&!info.items.contains(id));
            WeaponInfoItem detail;detail.itemId=Number(Field(item,"itemId"),1,1000000);
            detail.name=Text(Field(item,"name"),256);detail.description=Text(Field(item,"description"),16384);
            const auto& table=Field(item,"table");Require(table.IsBool()||table.IsNull());if(table.IsBool())detail.table=table.GetBool();
            Array(Field(item,"values"),detail.values,[](const Value& v){return Text(v,128);});
            Array(Field(item,"grade"),detail.grade,[](const Value& v){return Number(v,-1,8);});
            info.items.emplace(id,std::move(detail));
        }
    }
    const auto& images=Field(d,"images");Require(images.IsArray()&&images.Size()<=Catalog().size());
    for(const auto& v:images.GetArray()){const int id=Number(Field(v,"id"),1,65535);Require(Find(id)&&!p.images.contains(id));p.images.emplace(id,Texture(v));}
    for(const auto& r:p.slots)Require(r.x+r.w<=p.width&&r.y+r.h<=p.height);
    const auto inside=[&](const MenuRect& r){Require(r.x+r.w<=p.width&&r.y+r.h<=p.height);};
    for(const auto& r:p.favorites){if(version<3)inside(r);else Require(r.x+r.w<=p.favoritePanel.w && r.y+r.h<=p.favoritePanel.h);}
    for(const auto& r:p.teams)inside(r);inside(p.search);inside(p.rebuy);inside(p.account);
    if(version>=3){inside(p.listPanel);inside(p.favoritePanel);}
    for(const auto& i:Catalog())if(i.id==29||i.id==96||i.id==144||i.id==197||i.id==203||i.id==220||i.id==725||i.id==726||i.id==1022||i.id==1023)Require(p.images.contains(i.id));
    return p;
}
std::vector<std::string> Resources(const Presentation& p){
    std::vector<std::string> r{PresentationResource,MenuFontResource,MenuTabClickResource};for(const auto& t:p.frame)r.push_back(t.path);for(const auto& row:p.buttons)for(const auto& t:row)r.push_back(t.path);for(const auto& row:p.tabs)for(const auto& t:row)if(!t.path.empty())r.push_back(t.path);for(const auto& t:p.close)r.push_back(t.path);for(const auto& [id,t]:p.images)r.push_back(t.path);
    for(const auto& t:p.slotBackground)if(!t.path.empty())r.push_back(t.path);
    for(const auto* t:{&p.keyboard,&p.favoriteBackground,&p.weaponInfo.background,&p.weaponInfo.divider})if(!t->path.empty())r.push_back(t->path);
    for(const auto& [id,image]:p.weaponInfo.grades)r.push_back(image.path);
    for(const auto& image:p.weaponInfo.scroll.slider)if(!image.path.empty())r.push_back(image.path);
    std::sort(r.begin(),r.end());r.erase(std::unique(r.begin(),r.end()),r.end());return r;
}
}
