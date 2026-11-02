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
bool Near(float a,float b){return std::abs(a-b)<.001f;}
}
int main()
{
    using namespace csol::hud_style;
    const auto parsed=ParseCatalog(valid);
    Check(parsed.weapons.size()==1 && parsed.weapons.at("weapon_cannon").title=="黑龙炮","UTF8 official title retained");
    Check(parsed.portraits.at(1)=="resource/csol/hud/v1/p1.tga","server character identity maps portrait");
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
    for(const char* path:{CatalogPath,FontPath,"resource/csol/hud/v1/cannon.tga","resource/csol/hud/v1/ammo.tga","resource/csol/hud/v1/p1.tga"})
        Check(unique.contains(path),"closure includes catalog/font/dynamic weapon/portrait");
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
    const auto normal=MakeLayout(1280,720,1);
    Check(Near(normal.scale,1)&&Near(normal.character.x,0)&&Near(normal.character.y+normal.character.height,712),"original character panel retains eight-unit bottom gap");
    Check(Near(normal.weapon.x+normal.weapon.width,1280)&&Near(normal.weapon.y+normal.weapon.height,712),"original weapon panel retains eight-unit bottom gap");
    Check(Near(normal.portrait.y,564)&&Near(normal.portrait.width,99)&&Near(normal.portrait.height,116),"original portrait rectangle is 99 by 116 at H minus 156");
    Check(Near(normal.card.x,1106)&&Near(normal.card.y,571)&&Near(normal.card.width,170)&&Near(normal.card.height,61),"original selection card uses 61-unit row height");
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
