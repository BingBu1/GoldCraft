// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstring>
#include <cstdlib>
#include <stdexcept>
#include "../csol-scenario-point-manager/engine_fixture.h"
#include "../csol-scenario-map-session/fixtures.hpp"
#include "../../native/csol/server/scenario_map.h"
#include "rules_fixture.hpp"

extern "C" void zbstrigger(entvars_t*);

// Reuse the existing engine's controlled edicts and exact SDK entity types.
// This is not a real engine file system, classname registry, or GameRules start.
struct FrameEngine : Engine
{
    inline static FrameEngine* current{};
    std::map<std::string, std::string> files;
    std::map<void*, std::size_t> allocations;
    std::vector<int> slots;
    std::vector<CBasePlayer*> players;
    std::function<void()> onIndex;
    bool dedicated{};
    int dedicatedQueries{};

    FrameEngine()
    {
        current = this; globals.maxClients = 4;
        g_engfuncs.pfnLoadFileForMe = Load; g_engfuncs.pfnFreeFile = Free;
        g_engfuncs.pfnPEntityOfEntIndex = ClientIndex;
        g_engfuncs.pfnSetModel = [](edict_t*, const char*) {};
        g_engfuncs.pfnIsDedicatedServer = IsDedicated;
        std::vector<std::string> header(59), row(59, "0");
        header[0] = ";map_id"; header[1] = "map_name";
        header[2] = "map_filename"; header[58] = "SubType";
        row[0] = "1"; row[1] = "test"; row[2] = "map_session_test"; row[58] = "1";
        auto& catalog = files["resource/maplist.csv"];
        for (const auto& fields : {header, row})
        {
            for (std::size_t i = 0; i < fields.size(); ++i)
            { if (i) catalog += ','; catalog += fields[i]; }
            catalog += "\r\n";
        }
        files["maps/zbs_wave_map_session_test.cso"] = Hex(fixtures::waves);
        files["maps/zbs_map_session_test.cso"] = Hex(fixtures::rounds);
        files["maps/ZBS_ZT_map_session_test.cso"] = Hex(fixtures::types);
        files["maps/ZBS_TRG_map_session_test.cso"] = Hex(fixtures::triggers);
    }
    ~FrameEngine()
    {
        if (!allocations.empty()) std::abort();
        ClearWorld(); current = nullptr;
    }
    static std::string Hex(std::string_view text)
    {
        const auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
        std::string result;
        for (std::size_t i = 0; i < text.size(); i += 2)
            result.push_back(static_cast<char>(digit(text[i]) * 16 + digit(text[i + 1])));
        return result;
    }
    static unsigned char* Load(const char* name, int* length)
    {
        auto& engine = *current;
        if (*length) throw std::runtime_error("uninitialized file length");
        const auto found = engine.files.find(name);
        if (found == engine.files.end()) return nullptr;
        *length = static_cast<int>(found->second.size());
        auto* data = new unsigned char[found->second.size() + 1];
        std::memcpy(data, found->second.data(), found->second.size());
        data[found->second.size()] = 0;
        engine.allocations.emplace(data, found->second.size()); return data;
    }
    static void Free(void* memory)
    {
        if (!current->allocations.erase(memory)) throw std::runtime_error("invalid file release");
        delete[] static_cast<unsigned char*>(memory);
    }
    static edict_t* ClientIndex(int slot)
    {
        current->slots.push_back(slot);
        if (current->onIndex) current->onIndex();
        return Engine::FromOffset(slot);
    }
    static int IsDedicated() { ++current->dedicatedQueries; return current->dedicated ? 1 : 0; }
    edict_t* Raw(const char* classname, int index)
    {
        auto edict = std::make_unique<edict_t>();
        edict->v.pContainingEntity = edict.get(); edict->serialnumber = 1;
        auto* raw = edict.get(); indices[raw] = index; edicts.push_back(std::move(edict));
        raw->v.classname = AllocString(classname); return raw;
    }
    CBasePlayer& Client(int slot, Vector origin)
    {
        auto* edict = Raw("player", slot); auto* player = new(&edict->v) CBasePlayer;
        player->pev = &edict->v; player->m_pEntity = nullptr;
        player->pev->health = 100; player->pev->deadflag = DEAD_NO; player->pev->origin = origin;
        players.push_back(player); return *player;
    }
    void Trigger(int index, int id, float x)
    {
        auto* edict = Raw("zbstrigger", index); edict->v.model = AllocString("*1");
        zbstrigger(&edict->v);
        auto& trigger = *static_cast<CCSOLScenarioTrigger*>(edict->pvPrivateData);
        trigger.m_iId = id; trigger.Spawn();
        trigger.pev->mins = Vector(x - 5, -5, -5); trigger.pev->maxs = Vector(x + 5, 5, 5);
    }
    void Markers()
    {
        Player(101, 1); Monster(201, 1, 5);
        Trigger(301, 1, 0); Trigger(302, 2, 100); Trigger(330, 30, 200);
    }
    void ClearWorld()
    {
        for (auto* player : players)
        {
            auto* edict = player->edict();
            // Allocator is calloc in Engine::Allocate, so do not use delete.
            // The exact SDK type is known; its explicit destructor is intended.
            player->~CBasePlayer(); std::free(player); edict->pvPrivateData = nullptr;
        }
        players.clear(); ClearEdicts();
    }
};

