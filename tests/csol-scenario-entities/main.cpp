// SPDX-License-Identifier: GPL-3.0-or-later
#include <array>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "precompiled.h"
#include "../../native/csol/server/scenario_spawn_points.h"

extern "C" void playerspawn(entvars_t*);
extern "C" void zombiespawn(entvars_t*);
using namespace csol::scenario;
namespace
{
int checks{}, failures{};
void Check(bool value, const char* label)
{
    ++checks;
    if (!value) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
}
struct Engine
{
    static Engine* active;
    std::map<const edict_t*, int> indices;
    std::vector<std::unique_ptr<edict_t>> edicts;
    int allocations{}, indexCalls{};
    Engine()
    {
        active = this; g_engfuncs = {};
        g_engfuncs.pfnPvAllocEntPrivateData = Allocate;
        g_engfuncs.pfnIndexOfEdict = Index;
    }
    ~Engine()
    {
        for (auto& edict : edicts)
        {
            auto* entity = static_cast<CBaseEntity*>(edict->pvPrivateData);
            if (entity) { delete entity->m_pEntity; std::free(entity); }
        }
        active = nullptr; g_engfuncs = {};
    }
    static void* Allocate(edict_t* edict, int32 size)
    {
        if (edict->pvPrivateData || size <= 0) throw std::runtime_error("unexpected private allocation");
        ++active->allocations; return edict->pvPrivateData = std::calloc(1, static_cast<std::size_t>(size));
    }
    static int Index(const edict_t* edict) { ++active->indexCalls; return active->indices.at(edict); }
    template<class T> T& Create(bool player, int index)
    {
        auto edict = std::make_unique<edict_t>(); edict->v.pContainingEntity = edict.get();
        indices[edict.get()] = index; auto* raw = edict.get(); edicts.push_back(std::move(edict));
        if (player) playerspawn(&raw->v); else zombiespawn(&raw->v);
        auto* entity = static_cast<T*>(raw->pvPrivateData);
        Check(entity && entity->pev == &raw->v && entity->m_pEntity && entity->m_pEntity->BaseEntity() == entity,
            "real LINK_ENTITY and GetClassPtr create correct private entity and CCSEntity wrapper");
        const auto priorAllocations = allocations;
        if (player) playerspawn(&raw->v); else zombiespawn(&raw->v);
        Check(raw->pvPrivateData == entity && allocations == priorAllocations, "repeated real exported constructor reuses private data");
        return *entity;
    }
};
Engine* Engine::active{};
void Key(CBaseEntity& entity, const char* name, const char* value, bool handled = true)
{
    KeyValueData data{nullptr, const_cast<char*>(name), const_cast<char*>(value), 777};
    entity.KeyValue(&data); Check(data.fHandled == (handled ? TRUE : FALSE), "production KeyValue handled flag is exact");
}
template<class T> void UnchangedTransform(T& entity)
{
    entity.pev->origin = Vector(10.25f, -22.5f, 4096.0f); entity.pev->angles = Vector(-90, 37.75f, 180);
    entity.pev->solid = 123; entity.pev->effects = 456; entity.pev->flags = 789;
    const auto before = entity.pev->origin, angles = entity.pev->angles;
    entity.Spawn(); entity.Activate();
    Check(entity.pev->origin == before && entity.pev->angles == angles && entity.pev->solid == 123 && entity.pev->effects == 456 && entity.pev->flags == 789,
        "Spawn and inherited Activate preserve transform and unrelated entvars");
}
void DefaultsAndKeys()
{
    Engine engine;
    auto& player = engine.Create<CCSOLPlayerSpawn>(true, 71);
    auto& monster = engine.Create<CCSOLMonsterSpawn>(false, 99);
    Check(player.m_iId == 0 && player.m_iTeam == 0, "player private constructor defaults id0 team0");
    Check(monster.m_iId == 0 && monster.m_iTeam == 3 && monster.m_iMaxSpawn == 0, "monster private constructor defaults id0 team3 max0");
    UnchangedTransform(player); UnchangedTransform(monster);
    Check(player.m_iTeam == 2 && monster.m_iTeam == 1, "Spawn applies only original player0->2 monster3->1 defaults");
    for (int team : {0, 1, 2, 3, -1, 255, 256, 2147483647, (-2147483647 - 1)})
    {
        auto value = std::to_string(team); Key(player, "team2", value.c_str()); Key(monster, "team2", value.c_str());
        player.Spawn(); monster.Spawn();
        Check(player.m_iTeam == (team == 0 ? 2 : team) && monster.m_iTeam == (team == 3 ? 1 : team), "explicit team including full-width values follows entity-specific defaults");
    }
    for (const auto& [text, number] : std::vector<std::pair<const char*, int>>{{"0",0},{"-1",-1},{"65535",65535},{"65536",65536},{"2147483647",2147483647},{"-2147483648",(-2147483647-1)},{"5.5",5},{" -10abc",-10},{"+7",7},{"",0},{" ",0},{"bad",0}})
    {
        Key(player, "id", text); Key(monster, "id", text); Key(monster, "maxspawn", text);
        Check(player.m_iId == number && monster.m_iId == number && monster.m_iMaxSpawn == number, "KeyValue matches in-range atoi and retains full32-bit fields");
    }
    const int id = player.m_iId, team = player.m_iTeam, maximum = monster.m_iMaxSpawn;
    for (const char* key : {"ID", "team", "Team2", "maxSpawn", "origin", "angles", "unknown"})
    {
        Key(player, key, "11", false); Key(monster, key, "11", false);
    }
    Key(player, "maxspawn", "77", false);
    Check(player.m_iId == id && player.m_iTeam == team && monster.m_iMaxSpawn == maximum, "unknown keys fall through real base KeyValue without modifying entity fields");
}
void Collections()
{
    Engine engine; SpawnPoints points;
    auto& player = engine.Create<CCSOLPlayerSpawn>(true, 501);
    Key(player, "team2", "2"); Key(player, "id", "-1"); player.Spawn();
    const auto before = engine.indexCalls;
    Check(!player.Collect(points) && points.Players().empty() && engine.indexCalls == before, "full int32 -1 player proxy skipped before snapshot/index lookup");
    Key(player, "id", "65535"); UnchangedTransform(player);
    Check(player.Collect(points), "65535 player is collected despite same narrowed key as -1");
    const auto* players = points.FindPlayerPoints(2, -1);
    Check(players && players->size() == 1 && (*players)[0].id == 65535 && (*players)[0].team == 2 && (*players)[0].entityIndex == 501, "player snapshot keeps full id team and original edict index");
    Check(players && (*players)[0].origin == std::array<float,3>{10.25f,-22.5f,4096} && (*players)[0].angles == std::array<float,3>{-90,37.75f,180}, "snapshot preserves origin and angles exactly");
    auto& next = engine.Create<CCSOLPlayerSpawn>(true, 74); Key(next, "id", "131071"); next.Spawn();
    Check(next.Collect(points), "oversized alias player collected");
    players = points.FindPlayerPoints(2, 65535);
    Check(players && players->size() == 2 && (*players)[0].entityIndex == 501 && (*players)[1].entityIndex == 74, "same-key player points append in traversal order without sorting edict indices");
    auto& monster = engine.Create<CCSOLMonsterSpawn>(false, 89); Key(monster,"id","-1"); Key(monster,"maxspawn","1"); monster.Spawn(); monster.Collect(points);
    Check(points.ConsumePointQuota(1,-1), "monster snapshot quota starts zero");
    auto& last = engine.Create<CCSOLMonsterSpawn>(false, 33); Key(last,"id","65535"); Key(last,"maxspawn","8"); UnchangedTransform(last); last.Collect(points);
    const auto* result = points.FindMonsterPoint(1,-1);
    Check(points.Monsters().size() == 1 && result && result->point.id == 65535 && result->point.entityIndex == 33 && result->maxSpawn == 8 && result->consumed == 0,
        "same-key monster replaces prior snapshot and resets quota retaining winning full identity");
    const auto monsterCount = points.Monsters().size(), playerCount = points.Players().size();
    player.Activate(); monster.Activate();
    Check(points.Monsters().size() == monsterCount && points.Players().size() == playerCount, "inherited Activate does not register or remove points");
}
class Reader
{
    std::ifstream file;
public:
    explicit Reader(const char* path) : file(path, std::ios::binary)
    {
        if (!file) throw std::runtime_error("cannot open independent point fixture");
    }
    std::uint32_t U32()
    {
        unsigned char bytes[4]; file.read(reinterpret_cast<char*>(bytes), 4);
        if (!file) throw std::runtime_error("truncated point fixture");
        return std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8) | (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
    }
    int I32() { return std::bit_cast<std::int32_t>(U32()); }
    float Float() { return std::bit_cast<float>(U32()); }
    std::string String()
    {
        std::string value(U32(), '\0'); file.read(value.data(), value.size());
        if (!file) throw std::runtime_error("truncated point fixture string"); return value;
    }
    bool Done() { return file.peek() == std::char_traits<char>::eof(); }
};
bool Same(const SpawnPoint& actual, const SpawnPoint& expected)
{
    return actual.entityIndex == expected.entityIndex && actual.id == expected.id && actual.team == expected.team &&
        actual.origin == expected.origin && actual.angles == expected.angles;
}
void Actual(const char* path)
{
    Reader input(path); Check(input.U32() == 0x544e5053u, "independent fixture SPNT signature");
    const auto count = input.U32(); Check(count == 184, "deadend actual184 records");
    Engine engine; SpawnPoints points;
    std::map<std::int32_t, MonsterPoint> expectedMonsters;
    std::map<std::int32_t, std::vector<SpawnPoint>> expectedPlayers;
    std::size_t playerCount{}, monsterCount{};
    for (std::uint32_t i = 0; i < count; ++i)
    {
        SpawnPoint expected; expected.entityIndex = input.U32(); const auto kind = input.U32();
        expected.id = input.I32(); expected.team = input.I32(); const int maximum = input.I32();
        for (auto& value : expected.origin) value = input.Float();
        for (auto& value : expected.angles) value = input.Float();
        const auto key = input.I32(); const auto keys = input.U32();
        CBaseEntity* entity = kind == 0 ? static_cast<CBaseEntity*>(&engine.Create<CCSOLPlayerSpawn>(true, expected.entityIndex))
            : static_cast<CBaseEntity*>(&engine.Create<CCSOLMonsterSpawn>(false, expected.entityIndex));
        entity->pev->origin = Vector(expected.origin[0], expected.origin[1], expected.origin[2]);
        entity->pev->angles = Vector(expected.angles[0], expected.angles[1], expected.angles[2]);
        for (std::uint32_t j = 0; j < keys; ++j)
        {
            const auto name = input.String(), value = input.String();
            const bool handled = name == "id" || name == "team2" || (kind == 1 && name == "maxspawn");
            Key(*entity, name.c_str(), value.c_str(), handled);
        }
        entity->Spawn(); entity->Activate();
        Check(entity->pev->origin == Vector(expected.origin[0], expected.origin[1], expected.origin[2]) &&
            entity->pev->angles == Vector(expected.angles[0], expected.angles[1], expected.angles[2]), "actual ordered KeyValue/Spawn/Activate preserve engine-parsed origin angles");
        if (kind == 0)
        {
            ++playerCount; auto& player = *static_cast<CCSOLPlayerSpawn*>(entity);
            Check(player.m_iId == expected.id && player.m_iTeam == expected.team, "actual player original KV and Spawn default fields match independent oracle");
            Check(player.Collect(points) == (expected.id != -1), "actual player proxy collection rule");
            if (expected.id != -1) expectedPlayers[key].push_back(expected);
        }
        else
        {
            ++monsterCount; auto& monster = *static_cast<CCSOLMonsterSpawn*>(entity);
            Check(monster.m_iId == expected.id && monster.m_iTeam == expected.team && monster.m_iMaxSpawn == maximum, "actual monster KV and Spawn defaults match independent oracle");
            monster.Collect(points); expectedMonsters[key] = {expected, maximum, 0};
        }
    }
    Check(input.Done(), "independent fixture fully consumed without trailing bytes");
    Check(playerCount == 19 && monsterCount == 165, "actual deadend19 players and165 monsters passed real entity methods");
    Check(points.Monsters().size() == 164 && points.Monsters().size() == expectedMonsters.size(), "actual monster164 distinct keys with overwrite");
    Check(points.Players().size() == 2 && points.Players().size() == expectedPlayers.size(), "actual player2 groups preserve all points");
    for (const auto& [key, expected] : expectedMonsters)
    {
        const auto found = points.Monsters().find(key);
        Check(found != points.Monsters().end(), "actual monster independent signed key present");
        if (found == points.Monsters().end()) continue;
        Check(Same(found->second.point, expected.point) && found->second.maxSpawn == expected.maxSpawn && found->second.consumed == 0,
            "actual monster full snapshot winner location orientation fields and mock edict identity match");
    }
    for (const auto& [key, expected] : expectedPlayers)
    {
        const auto found = points.Players().find(key);
        Check(found != points.Players().end(), "actual player independent signed key present");
        if (found == points.Players().end()) continue;
        Check(found->second.size() == expected.size(), "actual player duplicate count preserved");
        for (std::size_t i = 0; i < std::min(found->second.size(), expected.size()); ++i)
            Check(Same(found->second[i], expected[i]), "actual player complete snapshots preserve original traversal order");
    }
    const auto* duplicate = points.FindMonsterPoint(1, 30);
    Check(duplicate && duplicate->point.entityIndex == 202, "deadend duplicate monster id30 uses later BSP fixture entity202");
    std::cout << "actual deadend: 184 records,19 players/2 groups,165 monsters/164 groups; BSP indices used only as mock engine identities\n";
}
}
int main(int argc, char** argv)
{
    try
    {
        if (argc == 1) { DefaultsAndKeys(); Collections(); }
        else if (argc == 3 && std::string_view(argv[1]) == "--actual") Actual(argv[2]);
        else throw std::runtime_error("usage: scenario entities tests [--actual independent-points.bin]");
    }
    catch (const std::exception& error) { ++failures; std::cerr << "EXCEPTION: " << error.what() << '\n'; }
    std::cout << "entities checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
