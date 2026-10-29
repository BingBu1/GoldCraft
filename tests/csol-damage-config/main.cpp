// SPDX-License-Identifier: GPL-3.0-or-later
#include "weapon_damage.h"
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace
{
unsigned checks = 0;
void Check(bool condition, const char* message)
{
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
std::string Encode(const rapidjson::Value& value)
{
    rapidjson::StringBuffer buffer;
    rapidjson::Writer writer(buffer);
    value.Accept(writer);
    return {buffer.GetString(), buffer.GetSize()};
}
}

int main(int argc, char** argv)
{
    try
    {
        Check(argc == 2, "configuration file argument required");
        std::ifstream file(argv[1], std::ios::binary);
        Check(bool(file), "cannot read sample configuration");
        const std::string json{std::istreambuf_iterator<char>(file), {}};
        CCSOLBeamDamageConfig config;
        std::string error;
        Check(!config.IsLoaded(), "unloaded configuration must be distinguishable");
        Check(config.Get(CSOLBeamVariant::Beamgun, CSOLDamageMode::Zombie, CSOLBeamDamage::ShootA) == 0,
              "unloaded configuration unexpectedly supplies damage");
        Check(config.Parse(json, error) && config.IsLoaded() && error.empty(), "sample configuration did not load");
        // Explicit expectations exercise mode/attack/variant indexing, not just
        // JSON round trips. The numeric values are base defaults, not final HP loss.
        constexpr float expected[3][11] = {
            {40,10,10,5,20,5,40,40,0,0,0},
            {850,600,11760,3000,3000,2000,16800,18000,100,75,6000},
            {3600,1800,19000,3000,3000,2000,78500,65000,250,75,16000}
        };
        for (unsigned variant=0; variant<2; ++variant)
            for (unsigned mode=0; mode<3; ++mode)
                for (unsigned attack=0; attack<11; ++attack)
                    Check(config.Get(static_cast<CSOLBeamVariant>(variant),static_cast<CSOLDamageMode>(mode),
                                     static_cast<CSOLBeamDamage>(attack)) == expected[mode][attack], "wrong base damage mapping");
        rapidjson::Document document;
        document.Parse(json.c_str());
        auto& allocator = document.GetAllocator();
        auto& normalOverrides = document["overrides"]["beamgun"];
        normalOverrides.AddMember("ShootADamageZombie", 0, allocator);
        auto& leOverrides = document["overrides"]["beamgunle"];
        leOverrides.AddMember("ShootADamageZombie", 123.5, allocator);
        leOverrides.AddMember("WingManAttack2DamageZombie", 7.25, allocator);
        const auto customized = Encode(document);
        Check(config.Parse(customized, error), "valid overrides rejected");
        Check(config.Get(CSOLBeamVariant::Beamgun,CSOLDamageMode::Zombie,CSOLBeamDamage::ShootA)==0, "zero override lost");
        Check(config.Get(CSOLBeamVariant::BeamgunLE,CSOLDamageMode::Zombie,CSOLBeamDamage::ShootA)==123.5f, "variant override lost");
        Check(config.Get(CSOLBeamVariant::BeamgunLE,CSOLDamageMode::Normal,CSOLBeamDamage::ShootA)==40, "override leaked into another mode");
        Check(config.Get(CSOLBeamVariant::Beamgun,CSOLDamageMode::Zombie,CSOLBeamDamage::WingManAttack2)==75, "override leaked into another weapon");
        Check(config.Get(CSOLBeamVariant::BeamgunLE,CSOLDamageMode::Zombie,CSOLBeamDamage::WingManAttack2)==7.25f, "fractional damage lost");

        auto reject = [&](const std::string& invalid)
        {
            const auto previous = config;
            Check(!config.Parse(invalid, error) && !error.empty(), "invalid configuration accepted");
            Check(config.IsLoaded(), "failed reload removed the valid configuration");
            for (unsigned v=0;v<2;++v) for(unsigned m=0;m<3;++m) for(unsigned a=0;a<11;++a)
                Check(config.Get(static_cast<CSOLBeamVariant>(v),static_cast<CSOLDamageMode>(m),static_cast<CSOLBeamDamage>(a)) ==
                      previous.Get(static_cast<CSOLBeamVariant>(v),static_cast<CSOLDamageMode>(m),static_cast<CSOLBeamDamage>(a)),
                      "failed reload partially changed active damage");
        };
        reject(""); reject("[]"); reject("{}"); reject(json+"garbage");
        reject(json+std::string(1,'\0')+"{}"); reject(std::string(65537,' '));
        reject("{\"version\":1,\"version\":1,\"defaults\":{}}");
        auto invalidField = [&](const char* value)
        {
            auto bad=customized;
            const std::string old="\"ShootADamage\":40";
            bad.replace(bad.find(old),old.size(),"\"ShootADamage\":"+std::string(value));
            reject(bad);
        };
        for(const auto* value : {"-1","true","null","[]","{}","\"40\"","1e100","1e9999","NaN"}) invalidField(value);
        auto mutate = [&](auto edit)
        {
            rapidjson::Document bad; bad.Parse(customized.c_str()); edit(bad); reject(Encode(bad));
        };
        mutate([](auto& d){d["version"].SetInt(2);});
        mutate([](auto& d){d.RemoveMember("defaults");});
        mutate([](auto& d){d["defaults"].RemoveMember("ShootB2DamageScenario");});
        mutate([](auto& d){d["defaults"].AddMember("ShootADamage",123,d.GetAllocator());});
        mutate([](auto& d){d["defaults"].AddMember("ShootADamge",123,d.GetAllocator());});
        mutate([](auto& d){d["overrides"]["beamgunle"].AddMember("ShootADamageZombie",555,d.GetAllocator());});
        mutate([](auto& d){d["overrides"].AddMember("other_weapon",rapidjson::Value(rapidjson::kObjectType),d.GetAllocator());});
        mutate([](auto& d){d["overrides"].SetArray();});
        mutate([](auto& d){d["overrides"]["beamgunle"].SetNull();});
        mutate([](auto& d){d.AddMember("defualts",0,d.GetAllocator());});
        mutate([](auto& d){rapidjson::Value n;n.SetString("ShootADamage\0hidden",19,d.GetAllocator());d["defaults"].AddMember(n,0,d.GetAllocator());});
        Check(config.Parse("\xEF\xBB\xBF"+json,error), "UTF-8 BOM rejected");
        Check(config.Get(CSOLBeamVariant::BeamgunLE,CSOLDamageMode::Zombie,CSOLBeamDamage::ShootA)==850, "successful reload retained stale override");
        document.Parse(json.c_str()); document.RemoveMember("overrides");
        Check(config.Parse(Encode(document),error), "optional overrides required unexpectedly");
        Check(config.Get(static_cast<CSOLBeamVariant>(-1),CSOLDamageMode::Normal,CSOLBeamDamage::ShootA)==0, "invalid variant was not bounded");
        Check(config.Get(CSOLBeamVariant::Beamgun,static_cast<CSOLDamageMode>(3),CSOLBeamDamage::ShootA)==0, "invalid mode was not bounded");
        Check(config.Get(CSOLBeamVariant::Beamgun,CSOLDamageMode::Normal,CSOLBeamDamage::Count)==0, "invalid attack was not bounded");
        CCSOLBeamDamageConfig empty;
        Check(!empty.Parse("{}",error) && !empty.IsLoaded(), "failed initial load marked ready");
        std::cout << "CSOL damage configuration: " << checks << " checks passed\n";
        return 0;
    }
    catch(const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
