// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../csol-scenario-monster-pool/pool_fixture.hpp"
#include "../csol-scenario-map-session/fixtures.hpp"
#include "../../native/csol/server/scenario_death_targets.h"
#include "../../native/csol/server/scenario_death_notice.h"
#include <deque>
#include <iostream>
#include <limits>
#include <string_view>

namespace death_events_test
{
inline int checks{}, failures{};
inline void Check(bool value, const char* label)
{ ++checks; if (!value) { ++failures; std::cerr << "FAIL " << label << '\n'; } }
inline void Require(bool value, const char* label)
{ Check(value, label); if (!value) throw std::runtime_error(label); }

struct Trace
{
    char kind{};
    int index{}, value{};
    std::string name;
};
struct Packet
{
    int destination{}, message{}, slot{};
    const float* origin{};
    std::vector<std::uint8_t> bytes;
    bool complete{};
};
struct UseCall
{
    int index{};
    CBaseEntity* activator{};
    CBaseEntity* caller{};
    USE_TYPE type{};
    float value{};
};
struct Target final : CBaseEntity
{
    int index{};
    std::vector<UseCall>* calls{};
    std::function<void()> action;
    void Use(CBaseEntity* activator, CBaseEntity* caller, USE_TYPE type, float value) override
    {
        calls->push_back({index, activator, caller, type, value});
        // A target can destroy itself. Neither the object nor its std::function
        // is touched after the copied callback has run.
        const auto callback = action;
        if (callback) callback();
    }
};

// Controlled engine storage/services. SDK classes, RTTI, EHANDLE, Map, Pool,
// Requests, target traversal and both production packet writers remain real.
struct EventEngine : PoolEngine
{
    inline static EventEngine* self{};
    std::map<std::string, std::string> files;
    std::set<void*> allocations;
    std::map<edict_t*, std::function<void(void*)>> privateOwners;
    std::vector<Trace> trace;
    std::vector<Packet> packets;
    std::vector<UseCall> uses;
    std::set<int> zeroOffsets;
    std::map<const edict_t*, int> indexReplies;
    std::function<void(char, int)> onQuery;
    std::function<void(char)> onMessage;
    std::function<edict_t*(edict_t*, const char*)> findOverride;
    int registeredId{96};
    bool packetOpen{}, worldSentinel{};
    edict_t world{};

    EventEngine();
    ~EventEngine();
    static std::string Hex(std::string_view);
    static unsigned char* Load(const char*, int*);
    static void ReleaseFile(void*);
    static edict_t* CreateMixed(int);
    static int QueryIndex(const edict_t*);
    static int QueryOffset(const edict_t*);
    static edict_t* QuerySlot(int);
    static edict_t* Find(edict_t*, const char*, const char*);
    static int Register(const char*, int);
    static void Begin(int, int, const float*, edict_t*);
    static void Byte(int);
    static void Short(int);
    static void End();
    void Hook(char);
    edict_t* Raw(const char*, int);
    CBasePlayer& Client(int);
    Target& AddTarget(int, const char*);
    void Dispose(edict_t*, bool markFree = true);
    void DisposeAll();
    void Markers();
    void ClearCapture();
    std::vector<int> Queried(char) const;
    std::vector<int> Recipients() const;
};

struct Events final : csol::scenario::TriggerWaveEvents
{
    // Explicit observation state; not a production wave consumer.
    bool observerValid{true};
    csol::scenario::ConfigError observerError;
    bool Check(csol::scenario::ConfigError& error) const override
    {
        if (!observerValid || !observerError.message.empty()) { error = observerError; return false; }
        return true;
    }
    void WaveActivated(std::uint16_t) override {}
    void BeforeSpatialWaveEnd(std::uint16_t) override {}
    void WavesChanged(std::span<const std::uint16_t>) override {}
    void GoalReached() override {}
};
struct NoOwner final : CCSOLMonsterDeathExecutor
{
    bool ExecuteDeath(CCSOLScenarioMonster&, const CSOLMonsterDeathArguments&) override
    { throw std::runtime_error("complete synchronous death owner is outside this fixture"); }
};
struct Fixture
{
    EventEngine engine;
    Events events;
    std::deque<csol::scenario::MonsterType> types;
    CCSOLMonsterPool pool;
    CCSOLScenarioMap map;
    NoOwner owner;
    CCSOLMonsterDeathRequests requests{owner};
    CCSOLScenarioMessages messages;
    CCSOLScenarioDeathTargets targets{map, requests};
    CCSOLScenarioDeathNotice notice{map, requests, messages};
    csol::scenario::ConfigError error;
    Fixture();
    ~Fixture();
    CCSOLScenarioMonster& Monster(int priority = 0, int id = 40, int suffix = -1);
    void Reborrow(CCSOLScenarioMonster&);
    void DestroyMapStorage();
};

inline bool DeathPacket(const Packet& packet, int slot, int entity, int id = 96)
{
    return packet.destination == MSG_ONE && packet.message == id && packet.slot == slot &&
        packet.origin == nullptr && packet.complete && packet.bytes == std::vector<std::uint8_t>{
        200, static_cast<std::uint8_t>(entity), static_cast<std::uint8_t>(static_cast<unsigned>(entity) >> 8)};
}
inline bool WavePacket(const Packet& packet, int event, int id = 96)
{
    return packet.destination == MSG_ALL && packet.message == id && packet.slot == 0 &&
        packet.origin == nullptr && packet.complete && packet.bytes == std::vector<std::uint8_t>{
        27, 7, static_cast<std::uint8_t>(event), static_cast<std::uint8_t>(static_cast<unsigned>(event) >> 8)};
}
void TargetCases();
void NoticeCases();
void CancellationCases();
void MessageCases();
}
