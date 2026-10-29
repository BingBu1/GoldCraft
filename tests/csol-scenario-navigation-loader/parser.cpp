// SPDX-License-Identifier: GPL-3.0-or-later
#include "support.hpp"
#include <limits>
#include <set>

namespace nav_loader_test
{
void ParserCases(const char* actualNav)
{
    const auto bytes=Read(std::filesystem::u8path(actualNav)); NavigationFile file; ConfigError error;
    Require(bytes.size()==101509,"frozen first map has inventoried byte length");
    Require(ReadNavigationV5(Bytes(bytes),file,error),"real first-map NAV parses including traversal sentinel seven");
    Check(file.bspSize==7361964 && file.areas.size()==1247 && file.places.empty() && file.trailingBytes==0,"actual header and complete consumption");
    std::size_t edges{},hiding{},approaches{},encounters{},encounterSpots{},zeros{},sevens{};
    std::set<std::uint32_t> ids,spotIds;
    for(const auto& a:file.areas) { ids.insert(a.id); for(const auto& s:a.hidingSpots)spotIds.insert(s.id); }
    for(const auto& a:file.areas)
    {
        for(const auto& c:a.connections)edges+=c.size();
        hiding+=a.hidingSpots.size(); approaches+=a.approaches.size(); encounters+=a.encounters.size();
        for(const auto& p:a.approaches)sevens+=(p.previousHow==7)+(p.nextHow==7);
        for(const auto& e:a.encounters)
        {
            if(!ids.contains(e.from)){Check(e.from==0,"actual unresolved from is zero");++zeros;}
            if(!ids.contains(e.to)){Check(e.to==0,"actual unresolved to is zero");++zeros;}
            encounterSpots+=e.spots.size();
            for(const auto& s:e.spots)if(!spotIds.contains(s.id)){Check(s.id==0,"actual unresolved spot is zero");++zeros;}
        }
    }
    Check(edges==4175 && hiding==24 && approaches==271 && encounters==322 && encounterSpots==196,"all actual record families retained");
    Check(zeros==640 && sevens==3,"640 null references and three original traversal sentinels remain data");
    for(const auto [id,index]:std::array<std::pair<unsigned,unsigned>,3>{{{456,3},{1309,5},{1780,2}}})
    {
        const auto found=std::find_if(file.areas.begin(),file.areas.end(),[&](const auto& a){return a.id==id;});
        Require(found!=file.areas.end() && found->approaches.size()>index,"inventoried sentinel location exists");
        Check(found->approaches[index].previous==0 && found->approaches[index].previousHow==7,"original absent previous area retains unspecified traversal");
    }
    const auto reversed=std::find_if(file.areas.begin(),file.areas.end(),[](const auto& a){return a.id==3148;});
    Require(reversed!=file.areas.end(),"reversed first-map area exists");
    Check(reversed->low.y==-204.2353057861328f && reversed->high.y==-225.0f,"real reversed Y is not normalized or rejected");

    const auto rich=RichFile(); const auto valid=Encode(rich);
    Require(ReadNavigationV5(Bytes(valid),file,error),"rich binary fixture accepted");
    Check(file.areas[0].connections[0]==std::vector<std::uint32_t>({42,42,0,999}),"file order duplicate and unresolved connections preserved");
    Check(file.areas[0].hidingSpots[0].flags==5 && file.areas[0].encounters[0].spots[1].position==255,"hiding flags and normalized-position source byte retained");
    // Every prefix of one complete mixed-format document fails transactionally.
    for(std::size_t size=0;size<valid.size();++size)RejectPreserving(valid.substr(0,size),"truncated mixed-format prefix rejected");
    auto extended=valid+std::string("tail\0bytes",10);
    Require(ReadNavigationV5(Bytes(extended),file,error),"original trailing extension tolerated");
    Check(file.trailingBytes==10,"trailing byte count reported");
    NavigationFile empty;
    Require(ReadNavigationV5(Bytes(Encode(empty)),file,error) && file.areas.empty(),"original zero-area document remains valid metadata");
    for(auto value:{0u,1u,4u,6u,0xffffffffu})
    {
        auto invalid=valid; for(unsigned byte=0;byte<4;++byte)invalid[4+byte]=static_cast<char>(value>>(byte*8));
        RejectPreserving(invalid,"non-v5 version requires a separate loader");
    }
    auto wrongMagic=valid;wrongMagic[0]^=1;RejectPreserving(wrongMagic,"wrong magic rejected");
    auto tooMany=Encode(empty);for(int i=14;i<18;++i)tooMany[i]=static_cast<char>(0xff);RejectPreserving(tooMany,"unbounded area count rejected before allocation");
    for(unsigned how:{0u,6u,7u})
    {
        auto f=rich;f.areas[0].approaches[0].previousHow=static_cast<std::uint8_t>(how);
        Require(ReadNavigationV5(Bytes(Encode(f)),file,error),"supported traversal or unspecified sentinel accepted");
        Check(file.areas[0].approaches[0].previousHow==how,"traversal metadata is not coerced");
    }
    for(unsigned how:{8u,255u})
    {
        auto f=rich;f.areas[0].approaches[0].nextHow=static_cast<std::uint8_t>(how);
        RejectPreserving(Encode(f),"host safely rejects unsupported traversal enum beyond sentinel; original MP accepted the raw byte");
    }
    auto f=rich;f.areas[0].approaches.resize(16);Require(ReadNavigationV5(Bytes(Encode(f)),file,error),"SDK capacity sixteen accepted");
    f.areas[0].approaches.resize(17);RejectPreserving(Encode(f),"approach seventeen rejected before SDK fixed array write");
    f=rich;f.areas[1].id=f.areas[0].id;RejectPreserving(Encode(f),"duplicate area id rejected");
    f=rich;f.areas[1].hidingSpots={f.areas[0].hidingSpots[0]};RejectPreserving(Encode(f),"duplicate spot id rejected across areas");
    f=rich;f.areas[0].encounters[0].toDirection=4;RejectPreserving(Encode(f),"unsafe encounter direction rejected");
    for(float value:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()})
    { f=rich;f.areas[0].low.x=value;RejectPreserving(Encode(f),"nonfinite geometry rejected"); }
    f=rich;f.places={std::string("a\0b",3)};RejectPreserving(Encode(f),"embedded place-name NUL rejected");
    f=rich;f.areas[0].low.z=100;f.areas[0].high.z=-20;f.areas[0].low.y=200;
    Require(ReadNavigationV5(Bytes(Encode(f)),file,error),"reversed XY and independent corner heights preserved");
}
}
