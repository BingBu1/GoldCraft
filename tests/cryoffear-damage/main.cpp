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
    if (argc != 5) return 2;
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
    Check(!FindFirearm(-1)&&!FindFirearm(895)&&!FindFirearm(903)&&!FindFirearm(Firearm::Count)&&
        !FindOriginalFirearm(0)&&!config.Find(Firearm::Count),"Invalid firearm identity accepted");
    for(unsigned bits=0;bits<=127;++bits)
        Check(ParseFirearmAdmission("1",std::to_string(bits)).weapons==bits,"Valid explicit firearm mask rejected");
    for(const char* bad:{"-1","+1"," 1","1 ","128","16x","4294967296","2147483648"})
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
        *config.Find(Firearm::M16)==ScenarioDamage{90,120},"Explicit M16 author balance rejected");
    const auto* m16=FindFirearm(Firearm::M16);
    Check(m16&&m16->hostId==902&&m16->originalId==9&&m16->clip==20&&m16->reserve==200&&
        m16->price==3100&&m16->ammunitionPrice==40&&m16->ammunitionCount==20&&
        !FindPistol(902)&&!OriginalPistolId(9),"M16 metadata or host family mismatch");
    for(const auto& bad:{Changed(v4Json,"\"m16\"","\"mp5\""),Changed(v4Json,"\"shot\": 90","\"shot\": -1"),
        Changed(v4Json,"\"shot\": 90","\"shot\": 90, \"penetration\": 5"),
        Changed(v4Json,"\"version\": 4","\"version\": 3"),Changed(v4Json,"\"version\": 4","\"version\": 5")})
    {
        Check(!config.Parse(bad,error),"Malformed v4 balance accepted");
        for(unsigned index=0;index<Firearms.size();++index)
            Check(config.Find(static_cast<Firearm>(index))!=nullptr,"Failed v4 reload lost a previous weapon");
        Check(*config.Find(Firearm::M16)==ScenarioDamage{90,120},"Failed v4 transaction changed M16 balance");
    }
    Check(config.Parse(v3Json,error)&&!config.Find(Firearm::M16)&&config.Find(Firearm::MP5),
        "Returning to v3 left stale M16 permission/data");
    Check(config.Parse(v2Json,error)&&!config.Find(Firearm::MP5)&&config.Find(Firearm::TMP),"Returning to v2 left stale MP5 permission/data");
    Check(config.Parse(json,error)&&!config.Find(Firearm::TMP)&&!config.Find(Firearm::MP5)&&config.Find(Pistol::VP70)->shot==80,
        "Returning to v1 left stale TMP permission/data");
    std::cout << "checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
