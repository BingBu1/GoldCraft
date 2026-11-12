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
}

int main(int argc, char** argv)
{
    try
    {
        const std::string_view group = argc > 1 ? argv[1] : "entity";
        if (group == "entity") Entity();
        else if (group == "retirement") Retirement();
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
