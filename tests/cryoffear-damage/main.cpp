// SPDX-License-Identifier: GPL-3.0-or-later
#include "native/guest/cof/scenario_damage.hpp"
#include "native/guest/cof/catalog.hpp"
#include "native/guest/cof/admission.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace
{
int failures{}, checks{};
void Check(bool value, const char* message)
{
    ++checks;
    if (!value) { ++failures; std::cerr << message << '\n'; }
}
std::string Changed(std::string value, const std::string& from, const std::string& to)
{
    const auto at = value.find(from);
    if (at == value.npos) throw std::runtime_error("Bad test mutation");
    value.replace(at, from.size(), to);
    return value;
}
}

int main(int argc, char** argv)
{
    using namespace guest::cof;
    if (argc != 8) return 2;
    std::ifstream file(argv[1], std::ios::binary);
    if (!file) return 3;
    const std::string json{std::istreambuf_iterator<char>(file), {}};
    ScenarioDamageConfig config;
    std::string error;
    Check(!config.Loaded() && !config.Find(Pistol::Glock), "Uninitialized damage became available");
    Check(!config.Parse("{}", error) && !error.empty() && !config.Loaded(), "Initial failure became ready");
    Check(config.Parse(json, error) && error.empty() && config.Loaded(), "Shipped balance config rejected");
    constexpr ScenarioDamage expected[]{{110, 120}, {165, 120}, {100, 120, 5}, {80, 120}};
    for (unsigned i = 0; i < std::size(expected); ++i)
        Check(config.Find(static_cast<Pistol>(i)) && *config.Find(static_cast<Pistol>(i)) == expected[i],
              "Shipped weapon balance or profile identity changed");
    Check(!config.Find(Pistol::Count) && !config.Find(static_cast<Pistol>(255)), "Invalid profile was accepted");

    const std::string malformed[]{
        "", "[1]", std::string(65537, ' '), std::string(20000, '[') + "0" + std::string(20000, ']'),
        Changed(json, "\"version\": 1", "\"version\": 2"),
        Changed(json, "\"version\": 1", "\"version\": true"),
        Changed(json, "\"glock\"", "\"unknown\""),
        Changed(json, "\"p345\"", "\"glock\""),
        Changed(json, "\"melee\": 120", "\"shot\": 120"),
        Changed(json, "\"shot\": 110", "\"shot\": -1"),
        Changed(json, "\"shot\": 110", "\"shot\": 1000001"),
        Changed(json, "\"shot\": 110", "\"shot\": 1e309"),
        Changed(json, "\"shot\": 110", "\"shot\": \"110\""),
        Changed(json, "\"shot\": 110", "\"shot\": null"),
        Changed(json, "\"shot\": 110", "\"shot\": 0, \"extra\": 1"),
        Changed(json, "\"penetration\": 5", "\"penetration\": -1"),
        Changed(json, "\"penetration\": 5", "\"penetration\": 33"),
        Changed(json, "\"penetration\": 5", "\"penetration\": 5.5"),
        Changed(json, "\"penetration\": 5", "\"penetration\": true"),
        Changed(json, "\"penetration\": 5", "\"extra\": 5"),
        Changed(json, "\"shot\": 110", "\"shot\": 110, \"penetration\": 5"),
        Changed(json, "\"pistols\"", "\"extra\": 1, \"pistols\""),
        Changed(json, "\"glock\"", std::string("\"glock") + char(0xff) + "\""),
        json + " trailing", json + std::string(1, '\0') + "trailing"
    };
    for (const auto& bad : malformed)
    {
        Check(!config.Parse(bad, error) && !error.empty(), "Malformed configuration was accepted");
        for (unsigned i = 0; i < std::size(expected); ++i)
            Check(config.Find(static_cast<Pistol>(i)) && *config.Find(static_cast<Pistol>(i)) == expected[i],
                  "Failed reload partially changed active damage");
    }

    const auto reloaded = Changed(json, "\"shot\": 80", "\"shot\": 0.5");
    Check(config.Parse(reloaded, error) && error.empty() && config.Find(Pistol::VP70)->shot == .5f,
          "Valid fractional reload did not commit");
    const auto boundaries = Changed(Changed(json, "\"shot\": 110", "\"shot\": 0"),
                                    "\"melee\": 120", "\"melee\": 1000000");
    Check(config.Parse(boundaries, error) && config.Find(Pistol::Glock)->shot == 0 &&
          config.Find(Pistol::Glock)->melee == 1'000'000, "Allowed balance limits were rejected");
    const auto legacy = Changed(json, ", \"penetration\": 5", "");
    Check(config.Parse(legacy,error) && config.Find(Pistol::Revolver)->penetration==0 &&
        config.Find(Pistol::Revolver)->shot==100,"Legacy config must retain original CoF ballistics");
    for(int count:{0,5,32})
        Check(config.Parse(Changed(json,"\"penetration\": 5","\"penetration\": "+std::to_string(count)),error)&&
            config.Find(Pistol::Revolver)->penetration==count,"Explicit host penetration boundary rejected");
    Check(config.Parse(json,error)&&!config.Find(Firearm::TMP),"Legacy config invented TMP readiness");
    auto version2=Changed(Changed(json,"\"version\": 1","\"version\": 2"),"\"pistols\"","\"firearms\"");
    version2=Changed(version2,"\"vp70\": { \"shot\": 80, \"melee\": 120 }",
        "\"vp70\": { \"shot\": 80, \"melee\": 120 },\n    \"tmp\": { \"shot\": 55, \"melee\": 120 }");
    Check(config.Parse(version2,error)&&config.Find(Firearm::TMP)&&
        *config.Find(Firearm::TMP)==ScenarioDamage{55,120},"Explicit TMP balance rejected");
    std::ifstream shippedV2(argv[2], std::ios::binary);
    if (!shippedV2) return 3;
    const std::string v2Json{std::istreambuf_iterator<char>(shippedV2), {}};
    Check(config.Parse(v2Json,error)&&error.empty(),"Shipped five-firearm configuration rejected");
    for(unsigned i=0;i<std::size(expected);++i)
        Check(config.Find(static_cast<Pistol>(i))&&*config.Find(static_cast<Pistol>(i))==expected[i],
            "Shipped v2 changed the four accepted balances");
    Check(config.Find(Firearm::TMP)&&*config.Find(Firearm::TMP)==ScenarioDamage{55,120},
        "Shipped v2 lost explicit TMP balance");
    for(const auto& pistol:Pistols)
        Check(FindFirearm(FirearmIdentity(pistol.identity))&&
            FindFirearm(pistol.hostId)->originalId==pistol.originalId&&
            config.Find(FirearmIdentity(pistol.identity))&&
            *config.Find(FirearmIdentity(pistol.identity))==*config.Find(pistol.identity),
            "Common firearm view changed an existing pistol");
    for(const auto& firearm:Firearms)
    {
        Check(FindFirearm(firearm.hostId)==&firearm&&FindOriginalFirearm(firearm.originalId)==&firearm&&
            OriginalFirearmId(firearm.originalId),"Generic lookup identity mismatch");
        Check(FirearmCapability(firearm.identity)==1u<<unsigned(firearm.identity),"Firearm bit mismatch");
    }
    const auto* tmp=FindFirearm(Firearm::TMP);
    Check(tmp&&tmp->hostId==900&&tmp->originalId==14&&tmp->clip==30&&tmp->reserve==300&&
        tmp->price==2000&&tmp->ammunitionPrice==30&&tmp->ammunitionCount==30&&
        tmp->displayName=="客串武器·MP9"&&!FindPistol(900)&&!OriginalPistolId(14),
        "TMP was misplaced in pistol family or lost authored metadata");
    Check(!FindFirearm(-1)&&!FindFirearm(895)&&!FindFirearm(906)&&!FindFirearm(Firearm::Count)&&
        !FindOriginalFirearm(0)&&!config.Find(Firearm::Count),"Invalid firearm identity accepted");
    for(unsigned bits=0;bits<=511;++bits)
        Check(ParseFirearmAdmission("1",std::to_string(bits)).weapons==bits,"Valid explicit firearm mask rejected");
    for (unsigned mask=0; mask<=LegacyFirearmMask; ++mask)
    {
        const auto legacy=std::to_string(mask);
        Check(ParseFirearmAdmission("1",legacy,"1").weapons==(mask|FirearmCapability(Firearm::Famas)),
            "FAMAS extension changed a legacy capability");
        Check(ParseFirearmAdmission("1",legacy,"512").weapons==mask,
            "Malformed FAMAS extension disabled a legacy gun");
    }
    for(const char* bad:{"-1","+1"," 1","1 ","512","16x","4294967296","2147483648"})
        Check(ParseFirearmAdmission("1",bad).weapons==0,"Malformed firearm mask accepted");
    Check(ParseFirearmAdmission("1","").weapons==1&&ParseFirearmAdmission("0","31").weapons==0&&
        ParsePistolAdmission("1","16").weapons==0,"Legacy protocol semantics changed");
    const std::string badV2[]{
        Changed(version2,"\"firearms\"","\"pistols\""),Changed(version2,"\"tmp\"","\"vp70\""),
        Changed(version2,"\"tmp\"","\"unknown\""),Changed(version2,"\"shot\": 55","\"shot\": -1"),
        Changed(version2,"\"shot\": 55","\"shot\": 55, \"penetration\": 5"),
        Changed(version2,"\"shot\": 55","\"shot\": 55, \"penetration\": 0"),
        Changed(version2,"\"version\": 2","\"version\": 3"),
        Changed(version2,"\"version\": 2","\"version\": 1"),version2+" trailing"};
    for(const auto& bad:badV2)
    {
        Check(!config.Parse(bad,error)&&config.Find(Firearm::TMP)&&
            *config.Find(Firearm::TMP)==ScenarioDamage{55,120},"Failed v2 reload partially committed");
        for(unsigned i=0;i<std::size(expected);++i)
            Check(config.Find(static_cast<Pistol>(i))&&*config.Find(static_cast<Pistol>(i))==expected[i],
                "Failed v2 reload changed legacy pistol values");
    }
    std::ifstream shippedV3(argv[3],std::ios::binary);if(!shippedV3)return 3;
    const std::string v3Json{std::istreambuf_iterator<char>(shippedV3),{}};
    Check(config.Parse(v3Json,error)&&config.Find(Firearm::MP5)&&
        *config.Find(Firearm::MP5)==ScenarioDamage{60,120},"Explicit MP5 author balance rejected");
    const auto* mp5=FindFirearm(Firearm::MP5);
    Check(mp5&&mp5->hostId==901&&mp5->originalId==27&&mp5->clip==30&&mp5->reserve==300&&
        mp5->displayName=="客串武器·MP5A3"&&!FindPistol(901)&&!OriginalPistolId(27),"MP5 incorrectly became a pistol or borrowed another identity");
    for(const auto& bad:{Changed(v3Json,"\"mp5\"","\"tmp\""),Changed(v3Json,"\"shot\": 60","\"shot\": -1"),
        Changed(v3Json,"\"shot\": 60","\"shot\": 60, \"penetration\": 5"),Changed(v3Json,"\"version\": 3","\"version\": 2")})
        Check(!config.Parse(bad,error)&&config.Find(Firearm::MP5)&&config.Find(Firearm::MP5)->shot==60,
            "Failed MP5 balance transaction changed the accepted configuration");
    Check(!config.Find(Firearm::M16),"Six-gun v3 invented M16 permission");
    std::ifstream shippedV4(argv[4],std::ios::binary);if(!shippedV4)return 3;
    const std::string v4Json{std::istreambuf_iterator<char>(shippedV4),{}};
    Check(config.Parse(v4Json,error)&&config.Find(Firearm::M16)&&
        *config.Find(Firearm::M16)==ScenarioDamage{90,120,3},"M16 minimum CS penetration missing");
    const auto* m16=FindFirearm(Firearm::M16);
    Check(m16&&m16->hostId==902&&m16->originalId==9&&m16->clip==20&&m16->reserve==200&&
        m16->price==3100&&m16->ammunitionPrice==40&&m16->ammunitionCount==20&&
        !FindPistol(902)&&!OriginalPistolId(9),"M16 metadata or host family mismatch");
    for(const auto& bad:{Changed(v4Json,"\"m16\"","\"mp5\""),Changed(v4Json,"\"shot\": 90","\"shot\": -1"),
        Changed(v4Json,"\"shot\": 90","\"shot\": 90, \"penetration\": 33"),
        Changed(v4Json,"\"version\": 4","\"version\": 3"),Changed(v4Json,"\"version\": 4","\"version\": 5")})
    {
        Check(!config.Parse(bad,error),"Malformed v4 balance accepted");
        for(unsigned index=0;index<static_cast<unsigned>(Firearm::G43);++index)
            Check(config.Find(static_cast<Firearm>(index))!=nullptr,"Failed v4 reload lost a previous weapon");
        Check(*config.Find(Firearm::M16)==ScenarioDamage{90,120,3},"Failed v4 transaction changed M16 balance");
    }
    Check(!config.Find(Firearm::G43),"Seven-gun v4 invented G43 permission");
    std::ifstream shippedV5(argv[5],std::ios::binary);if(!shippedV5)return 3;
    const std::string v5Json{std::istreambuf_iterator<char>(shippedV5),{}};
    Check(config.Parse(v5Json,error)&&config.Find(Firearm::G43)&&
        *config.Find(Firearm::G43)==ScenarioDamage{180,120,3},"G43 minimum CS penetration missing");
    const auto* g43=FindFirearm(Firearm::G43);
    Check(g43&&g43->hostId==903&&g43->originalId==26&&g43->clip==10&&g43->reserve==100&&
        g43->price==3500&&g43->ammunitionPrice==50&&g43->ammunitionCount==10&&!FindPistol(903)&&!OriginalPistolId(26),
        "G43 metadata or family mismatch");
    for(const auto& bad:{Changed(v5Json,"\"g43\"","\"m16\""),Changed(v5Json,"\"shot\": 180","\"shot\": -1"),
        Changed(v5Json,"\"shot\": 180","\"shot\": 180, \"penetration\": 33"),
        Changed(v5Json,"\"version\": 5","\"version\": 4"),Changed(v5Json,"\"version\": 5","\"version\": 6")})
    {
        Check(!config.Parse(bad,error),"Malformed v5 balance accepted");
        for(unsigned i=0;i<unsigned(Firearm::Shotgun);++i)
            Check(config.Find(static_cast<Firearm>(i))!=nullptr,"Failed v5 lost existing permissions");
        Check(config.Find(Firearm::G43)->shot==180,"Failed v5 changed G43 damage");
    }
    for(const auto weapon:{Firearm::M16,Firearm::G43})
    {
        const auto marker=weapon==Firearm::M16?"\"shot\": 90":"\"shot\": 180";
        for(int penetration:{0,1,2,3,5,32})
        {
            const auto configured=Changed(v5Json,marker,std::string(marker)+", \"penetration\": "+std::to_string(penetration));
            Check(config.Parse(configured,error)&&config.Find(weapon)->penetration==(penetration<3?3:penetration),
                "Rifle minimum or higher explicit penetration lost");
            for(const auto& value:{"-1","33","3.5","true","null","\"3\""})
            {
                const auto bad=Changed(v5Json,marker,std::string(marker)+", \"penetration\": "+value);
                Check(!config.Parse(bad,error)&&config.Find(weapon)->penetration==(penetration<3?3:penetration),
                    "Failed rifle transaction lost the previous accepted penetration");
            }
            Check(config.Find(Firearm::Revolver)->penetration==5,"Rifle policy changed Revolver penetration");
        }
    }
    Check(!config.Find(Firearm::Shotgun),"Eight-gun v5 invented shotgun permission");
    std::ifstream shippedV6(argv[6],std::ios::binary);if(!shippedV6)return 3;
    const std::string v6Json{std::istreambuf_iterator<char>(shippedV6),{}};
    Check(config.Parse(v6Json,error)&&config.Find(Firearm::Shotgun)&&
        *config.Find(Firearm::Shotgun)==ScenarioDamage{35,25},"Explicit nine-gun shotgun balance rejected");
    const auto* shotgun=FindFirearm(Firearm::Shotgun);
    Check(shotgun&&shotgun->hostId==904&&shotgun->originalId==4&&shotgun->clip==5&&shotgun->reserve==50&&
        shotgun->price==3000&&shotgun->ammunitionPrice==65&&shotgun->ammunitionCount==5&&
        shotgun->displayName=="客串武器·Remington 870"&&!FindPistol(904)&&!OriginalPistolId(4),
        "870 host metadata or family mismatch");
    const std::string badV6[]{Changed(v6Json,"\"shotgun\"","\"g43\""),
        Changed(v6Json,"\"shot\": 35","\"shot\": -1"),
        Changed(v6Json,"\"shot\": 35","\"shot\": 35, \"penetration\": 3"),
        Changed(v6Json,"\"version\": 6","\"version\": 5"),
        Changed(v6Json,"\"version\": 6","\"version\": 7")};
    for(const auto& bad:badV6)
    {
        Check(!config.Parse(bad,error),"Malformed nine-gun balance accepted");
        for(const auto& definition:Firearms)Check((config.Find(definition.identity)!=nullptr)==(definition.identity!=Firearm::Famas),"Failed v6 changed active permissions");
        Check(*config.Find(Firearm::Shotgun)==ScenarioDamage{35,25},"Failed v6 changed accepted shotgun damage");
    }
    Check(!config.Find(Firearm::Famas), "Nine-gun profile enabled FAMAS");
    std::ifstream shippedV7(argv[7],std::ios::binary);
    if (!shippedV7) return 3;
    const std::string v7Json{std::istreambuf_iterator<char>(shippedV7),{}};
    Check(config.Parse(v7Json,error) && config.Find(Firearm::Famas) &&
        *config.Find(Firearm::Famas)==ScenarioDamage{80,120,3}, "Shipped ten-gun balance rejected");
    Check(*config.Find(Firearm::Revolver)==ScenarioDamage{550,120,5} &&
        *config.Find(Firearm::G43)==ScenarioDamage{800,120,3} &&
        *config.Find(Firearm::M16)==ScenarioDamage{100,120,3} &&
        *config.Find(Firearm::Shotgun)==ScenarioDamage{45,25}, "Current scenario firearm damage");
    const auto* famas=FindFirearm(Firearm::Famas);
    Check(famas && famas->hostId==905 && famas->originalId==23 && famas->clip==35 &&
        famas->reserve==150 && famas->price==3200 && famas->ammunitionPrice==60 && famas->ammunitionCount==30,
        "FAMAS metadata or ammunition purchase changed");
    for (const auto& bad : {Changed(v7Json,"\"famas\"","\"m16\""), Changed(v7Json,"\"shot\": 80","\"shot\": -1")})
    {
        Check(!config.Parse(bad,error),"Malformed ten-gun balance accepted");
        for (const auto& firearm : Firearms)
            Check(config.Find(firearm.identity)!=nullptr,"Failed v7 reload changed active permissions");
    }
    Check(config.Parse(v6Json,error) && !config.Find(Firearm::Famas) && config.Find(Firearm::Shotgun),
        "Retracting v7 left stale FAMAS permission or removed the 870");
    Check(config.Parse(v5Json,error)&&!config.Find(Firearm::Shotgun)&&config.Find(Firearm::G43),
        "Returning to v5 left stale shotgun data or changed eight-gun compatibility");
    Check(config.Parse(v4Json,error)&&!config.Find(Firearm::G43)&&config.Find(Firearm::M16)&&
        config.Find(Firearm::M16)->penetration==3,
        "Returning to v4 left stale G43 permission/data");
    Check(config.Parse(v3Json,error)&&!config.Find(Firearm::M16)&&config.Find(Firearm::MP5),
        "Returning to v3 left stale M16 permission/data");
    Check(config.Parse(v2Json,error)&&!config.Find(Firearm::MP5)&&config.Find(Firearm::TMP),"Returning to v2 left stale MP5 permission/data");
    Check(config.Parse(json,error)&&!config.Find(Firearm::TMP)&&!config.Find(Firearm::MP5)&&config.Find(Pistol::VP70)->shot==80,
        "Returning to v1 left stale TMP permission/data");
    std::cout << "checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