struct FrameEvents : csol::scenario::TriggerWaveEvents
{
    // Explicit observation state; not a production wave consumer.
    bool observerValid{true};
    csol::scenario::ConfigError observerError;
    bool Check(csol::scenario::ConfigError& error) const override
    {
        if (!observerValid || !observerError.message.empty()) { error = observerError; return false; }
        return true;
    }
    std::vector<char> tags;
    std::vector<std::vector<std::uint16_t>> summaries;
    std::function<void(char)> callback;
    void Invoke(char tag) { tags.push_back(tag); if (callback) callback(tag); }
    void WaveActivated(std::uint16_t) override { Invoke('a'); }
    void BeforeSpatialWaveEnd(std::uint16_t) override { Invoke('e'); }
    void WavesChanged(std::span<const std::uint16_t> ids) override
    { summaries.emplace_back(ids.begin(), ids.end()); Invoke('s'); }
    void GoalReached() override { Invoke('g'); }
    void Clear() { tags.clear(); summaries.clear(); }
};

struct FrameFixture
{
    FrameEngine engine;
    CCSOLScenarioMap map;
    FrameEvents events;
    FrameRules rules;
    int dispatches{};
    std::vector<csol::scenario::RowClock> dispatchClocks;
    std::function<void(const csol::scenario::WaveRow&, csol::scenario::RowClock&)> onDispatch;
    explicit FrameFixture(bool begin = true)
    {
        engine.Markers();
        if (begin) Start();
    }
    void Start(float now = 0, float freeze = 0)
    {
        if (!map.BeginMap("map_session_test") || !map.CollectEntities() ||
            !map.BeginRound(0, now, freeze, events)) throw std::runtime_error("frame fixture setup failed");
        events.Clear(); engine.slots.clear();
    }
    static void Dispatch(void* context, const csol::scenario::WaveRow& row,
        csol::scenario::RowClock& clock)
    {
        auto& fixture = *static_cast<FrameFixture*>(context);
        ++fixture.dispatches; fixture.dispatchClocks.push_back(clock);
        if (fixture.onDispatch) fixture.onDispatch(row, clock);
    }
    CSOLScenarioFrame Frame(float think, float engineTime, int alive = 4)
    {
        engine.globals.time = engineTime;
        return map.UpdateFrame(rules, think, alive, Dispatch, this, events);
    }
    void Activate(std::uint16_t wave = 7, float elapsed = 0)
    {
        csol::scenario::WaveTriggerActions actions(map.Round().Waves(), map.Round().Schedule(), events);
        actions.ActivateWave(wave, elapsed);
        if (!actions.Valid()) throw std::runtime_error("frame fixture activation failed");
        events.Clear();
    }
};
