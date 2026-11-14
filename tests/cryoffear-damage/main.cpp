// SPDX-License-Identifier: GPL-3.0-or-later
#include "native/guest/cof/scenario_damage.hpp"
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
    if (argc != 2) return 2;
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
    std::cout << "checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
