// SPDX-License-Identifier: GPL-3.0-or-later
// Actual ReGameDLL entity export and methods over a controlled engine.
#include "../csol-scenario-rules-session/sdk_engine.hpp"
#include "../../native/csol/server/scenario/core/scenario_boss_room.h"

#include <array>
#include <bit>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>

extern "C" void boss_room(entvars_t*);

namespace
{
using namespace sdk_test;
unsigned behaviorChecks{};
unsigned modelCalls{};
unsigned keyCalls{};
unsigned roomFinds{};

void Expect(bool value, const char* message)
{
    ++behaviorChecks;
    Check(value, message);
}

CCSOLBossRoom* Create()
{
    auto* edict = Engine::Create(Engine::String("boss_room"));
    auto* entity = dynamic_cast<CCSOLBossRoom*>(CBaseEntity::Instance(edict));
    Require(entity != nullptr, "boss_room export constructs its actual SDK class");
    return entity;
}

void Key(CCSOLBossRoom& room, const char* name, const char* value, int expected, bool handled)
{
    KeyValueData data{const_cast<char*>("boss_room"), const_cast<char*>(name), const_cast<char*>(value), 17};
    room.KeyValue(&data);
    std::cout << "ROOM_KEY " << keyCalls++ << ' ' << room.RoomId() << ' ' << data.fHandled << '\n';
    Expect(room.RoomId() == expected, "original decimal id assignment or preservation");
    Expect(data.fHandled == (handled ? TRUE : FALSE), "original handled flag for exact id key");
}

void Entity()
{
    // Keep the full controlled edict/string pools off the Windows thread stack.
    auto storage = std::make_unique<Engine>();
    auto& engine = *storage;
    auto* room = Create();
    Expect(room->RoomId() == 0, "omitted room id defaults to zero in the real export");
    for (const auto& [text, value] : std::array<std::pair<const char*, int>, 9>{{
        {"23", 23}, {"-1", -1}, {"", 0}, {"unused", 0}, {"  +17tail", 17},
        {"-2147483648", -2147483647 - 1}, {"2147483647", 2147483647}, {"0x20", 0}, {"0", 0}}})
        Key(*room, "id", text, value, true);
    Key(*room, "id", "37", 37, true);
    for (const auto* key : {"ID", "id ", "target", "model", "origin"})
        Key(*room, key, "999", 37, false);

    room->pev->movetype = MOVETYPE_FLY;
    room->pev->solid = SOLID_BBOX;
    room->pev->effects = 0x21;
    room->pev->origin = Vector(100, 200, 300);
    room->pev->model = Engine::String("*90");
    modelCalls = 0;
    g_engfuncs.pfnSetModel = +[](edict_t* edict, const char* model)
    {
        ++modelCalls;
        Expect(edict->v.movetype == MOVETYPE_NONE && edict->v.solid == SOLID_NOT,
            "movement and solidity change before SetModel");
        Expect(edict->v.effects == 0x21, "invisibility is applied after SetModel");
        Expect(std::string_view(model) == "*90", "original brush model reaches the engine unchanged");
        // Controlled engine output, using the actual Nightmare brush bounds.
        edict->v.absmin = Vector(-34, -590, 240);
        edict->v.absmax = Vector(958, 590, 496);
        edict->v.modelindex = 91;
    };
    room->Spawn();
    Expect(modelCalls == 1, "Spawn delegates one SetModel call without precache or traces");
    Expect(room->RoomId() == 37, "Spawn preserves a parsed room id");
    Expect(room->pev->effects == 0xa1, "Spawn ORs EF_NODRAW and preserves other effect bits");
    Expect(room->pev->origin == Vector(100, 200, 300), "Spawn preserves the authored origin");
    Expect(room->pev->modelindex == 91 && room->pev->absmin == Vector(-34, -590, 240) &&
        room->pev->absmax == Vector(958, 590, 496), "Spawn retains the engine's brush bounds and model index");
    std::cout << "ROOM_SPAWN " << room->pev->movetype << ' ' << room->pev->solid << ' '
        << room->pev->effects << ' ' << room->pev->modelindex << ' ' << room->RoomId();
    for (const auto& value : {room->pev->origin, room->pev->absmin, room->pev->absmax})
        for (unsigned axis = 0; axis != 3; ++axis)
            std::cout << ' ' << std::bit_cast<std::uint32_t>(value[axis]);
    std::cout << '\n';

    auto* fresh = Create();
    Expect(fresh->RoomId() == 0, "each new room has its own default id");
    Engine::CheckGuards(room->edict());
    Engine::CheckGuards(fresh->edict());
}

void Retirement()
{
    auto storage = std::make_unique<Engine>();
    auto& engine = *storage;
    auto* room = Create();
    const EHANDLE old(room);
    const int slot = Engine::Index(room->edict());
    room->pev->model = Engine::String("*90");
    g_engfuncs.pfnSetModel = +[](edict_t* edict, const char*)
    {
        const int slot = Engine::Index(edict);
        Engine::Remove(edict);
        auto* replacement = Engine::self->Reserve(slot, "boss_room");
        boss_room(&replacement->v);
        replacement->v.effects = 0x10;
    };
    room->Spawn();
    auto* replacement = dynamic_cast<CCSOLBossRoom*>(CBaseEntity::Instance(Engine::At(slot)));
    Expect(!old.Get<CBaseEntity>() && replacement, "SetModel callback can retire and reuse the same edict");
    Expect(replacement && replacement->pev->effects == 0x10,
        "retired Spawn does not write EF_NODRAW into the replacement");
    Expect(replacement && replacement->RoomId() == 0, "replacement retains its own default id");
    Engine::CheckGuards(replacement->edict());
}

void RoomData(CCSOLBossRoom& room)
{
    std::cout << "ROOM_DATA " << Engine::Index(room.edict()) << ' ' << room.RoomId();
    for (const auto& value : {room.pev->absmin, room.pev->absmax})
        for (unsigned axis = 0; axis != 3; ++axis)
            std::cout << ' ' << std::bit_cast<std::uint32_t>(value[axis]);
    std::cout << '\n';
}

CCSOLBossRoom* Room(int id, const Vector& lo, const Vector& hi)
{
    auto* room = Create();
    const auto text = std::to_string(id);
    KeyValueData key{const_cast<char*>("boss_room"), const_cast<char*>("id"), const_cast<char*>(text.c_str()), 0};
    room->KeyValue(&key);
    room->pev->absmin = lo;
    room->pev->absmax = hi;
    RoomData(*room);
    return room;
}

void Reset(CCSOLBossRoomCache& cache, const std::shared_ptr<CSOLScenarioEntityAccess>& access)
{
    Require(cache.ControllerConstructed(access), "actual live controller construction binds its room cache");
    std::cout << "ROOM_RESET\n";
}

CSOLBossRoomResult Query(CCSOLBossRoomCache& cache, int id, Vector& lo, Vector& hi)
{
    const Vector beforeLo = lo, beforeHi = hi;
    const unsigned finds = roomFinds;
    const auto result = cache.Bounds(id, lo, hi);
    std::cout << "ROOM_QUERY " << id << ' ' << static_cast<int>(result) << ' ' << roomFinds - finds;
    for (const auto& value : {beforeLo, beforeHi, lo, hi})
        for (unsigned axis = 0; axis != 3; ++axis)
            std::cout << ' ' << std::bit_cast<std::uint32_t>(value[axis]);
    std::cout << '\n';
    return result;
}

void Cache()
{
    auto storage = std::make_unique<Engine>();
    auto& engine = *storage;
    auto access = std::make_shared<CSOLScenarioEntityAccess>();
    CCSOLBossRoomCache cache;
    Vector lo(11.25f, -22.5f, 33.75f), hi(-44.5f, 55.25f, -66.0f);
    Expect(cache.Bounds(0, lo, hi) == CSOLBossRoomResult::Unavailable,
        "cache cannot query before a real controller binds its map access");
    engine.onFind = [](edict_t*, const char* field, const char* value)
    {
        ++roomFinds;
        Expect(std::strcmp(field, "classname") == 0 && std::strcmp(value, "boss_room") == 0,
            "room lookup uses the original classname field and value");
    };
    Reset(cache, access);
    const Vector initialLo = lo, initialHi = hi;
    Expect(Query(cache, 0, lo, hi) == CSOLBossRoomResult::Missing && lo == initialLo && hi == initialHi,
        "missing room preserves both caller output vectors");
    auto* first = Room(2, Vector(1, 2, 3), Vector(4, 5, 6));
    auto* match = Room(0, Vector(-34, -590, 240), Vector(958, 590, 496));
    Room(0, Vector(100, 200, 300), Vector(400, 500, 600));
    Expect(Query(cache, 0, lo, hi) == CSOLBossRoomResult::Found &&
        lo == match->pev->absmin && hi == match->pev->absmax, "first matching id supplies the engine bounds");
    const Vector cachedLo = lo, cachedHi = hi;
    match->pev->absmin = Vector(700, 800, 900);
    match->pev->absmax = Vector(1700, 1800, 1900);
    RoomData(*match);
    auto before = roomFinds;
    Expect(Query(cache, 0, lo, hi) == CSOLBossRoomResult::Found && lo == cachedLo && hi == cachedHi &&
        roomFinds == before, "cached room hit keeps the prior range and performs no engine lookup");
    lo = initialLo;
    hi = initialHi;
    Expect(Query(cache, 23, lo, hi) == CSOLBossRoomResult::Missing && lo == initialLo && hi == initialHi,
        "failed different-id lookup leaves caller outputs and previous cached room intact");
    before = roomFinds;
    Expect(Query(cache, 0, lo, hi) == CSOLBossRoomResult::Found && lo == cachedLo && hi == cachedHi &&
        roomFinds == before, "failed lookup does not evict the previous cached id");
    Reset(cache, access);
    Expect(Query(cache, -1, lo, hi) == CSOLBossRoomResult::Found &&
        lo == Vector(99999, 99999, 99999) && hi == cachedHi,
        "constructor resets id/minimum only; request -1 preserves original comparison behavior");
    Expect(Query(cache, 0, lo, hi) == CSOLBossRoomResult::Found &&
        lo == match->pev->absmin && hi == match->pev->absmax,
        "new controller construction permits a fresh engine range lookup");
    auto* negative = Room(-1, Vector(-1, -2, -3), Vector(-4, -5, -6));
    Expect(Query(cache, -1, lo, hi) == CSOLBossRoomResult::Found &&
        lo == negative->pev->absmin && hi == negative->pev->absmax, "an explicit negative room id can match");
    negative->pev->absmin = Vector(-10, -20, -30);
    RoomData(*negative);
    before = roomFinds;
    Expect(Query(cache, -1, lo, hi) == CSOLBossRoomResult::Found && lo == negative->pev->absmin &&
        roomFinds > before, "cached id -1 still scans rather than using the ordinary cache fast path");
    const int removed = Engine::Index(first->edict());
    Engine::Remove(first->edict());
    std::cout << "ROOM_DELETE " << removed << '\n';
    Expect(Query(cache, 2, lo, hi) == CSOLBossRoomResult::Missing,
        "engine removal affects a noncached room search");
    Expect(Query(cache, 0, lo, hi) == CSOLBossRoomResult::Found && lo == match->pev->absmin,
        "a different id can populate the shared cache after negative and missing lookups");
}

void Cancellation()
{
    auto storage = std::make_unique<Engine>();
    auto& engine = *storage;
    auto access = std::make_shared<CSOLScenarioEntityAccess>();
    CCSOLBossRoomCache cache;
    auto* room = Room(0, Vector(1, 2, 3), Vector(4, 5, 6));
    const Vector originalLo(11, 12, 13), originalHi(14, 15, 16);
    Vector lo = originalLo, hi = originalHi;
    Require(cache.ControllerConstructed(access), "bind current map for cancellation");
    engine.onFind = [&](edict_t*, const char*, const char*) { access->Revoke(); };
    Expect(cache.Bounds(0, lo, hi) == CSOLBossRoomResult::Unavailable && lo == originalLo && hi == originalHi,
        "lookup callback revocation cancels without copying entity bounds");
    bool looked{};
    engine.onFind = [&](edict_t*, const char*, const char*) { looked = true; };
    Expect(cache.Bounds(0, lo, hi) == CSOLBossRoomResult::Unavailable && !looked,
        "revoked map token is checked before any engine lookup");
    access = std::make_shared<CSOLScenarioEntityAccess>();
    Require(cache.ControllerConstructed(access), "bind replacement map token");
    unsigned nested{};
    engine.onFind = [&](edict_t*, const char*, const char*)
    {
        ++nested;
        Vector nestedLo = originalLo, nestedHi = originalHi;
        Expect(cache.Bounds(0, nestedLo, nestedHi) == CSOLBossRoomResult::Unavailable &&
            nestedLo == originalLo && nestedHi == originalHi, "nested room lookup is rejected without engine recursion");
    };
    Expect(cache.Bounds(0, lo, hi) == CSOLBossRoomResult::Found && nested == 1 && lo == room->pev->absmin,
        "outer lookup survives a rejected nested query");
    Require(cache.ControllerConstructed(access), "reset shared cache before reentrant construction");
    bool reset{};
    engine.onFind = [&](edict_t*, const char*, const char*)
    { if (!reset) { reset = true; Require(cache.ControllerConstructed(access), "new controller can reset during lookup"); } };
    lo = originalLo;
    hi = originalHi;
    Expect(cache.Bounds(0, lo, hi) == CSOLBossRoomResult::Unavailable && reset && lo == originalLo && hi == originalHi,
        "old query cannot overwrite a new controller's cache generation");
    engine.onFind = {};
    Expect(cache.Bounds(0, lo, hi) == CSOLBossRoomResult::Found && lo == room->pev->absmin,
        "new controller remains usable after the cancelled old query");
    Require(cache.ControllerConstructed(access), "reset before offset callback retirement");
    const EHANDLE old(room);
    engine.onOffset = [&](const edict_t* ed)
    {
        engine.onOffset = {};
        if (ed == room->edict()) Engine::Remove(const_cast<edict_t*>(ed));
    };
    lo = originalLo;
    hi = originalHi;
    Expect(cache.Bounds(0, lo, hi) == CSOLBossRoomResult::Unavailable && !old.Get<CBaseEntity>() &&
        lo == originalLo && hi == originalHi, "retired entity is checked after an engine offset callback");
    Expect(!cache.ControllerConstructed({}), "missing map access cannot initialize a controller cache");
    access->Revoke();
    Expect(!cache.ControllerConstructed(access), "revoked map access cannot reopen a controller cache");
}
}

int main(int argc, char** argv)
{
    try
    {
        const std::string_view group = argc > 1 ? argv[1] : "entity";
        if (group == "entity") Entity();
        else if (group == "retirement") Retirement();
        else if (group == "cache") Cache();
        else if (group == "cancellation") Cancellation();
        else throw std::runtime_error("unknown boss-room fixture group");
    }
    catch (const std::exception& error)
    {
        ++sdk_test::failures;
        std::cerr << "ERROR " << error.what() << '\n';
    }
    std::cout << "behaviorChecks=" << behaviorChecks << " checks=" << sdk_test::checks
        << " failures=" << sdk_test::failures << '\n';
    return sdk_test::failures ? 1 : 0;
}
