// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../csol-scenario-monster-skills/trap_fixture.hpp"
#include "../../native/csol/scenario/core/scenario_round_hud.hpp"
#include "../../native/csol/scenario/monsters/elite_visual_resources.hpp"
#include "../../native/csol/character_skill_resources.hpp"
#include "../../native/csol/scenario/core/status_icons.hpp"
#include "../../native/csol/scenario/core/buy_zone_hint.hpp"
namespace oberon_chain
{
using sdk_test::Require;
std::map<std::string, std::string> BaseResources()
{
    auto result = trap_test::Resources(0);
    std::map<std::string, std::string> renamed;
    for (const auto& entry : result)
    {
        auto name = entry.first, bytes = entry.second;
        if (const auto p = name.find("map_session_test"); p != name.npos)
            name.replace(p, std::strlen("map_session_test"), "zs_deadend");
        if (name == "resource/maplist.csv")
        {
            const auto p = bytes.find("map_session_test"), row = bytes.find("\r\n1,fixture,");
            Require(p != bytes.npos && row != bytes.npos, "authored map metadata exists");
            bytes.replace(p, std::strlen("map_session_test"), "zs_deadend");
            bytes.replace(row + 2, 1, "98");
        }
        renamed.emplace(std::move(name), std::move(bytes));
    }
    result = std::move(renamed);
    const auto encode = difficulty_test::Encrypt;
    result["maps/zbs_zs_deadend.cso"] = encode(
        "metadata,0\r\nunused,0\r\nthreshold,100\r\nmoney,1\r\nexp,1\r\nRewardRound,0\r\n"
        "round,waveTypes,health,armor,speed,damage,range,interval,money,unused,music,playerSpawn\r\n"
        "0,1,0,0,0,0,0,0,0,0,0,1\r\n");
    // Keep the round open without wave-generated monsters during this short
    // test. The ordinary seed still traverses the actual factory and rules.
    result["maps/zbs_wave_zs_deadend.cso"] = encode(
        "waveID,spawnPoint,startTime,endTime,intervalTime,zombi,zombiRatio,eventmessage\r\n"
        "1,1,100000,100001,1,1,1,\r\n");
    result["maps/ZBS_ZT_zs_deadend.cso"] = encode(
        "id,appearance,attackDamage,attackRange,attackInterval,health,armor,speed,bonusMoney,priority,skill,color,Killtype,EventClass,propensity,clearpoint,dropItem,exp\r\n"
        "1,zombi_origin,1,1,1,100,0,100,1,0,0,255 255 255,0,0,0,0,0,1\r\n");
    std::string row;
    for (int i = 0; i < 64; ++i) { if (i) row += ','; row += '1'; }
    result["maps/ZBS_Difficulty_Default.cso"] = encode("header\r\n" + row + "\r\n" + row + "\r\n");
    auto& json = result.at("maps/ZBS.json");
    Require(!json.empty() && json.back() == '}', "authored equipment JSON root");
    json.pop_back();
    json += R"(,"ZBS_Elite":{"Properties":[{"difficulty":1,"attackDamage":1,"attackRange":30,"attackInterval":1,"maxHealth":1000,"maxArmor":0,"maxSpeed":100},{"difficulty":2,"attackDamage":1,"attackRange":30,"attackInterval":1,"maxHealth":1000,"maxArmor":0,"maxSpeed":100}],"Colors":[255,255,255],"Skills":[)";
    for (int i = 0; i < 14; ++i)
    {
        if (i) json += ',';
        json += i == 1 ? R"({"Params":{"Num":2}})" : R"({"Params":{}})";
    }
    json += "]}}";
    result["models/monster/zbs_elite_01.mdl"] = "fixture-model";
    for (const auto& resource : csol::scenario_hud::Resources) result[resource.path] = "fixture-image";
    for (std::size_t type = 0; type < csol::status::ImageCount; ++type)
        result[std::string(csol::status::ImagePrefix) + std::to_string(type) + ".tga"] = "fixture-image";
    for (const auto* image : csol::buy_hint::Images) result[image] = "fixture-image";
    result[csol::buy_hint::Font] = "fixture-font";
    result[csol::scenario_hud::MessagePath] = encode("id,text,image,sound\r\n");
    result[csol::scenario_hud::LocalizationPath] = "fixture-localization";
    result[csol::scenario_hud::FontPath] = "fixture-font";
    result[csol::smoke::EventPath] = "fixture-event";
    for (const auto* path : csol::elitefx::Models) result[path] = "fixture-model";
    for (const auto* path : csol::elitefx::Sounds) result[path] = "fixture-sound";
    for (const auto* path : csol::skillfx::Models) result[path] = "fixture-model";
    for (const auto* path : csol::skillfx::Dependencies) result[path] = "fixture-dependency";
    for (const auto* path : csol::skillfx::Sounds) result[path] = "fixture-sound";
    for (const auto* path : csol::skillfx::ClientResources) result[path] = "fixture-image";
    result["resource/ModeEvent/modeevent.csv"] = ";id,name,event\r\n";
    result["sound/sentences.json"] = R"({"MRAD_BUFFCLASS_SKILL":{}})";
    std::array<std::string, 35> emotion;
    emotion[0] = "model_id"; emotion[8] = "wa05"; emotion[18] = "seq05";
    emotion[24] = "frame01"; emotion[34] = "vmodelIndex";
    std::string actionHeader;
    for (std::size_t i = 0; i < emotion.size(); ++i)
    { if (i) actionHeader += ','; actionHeader += emotion[i]; }
    result["classes2/emotion.csv"] = actionHeader + "\r\n";
    return result;
}

}
