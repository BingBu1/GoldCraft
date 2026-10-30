// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_performance.h"
#include "game_rules_gate.h"
#include "scenario_monster_pool.h"
#include "scenario_monster_ordinary.h"
#include "scenario_player_runtime.h"

#include <cmath>
#include <cstring>

extern edict_t* g_pEdicts; // ReGameDLL's ServerActivate-owned edict array.

namespace
{
using namespace csol::scenario::perf;
enum class Phase : std::size_t { Inactive, Other, Selection, Active, Rejected, Count };
constexpr std::array<const char*, std::size_t(Phase::Count)> PhaseNames{
    "inactive", "other", "selection", "active", "rejected"};
// Upper bounds in microseconds. The final bucket is unbounded.
constexpr std::array<std::uint64_t, 11> Bounds{
    500, 1000, 2000, 4000, 8000, 16000, 33000, 50000, 100000, 250000, 1000000};
struct Distribution
{
    std::uint64_t count{}, total{}, maximum{};
    std::array<std::uint64_t, Bounds.size() + 1> buckets{};
    void Add(std::uint64_t ns)
    {
        ++count; total += ns;
        if (ns > maximum) maximum = ns;
        std::size_t bin{};
        while (bin < Bounds.size() && ns > Bounds[bin] * 1000) ++bin;
        ++buckets[bin];
    }
    double Percentile(unsigned percentage) const
    {
        if (!count) return 0;
        const auto rank = (count * percentage + 99) / 100;
        std::uint64_t seen{};
        for (std::size_t i = 0; i < buckets.size(); ++i)
            if ((seen += buckets[i]) >= rank)
                return i < Bounds.size() ? Bounds[i] / 1000.0 : maximum / 1.0e6;
        return 0;
    }
};
struct FrameData
{
    std::uint64_t frames{}, invalidEngineTimes{}, transitions{};
    std::array<std::uint64_t, std::size_t(Phase::Count)> phases{};
    std::array<Distribution, std::size_t(Phase::Count)> wall, engine;
    Clock::time_point previous{}, populationAt{};
    Phase previousPhase{Phase::Inactive};
    float previousGameTime{};
    std::array<char, 64> previousMap{};
    bool hasPrevious{};
    int lastMonsters{}, lastLiving{}, maxMonsters{}, maxLiving{};
    std::uint64_t populationSamples{}, monsterSum{}, livingSum{};
};
FrameData frames;
bool registered{};

Phase ReadPhase()
{
    if (!g_bServerActive || !gpGlobals || !g_pGameRules) return Phase::Inactive;
    if (CSOL_RulesRejected()) return Phase::Rejected;
    if (!CSOL_UsesScenarioRules()) return Phase::Other;
    if (CSOL_ScenarioSelecting()) return Phase::Selection;
    return Phase::Active;
}
void Population(Clock::time_point now, Phase phase)
{
    if (now < frames.populationAt) return;
    frames.populationAt = now + std::chrono::seconds(1);
    frames.lastMonsters = frames.lastLiving = 0;
    // Rejected/retired maps must never be traversed for entity samples.
    if (phase != Phase::Active && phase != Phase::Selection) return;
    CSOLScenarioSdkScope scope;
    if (!scope.Scenario() || !scope.Current() || !g_pEdicts || !gpGlobals) return;
    for (int i = 1; i < gpGlobals->maxEntities; ++i)
    {
        auto& edict = g_pEdicts[i];
        if (edict.free || !edict.pvPrivateData) continue;
        auto* monster = dynamic_cast<CCSOLScenarioMonster*>(CBaseEntity::Instance(&edict));
        if (!monster || !monster->Pool() || !monster->Pool()->EntityAccessAllowed() ||
            !monster->Pool()->Borrowed(*monster)) continue;
        ++frames.lastMonsters;
        if (monster->pev->deadflag == DEAD_NO && monster->pev->health > 0) ++frames.lastLiving;
    }
    ++frames.populationSamples;
    frames.monsterSum += frames.lastMonsters;
    frames.livingSum += frames.lastLiving;
    if (frames.lastMonsters > frames.maxMonsters) frames.maxMonsters = frames.lastMonsters;
    if (frames.lastLiving > frames.maxLiving) frames.maxLiving = frames.lastLiving;
}
void PrintDistribution(const char* kind, const char* phase, const Distribution& value)
{
    if (!value.count) return;
    SERVER_PRINT(UTIL_VarArgs("[csol_perf] %s phase=%s n=%llu mean_ms=%.6f p50_bucket_upper_ms=%.3f p95_bucket_upper_ms=%.3f max_ms=%.6f rate_hz=%.3f\n",
        kind, phase, static_cast<unsigned long long>(value.count), value.total / (1.0e6 * value.count),
        value.Percentile(50), value.Percentile(95), value.maximum / 1.0e6,
        value.total ? value.count * 1.0e9 / value.total : 0));
}
void Report()
{
    SERVER_PRINT(UTIL_VarArgs("[csol_perf] enabled=%d frames=%llu transitions=%llu invalid_engine_times=%llu map=%s current_phase=%s\n",
        enabled ? 1 : 0, static_cast<unsigned long long>(frames.frames),
        static_cast<unsigned long long>(frames.transitions), static_cast<unsigned long long>(frames.invalidEngineTimes),
        gpGlobals && gpGlobals->pStringBase ? STRING(gpGlobals->mapname) : "unknown", PhaseNames[std::size_t(ReadPhase())]));
    for (std::size_t i = 0; i < frames.phases.size(); ++i)
    {
        SERVER_PRINT(UTIL_VarArgs("[csol_perf] phase=%s frames=%llu\n", PhaseNames[i], static_cast<unsigned long long>(frames.phases[i])));
        PrintDistribution("wall_startframe_interval", PhaseNames[i], frames.wall[i]);
        PrintDistribution("engine_frametime", PhaseNames[i], frames.engine[i]);
    }
    SERVER_PRINT(UTIL_VarArgs("[csol_perf] population_samples=%llu borrowed_last=%d borrowed_max=%d borrowed_mean=%.3f living_last=%d living_max=%d living_mean=%.3f\n",
        static_cast<unsigned long long>(frames.populationSamples), frames.lastMonsters, frames.maxMonsters,
        frames.populationSamples ? double(frames.monsterSum) / frames.populationSamples : 0,
        frames.lastLiving, frames.maxLiving, frames.populationSamples ? double(frames.livingSum) / frames.populationSamples : 0));
    for (std::size_t i = 0; i < measurements.timers.size(); ++i)
    {
        const auto& value = measurements.timers[i];
        SERVER_PRINT(UTIL_VarArgs("[csol_perf] timer=%s calls=%llu inclusive_ms=%.6f mean_us=%.6f max_ms=%.6f\n",
            TimerNames[i].data(), static_cast<unsigned long long>(value.calls), value.nanoseconds / 1.0e6,
            value.calls ? value.nanoseconds / (1000.0 * value.calls) : 0, value.maximum / 1.0e6));
    }
    for (std::size_t i = 0; i < measurements.counters.size(); ++i)
        SERVER_PRINT(UTIL_VarArgs("[csol_perf] counter=%s value=%llu\n", CounterNames[i].data(),
            static_cast<unsigned long long>(measurements.counters[i])));
    SERVER_PRINT("[csol_perf] Timers include children: do not sum categories. Frame intervals include engine work, sleep and diagnostics. Active means current scenario rules, not proof of combat. Rejected/empty samples are not FPS acceptance.\n");
    if (CSOL_RulesRejected()) SERVER_PRINT(UTIL_VarArgs("[csol_perf] rejection=%s\n", CSOL_RulesRejectionReason()));
    // Printing through RCON/console is not a gameplay frame sample.
    frames.hasPrevious = false;
}
void Command()
{
    const char* action = CMD_ARGC() > 1 ? CMD_ARGV(1) : "report";
    if (!std::strcmp(action, "begin") || !std::strcmp(action, "reset"))
    {
        const bool enable = !std::strcmp(action, "begin") || enabled;
        Reset(enable);
        frames = {};
        SERVER_PRINT(UTIL_VarArgs("[csol_perf] reset; enabled=%d. Use csol_scenario_perf end to stop and report.\n", enabled ? 1 : 0));
    }
    else if (!std::strcmp(action, "end"))
    {
        enabled = false;
        ++generation; // Retire any enclosing samples without clearing results.
        Report();
    }
    else if (!std::strcmp(action, "report")) Report();
    else if (!std::strcmp(action, "players"))
    {
        CSOLScenarioSdkScope scope;
        if (!scope.Scenario() || !scope.Current() || !g_pEdicts || !gpGlobals) return;
        const auto players = CSOL_ScenarioPlayerAuthority(scope.EntityAccess().get());
        if (!players) return;
        for (int i = 1; i <= gpGlobals->maxClients; ++i)
        {
            if (!scope.Current()) return;
            auto& edict = g_pEdicts[i];
            if (edict.free || !edict.pvPrivateData) continue;
            auto* player = dynamic_cast<CBasePlayer*>(CBaseEntity::Instance(&edict));
            if (!player || player->has_disconnected) continue;
            const EHANDLE handle(player);
            const auto state = players->Find(handle);
            if (!state) continue;
            std::optional<csol::scenario::ClassModDefinition> selected;
            csol::scenario::ConfigError error;
            const bool queried = players->CurrentClassMod(handle, 10, selected, error);
            if (!scope.Current() || players->Find(handle) != state) return;
            const auto* weapon = dynamic_cast<CBasePlayerWeapon*>(player->m_pActiveItem);
            // Explicit, read-only diagnostics distinguish missing character
            // abilities from global death-owner errors without logging per hit.
            SERVER_PRINT(UTIL_VarArgs("[csol_player] entity=%d class=%d weapon=%d fake=%d classmod_current=%d ability_item=%d ability_subtype=%d ability_installed=%d damage_hits=%u error=%s\n",
                i, state->classId, weapon ? weapon->m_iId : 0,
                (player->pev->flags & FL_FAKECLIENT) != 0, queried ? 1 : 0,
                selected ? selected->item : 0, selected ? selected->subtype : 0,
                state->activeSkill ? 1 : 0, state->monsterDamageHits, error.message.c_str()));
        }
        frames.hasPrevious = false;
    }
    else if (!std::strcmp(action, "ai"))
    {
        CSOLScenarioSdkScope scope;
        if (!scope.Scenario() || !scope.Current() || !g_pEdicts || !gpGlobals) return;
        // An explicit snapshot keeps diagnostics out of the Think hot path.
        // A standing corpse and a live AI error have different causes.
        for (int i = 1; i < gpGlobals->maxEntities; ++i)
        {
            if (!scope.Current()) return;
            auto& edict = g_pEdicts[i];
            if (edict.free || !edict.pvPrivateData) continue;
            auto* monster = dynamic_cast<CCSOLScenarioMonster*>(CBaseEntity::Instance(&edict));
            if (!monster || !monster->Pool() || !monster->Pool()->EntityAccessAllowed() ||
                !monster->Pool()->Borrowed(*monster)) continue;
            const auto* ordinary = dynamic_cast<const CCSOLOrdinaryMonsterBehavior*>(monster->Behavior());
            const auto error = ordinary ? ordinary->Error().message : std::string{};
            SERVER_PRINT(UTIL_VarArgs("[csol_ai] entity=%d type=%d skill=%d health=%.3f deadflag=%d sequence=%d frame=%.3f rate=%.3f animtime=%.3f nextthink=%.3f now=%.3f error=%s\n",
                i, monster->TypeId(), monster->Type() ? monster->Type()->skill : 0,
                monster->pev->health, monster->pev->deadflag, monster->pev->sequence,
                monster->pev->frame, monster->pev->framerate, monster->pev->animtime,
                monster->pev->nextthink, gpGlobals->time, error.c_str()));
        }
    }
    else SERVER_PRINT("Usage: csol_scenario_perf begin|reset|report|end|ai|players\n");
}
}

