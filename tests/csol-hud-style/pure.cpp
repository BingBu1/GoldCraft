// SPDX-License-Identifier: GPL-3.0-or-later
#include "native/csol/hud_style.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <source_location>
#include <stdexcept>
#include <vector>

namespace {
unsigned checks{}, failures{};
void Check(bool value,const char* text,std::source_location at=std::source_location::current())
{++checks;if(!value){++failures;std::cerr<<"FAIL "<<at.line()<<' '<<text<<'\n';}}
const std::string row=R"({"name":"weapon_cannon","title":"黑龙炮","weapon":"resource/csol/hud/v1/cannon.tga","ammo":"resource/csol/hud/v1/ammo.tga","ammo2":""})";
const std::string valid="{\"version\":1,\"weapons\":["+row+"],\"portraits\":[{\"id\":1,\"path\":\"resource/csol/hud/v1/p1.tga\"}]}";
std::string Replace(std::string value,std::string_view old,std::string_view next)
{const auto at=value.find(old);if(at==value.npos)throw std::runtime_error("bad test replacement");value.replace(at,old.size(),next);return value;}
void Reject(const std::string& value)
{bool failed=false;try{(void)csol::hud_style::ParseCatalog(value);}catch(const std::exception&){failed=true;}Check(failed,"malformed catalog rejected");}
std::vector<std::byte> Bytes(std::uint32_t value,unsigned length)
{std::vector<std::byte> bytes;for(unsigned i=0;i<length;++i)bytes.push_back(std::byte(value>>(i*8)));return bytes;}
std::vector<std::byte> MoneyPacket(std::uint32_t value,unsigned track=1)
{auto bytes=Bytes(value,4);bytes.push_back(std::byte(track));return bytes;}
bool Near(float a,float b){return std::abs(a-b)<.001f;}
}
int main()
{
    using namespace csol::hud_style;
    const auto parsed=ParseCatalog(valid);
    Check(parsed.weapons.size()==1 && parsed.weapons.at("weapon_cannon").title=="黑龙炮","UTF8 official title retained");
    Check(parsed.portraits.at(1)=="resource/csol/hud/v1/p1.tga","server character identity maps portrait");
    Check(parsed.weapons.at("weapon_cannon").selected.empty()&&!parsed.weapons.at("weapon_cannon").selectedKnown,"legacy equipped catalogs do not invent selected art");
    const auto withSelected=ParseCatalog(Replace(valid,"\"ammo2\":\"\"","\"ammo2\":\"\",\"selected\":\"resource/csol/hud/v1/cannon_s.tga\""));
    const auto selectedPaths=ResourcePaths(withSelected);
    Check(std::find(selectedPaths.begin(),selectedPaths.end(),"resource/csol/hud/v1/cannon_s.tga")!=selectedPaths.end(),"selected art participates in resource closure");
    const auto absentSelected=ParseCatalog(Replace(valid,"\"ammo2\":\"\"","\"ammo2\":\"\",\"selected\":\"\""));
    Check(absentSelected.weapons.at("weapon_cannon").selectedKnown&&absentSelected.weapons.at("weapon_cannon").selected.empty(),"explicit absent original weapon_s is distinct from legacy catalog");
    Reject(Replace(valid,"\"ammo2\":\"\"","\"ammo2\":\"\",\"selected\":\"../cannon_s.tga\""));
    for(const auto& text:std::vector<std::string>{"","[]","{}",Replace(valid,"\"version\":1","\"version\":2"),
        Replace(valid,"\"version\":1","\"version\":1.0"),Replace(valid,"weapon_cannon","cannon"),
        Replace(valid,"weapon_cannon","weapon_"),Replace(valid,"weapon_cannon","weapon_Cannon"),
        Replace(valid,"黑龙炮",""),Replace(valid,"黑龙炮","bad\\nname"),Replace(valid,"黑龙炮","bad\\u0000name"),
        Replace(valid,"resource/csol/hud/v1/cannon.tga","../cannon.tga"),
        Replace(valid,"resource/csol/hud/v1/cannon.tga","resource/csol/hud/v1/../cannon.tga"),
        Replace(valid,"resource/csol/hud/v1/cannon.tga","resource/csol/hud/v1/a:tga.tga"),
        Replace(valid,"resource/csol/hud/v1/cannon.tga","resource/csol/hud/v1/a file.tga"),
        Replace(valid,"resource/csol/hud/v1/cannon.tga","resource/csol/hud/v1/a.png"),
        Replace(valid,"\"id\":1","\"id\":0"),Replace(valid,"\"id\":1","\"id\":65536"),
        Replace(valid,"\"id\":1","\"id\":-1"),Replace(valid,"["+row+"]","[]"),
        Replace(valid,"["+row+"]","["+row+","+row+"]"),std::string(1024*1024+1,' '),
        Replace(valid,"黑龙炮",std::string(193,'a')),Replace(valid,"黑龙炮",std::string("\xc0\xaf",2))})Reject(text);
    Reject(Replace(valid,"\"portraits\":[{\"id\":1,\"path\":\"resource/csol/hud/v1/p1.tga\"}]",
        "\"portraits\":[{\"id\":1,\"path\":\"resource/csol/hud/v1/p1.tga\"},{\"id\":1,\"path\":\"resource/csol/hud/v1/p2.tga\"}]"));
    const auto paths=ResourcePaths(parsed);
    const std::set<std::string> unique(paths.begin(),paths.end());
    Check(unique.size()==paths.size() && std::is_sorted(paths.begin(),paths.end()),"precache order deterministic and deduplicated");
    for(const char* path:{CatalogPath,FontPath,SchemePath,"resource/csol/hud/v1/cannon.tga","resource/csol/hud/v1/ammo.tga","resource/csol/hud/v1/p1.tga"})
        Check(unique.contains(path),"closure includes catalog/font/scheme/dynamic weapon/portrait");
    Check(!unique.contains("resource/font/df_gb_y9.ttf"),"retained rollback font is not an active download dependency");
    for(const auto& resource:Resources)Check(unique.contains(resource.path),"closure includes fixed artwork");
    Check(!unique.contains(""),"optional empty ammo excluded");
    auto duplicate=parsed;duplicate.weapons.emplace("weapon_other",parsed.weapons.at("weapon_cannon"));
    Check(ResourcePaths(duplicate)==paths,"shared art only precached once");
    Vitals vitals;
    Check(!vitals.healthKnown&&!vitals.armorKnown,"fresh session does not invent vitals");
    for(auto value:{0u,1u,255u})Check(vitals.Health(Bytes(value,1))&&vitals.health==int(value)&&vitals.healthKnown,"legacy Health byte");
    for(auto value:{0u,1u,255u,256u,1700u,65535u,65536u,2147483647u})
    {
        Check(vitals.Health(Bytes(value,4))&&vitals.health==int(value),"wide Health is not clamped to 255");
        Check(vitals.Armor(Bytes(value,4))&&vitals.armor==int(value),"wide Armor is not clamped to legacy short");
    }
    for(auto value:{0u,200u,32768u,65535u})Check(vitals.Armor(Bytes(value,2))&&vitals.armor==int(value),"legacy Battery unsigned short");
    const auto saved=vitals;
    for(unsigned length:{0u,2u,3u,5u})Check(!vitals.Health(std::vector<std::byte>(length))&&vitals.health==saved.health,"bad Health length is transactional");
    for(unsigned length:{0u,1u,3u,5u})Check(!vitals.Armor(std::vector<std::byte>(length))&&vitals.armor==saved.armor,"bad Battery length is transactional");
    for(auto value:{0x80000000u,0xffffffffu})
    {Check(!vitals.Health(Bytes(value,4))&&vitals.health==saved.health,"negative LONG HP rejected");Check(!vitals.Armor(Bytes(value,4))&&vitals.armor==saved.armor,"negative LONG Armor rejected");}
    vitals={};Check(!vitals.healthKnown&&!vitals.armorKnown&&!vitals.health&&!vitals.armor,"life reset clears knowledge and values");
    Money money;
    Check(!money.known&&!money.changed&&!money.trackChange,"fresh money has no invented balance or income");
    Check(money.ApplyMoney(MoneyPacket(7500))&&money.amount==7500&&money.known&&money.delta==0&&!money.changed&&!money.trackChange,
        "first tracked signon balance is not earned income");
    Check(money.ApplyMoney(MoneyPacket(9000))&&money.amount==9000&&money.delta==1500&&money.changed&&money.trackChange,
        "tracked income comes from authoritative adjacent balances");
    const auto moneySaved=money;
    for(unsigned length:{0u,1u,2u,3u,4u,6u,7u})
        Check(!money.ApplyMoney(std::vector<std::byte>(length))&&money.amount==moneySaved.amount&&money.delta==moneySaved.delta&&
            money.known==moneySaved.known&&money.changed==moneySaved.changed&&money.trackChange==moneySaved.trackChange,
            "malformed Money length preserves all state transactionally");
    Check(money.ApplyMoney(MoneyPacket(4250))&&money.delta==-4750&&money.trackChange,"purchase preserves signed expense");
    Check(money.ApplyMoney(MoneyPacket(32000,0))&&money.amount==32000&&money.delta==27750&&money.changed&&!money.trackChange,
        "untracked update changes balance without change notification");
    Check(money.ApplyMoney(MoneyPacket(32000))&&money.delta==0&&!money.changed&&!money.trackChange,"repeated balance does not create income");
    for(auto hidden:{0xffffffffu,0x80000000u})
    {
        Check(money.ApplyMoney(MoneyPacket(hidden))&&money.known&&money.amount<0&&money.delta==0&&!money.changed&&!money.trackChange,
            "negative signed balance remains known but cannot create unsigned money or a delta");
        Check(money.ApplyMoney(MoneyPacket(7500))&&money.amount==7500&&money.delta==0&&!money.changed&&!money.trackChange,
            "recovery from hidden balance does not award artificial income");
    }
    Check(money.ApplyMoney(MoneyPacket(0))&&money.delta==-7500,"zero balance remains a valid tracked expense");
    Check(money.ApplyMoney(MoneyPacket(2147483647u,255))&&money.delta==2147483647&&money.trackChange,
        "maximum signed amount and nonzero track flag are accepted without truncation");
    Check(money.ApplyMoney(MoneyPacket(0))&&money.delta==-2147483647,"maximum expense does not overflow signed delta");
    money={};Check(!money.known&&!money.changed&&!money.trackChange&&!money.amount&&!money.delta,"session reset clears all money knowledge");
    for(float scale:{.5f,1.f,2.f})for(bool scenario:{false,true})
    {
        const auto panel=MoneyBackground(scale,scenario);
        Check(Near(panel.x,0)&&Near(panel.y,(scenario?195.f:154.f)*scale)&&Near(panel.width,191*scale)&&Near(panel.height,28*scale),
            "original money panel is anchored at left with independent scenario offset");
    }
    const auto normal=MakeLayout(1280,720,1);
    Check(Near(normal.scale,1)&&Near(normal.character.x,0)&&Near(normal.character.y+normal.character.height,712),"original character panel retains eight-unit bottom gap");
    Check(Near(normal.weapon.x+normal.weapon.width,1280)&&Near(normal.weapon.y+normal.weapon.height,712),"original weapon panel retains eight-unit bottom gap");
    Check(Near(normal.portrait.y,564)&&Near(normal.portrait.width,99)&&Near(normal.portrait.height,116),"original portrait rectangle is 99 by 116 at H minus 156");
    Check(Near(normal.card.x,1106)&&Near(normal.card.y,571)&&Near(normal.card.width,170)&&Near(normal.card.height,61),"original selection card uses 61-unit row height");
    for(auto dimensions:{std::array<int,2>{640,480},{800,600},{1024,768},{1280,720},{1920,1080},{3440,1440}})
    {
        const auto fixed=MakeLayout(dimensions[0],dimensions[1],1);
        Check(Near(fixed.scale,1)&&Near(fixed.card.width,170)&&Near(fixed.card.height,61)&&Near(fixed.portrait.width,99),
            "normal resolutions retain original fixed-pixel panel and glyph sizing");
    }
    for(auto dimensions:{std::array<int,2>{640,480},{800,600},{1024,768},{1920,1080},{3440,1440},{320,200}})
        for(float user:{-1.f,0.f,.5f,1.f,2.f,100.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()})
        {
            const auto layout=MakeLayout(dimensions[0],dimensions[1],user);
            Check(std::isfinite(layout.scale)&&layout.scale>0,"scale always finite and positive for valid viewport");
            for(const auto& rect:{layout.character,layout.portrait,layout.weapon,layout.card})
                Check(rect.x>=0&&rect.y>=0&&rect.x+rect.width<=dimensions[0]+.01f&&rect.y+rect.height<=dimensions[1]+.01f,"HUD rectangle remains in viewport");
        }
    for(auto dimensions:{std::array<int,2>{0,720},{1280,0},{-1,720},{1280,-1}})
        Check(MakeLayout(dimensions[0],dimensions[1],1).scale==0,"invalid viewport does not draw");
    std::cout<<"hud_style_pure checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
