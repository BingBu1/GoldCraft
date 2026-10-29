// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <map>
#include <string>
#include <string_view>
#include <vector>
#include "../csol-scenario-map-session/fixtures.hpp"

namespace session_test
{
inline std::string Unhex(std::string_view value)
{
    auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    std::string out;
    for (std::size_t i = 0; i < value.size(); i += 2)
        out.push_back(static_cast<char>(digit(value[i]) * 16 + digit(value[i + 1])));
    return out;
}
inline std::map<std::string, std::string> Resources()
{
    std::vector<std::string> header(59), row(59, "0");
    header[0] = ";map_id"; header[1] = "map_name"; header[2] = "map_filename"; header[58] = "SubType";
    row[0] = "1"; row[1] = "fixture"; row[2] = "map_session_test"; row[58] = "1";
    std::string catalog;
    for (const auto& fields : {header, row})
    {
        for (std::size_t i = 0; i < fields.size(); ++i) { if (i) catalog += ','; catalog += fields[i]; }
        catalog += "\r\n";
    }
    return {{"resource/maplist.csv", catalog}, {"resource/buff/ClassMod.csv", "item,category,type,subtype,v1,v2,v3,v4,v5,v6,v7\r\n"},
        {"resource/buff/ClassModConfig.json", R"({"CategoryMaxSlots":[0,0,1,1,2,1,5,5,5]})"},
        {"resource/buff/ClassModPreset.csv", "ClassID,PresetID,slots\r\n"},
        {"maps/zbs_wave_map_session_test.cso", Unhex(fixtures::waves)},
        {"maps/zbs_map_session_test.cso", Unhex(fixtures::rounds)},
        {"maps/ZBS_ZT_map_session_test.cso", Unhex(fixtures::types)},
        {"maps/ZBS_TRG_map_session_test.cso", Unhex(fixtures::triggers)},
        // Explicit filesystem fixtures for the authored monster tables. The
        // controlled engine does not parse MDL bytes; these prove existence only.
        {"models/player/zombi_host/zombi_host.mdl", "fixture-zombi-host-model"},
        {"models/player/zombi_origin/zombi_origin.mdl", "fixture-zombi-origin-model"}};
}
}