void CSOL_InitScenarioPerformance()
{
    if (registered) return;
    ADD_SERVER_COMMAND("csol_scenario_perf", Command);
    registered = true;
}
void CSOL_RecordScenarioPerformanceFrame()
{
    if (!csol::scenario::perf::enabled) return;
    Scope diagnostic(Timer::Diagnostics);
    const auto now = Clock::now();
    const auto phase = ReadPhase();
    const auto index = std::size_t(phase);
    const char* map = gpGlobals && gpGlobals->pStringBase ? STRING(gpGlobals->mapname) : "";
    const float gameTime = gpGlobals ? gpGlobals->time : 0;
    const bool sameMap = !std::strcmp(frames.previousMap.data(), map) && gameTime >= frames.previousGameTime;
    ++frames.frames;
    ++frames.phases[index];
    // Exclude transitions so a map load cannot appear as a combat frame.
    if (frames.hasPrevious && frames.previousPhase == phase && sameMap)
    {
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now - frames.previous).count();
        if (ns >= 0) frames.wall[index].Add(static_cast<std::uint64_t>(ns));
    }
    else if (frames.hasPrevious) ++frames.transitions;
    frames.previous = now;
    frames.previousPhase = phase;
    frames.previousGameTime = gameTime;
    std::strncpy(frames.previousMap.data(), map, frames.previousMap.size() - 1);
    frames.previousMap.back() = '\0';
    frames.hasPrevious = true;
    const double seconds = gpGlobals ? gpGlobals->frametime : -1;
    if (std::isfinite(seconds) && seconds >= 0 && seconds <= 3600)
        frames.engine[index].Add(static_cast<std::uint64_t>(seconds * 1.0e9));
    else ++frames.invalidEngineTimes;
    Population(now, phase);
}
