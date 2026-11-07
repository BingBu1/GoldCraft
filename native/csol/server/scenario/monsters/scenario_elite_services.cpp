// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_elite_services.h"
#include "../core/scenario_runtime.h"
#include "../core/scenario_session.h"
#include "scenario_monster_factory.h"
#include "scenario_monster_ordinary.h"
#include "../core/scenario_entity_lookup.h"
#include "../core/game_rules_gate.h"

#include <cmath>
#include <array>
#include <bit>
#include <limits>

#pragma clang fp contract(off)

namespace
{
using csol::scenario::ConfigError;
bool Fail(ConfigError& error, const char* message) { error.message = message; return false; }

// CVTTSS2SI returns the integer-indefinite value on overflow/NaN. Preserve
// that instruction's result without an undefined out-of-range C++ cast.
std::int32_t TruncateClock(float value)
{
    if (!std::isfinite(value) ||
        value < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
        value > static_cast<double>(std::numeric_limits<std::int32_t>::max()))
        return std::numeric_limits<std::int32_t>::min();
    return static_cast<std::int32_t>(value);
}
}

struct CCSOLScenarioEliteServices::Call
{
    const CCSOLScenarioEliteServices& owner;
    std::shared_ptr<CCSOLScenarioRuntime> runtime;
    std::shared_ptr<CCSOLScenarioEliteServices> service;
    std::shared_ptr<CCSOLScenarioSession> session;
    std::shared_ptr<State> state;
    explicit Call(const CCSOLScenarioEliteServices& value)
        : owner(value), runtime(value.m_runtime.lock()),
          service(runtime ? runtime->eliteServices : nullptr),
          session(value.m_session), state(value.m_state) {}
    bool MapCurrent() const
    {
        return runtime && !runtime->Closing() && service.get() == &owner &&
            runtime->eliteServices == service && runtime->session == session && session &&
            owner.m_state == state && state && state->access && state->access->Allowed() &&
            runtime->Generation() == state->runtimeGeneration &&
            session->Map().EntityAccess() == state->access;
    }
    bool Current() const
    {
        return MapCurrent() && state->roundReady &&
            session->Map().RoundGeneration() == state->round &&
            session->Map().State() == CSOLScenarioMapState::RoundActive;
    }
};

CCSOLScenarioEliteServices::CCSOLScenarioEliteServices(
    std::weak_ptr<CCSOLScenarioRuntime> runtime,
    std::shared_ptr<const csol::scenario::EliteSettings> settings)
    : m_runtime(std::move(runtime)), m_settings(std::move(settings)), m_state(std::make_shared<State>())
{
    const auto owner = m_runtime.lock();
    m_session = owner ? owner->session : nullptr;
    if (!owner || !m_session) return;
    m_state->access = m_session->Map().EntityAccess();
    m_state->runtimeGeneration = owner->Generation();
    m_state->round = m_session->Map().RoundGeneration();
    // Original10820967 rules+320=0;10822FD3 rules+26C/+270=0;
    // a new game has11EE764C=0. Ordinary round changes preserve those values.
}

bool CCSOLScenarioEliteServices::Current() const { return Call(*this).Current(); }

void CCSOLScenarioEliteServices::Invalidate() { m_state.reset(); }

bool CCSOLScenarioEliteServices::BeginRound(float now, ConfigError& error, bool replay)
{
    Call call(*this);
    if (!call.MapCurrent() || !error.message.empty()) return false;
    const auto round = call.session->Map().RoundGeneration();
    if (call.session->Map().State() != CSOLScenarioMapState::RoundActive ||
        round != call.state->round + 1)
        return Fail(error, "elite services require the next formal round generation");
    if (!std::isfinite(now)) return Fail(error, "elite services round clock is invalid");
    auto next = std::make_shared<State>(*call.state);
    if (replay)
    {
        next->previousRoundDuration = 0;
        next->fragRateReference = 0;
        next->savedElapsed = 0;
        next->retryCountdown = -1;
        m_baseType.reset();
        m_typeDifficulty = -1;
    }
    else if (next->roundStarted > 0)
    {
        next->previousRoundDuration = now - next->roundStarted;
        if (!std::isfinite(next->previousRoundDuration))
            return Fail(error, "elite services previous round duration is invalid");
    }
    next->roundStarted = now;
    next->round = round;
    next->roundReady = true;
    next->lastSuccessfulPoint = 0;
    m_state = std::move(next);
    return true;
}

bool CCSOLScenarioEliteServices::QualifiedPlayers(CSOLMonsterSpawnCall& spawn,
    int& count, ConfigError& error) const
{
    Call call(*this);
    if (!call.Current() || !error.message.empty() ||
        !spawn.BelongsTo(call.session->Map()) || !spawn.Current()) return false;
    const auto players = call.session->Players();
    if (!players) return Fail(error, "elite qualified-player registry is unavailable");
    int qualified{};
    const bool counted = players->CountDamageScalePlayers(qualified);
    if (!call.Current() || !spawn.Current() || call.session->Players() != players) return false;
    if (!counted) return Fail(error, "elite qualified-player registry could not be enumerated");
    // This is the existing108F56B0 implementation: configured class record,
    // current player identity and !FL_DORMANT. Dead players still count.
    count = qualified;
    return true;
}

bool CCSOLScenarioEliteServices::RecordSuccessfulSpawn(CSOLMonsterSpawnCall& spawn,
    int requestedPoint, ConfigError& error)
{
    Call call(*this);
    if (!call.Current() || !error.message.empty() ||
        !spawn.BelongsTo(call.session->Map())) return false;
    const auto* monster = spawn.Current();
    if (!monster) return false;
    if (monster->SpawnPointId() != requestedPoint)
        return Fail(error, "elite spawn-point record differs from its factory request");
    //11385AF8 passes the wrapper's point argument as Create's final+24
    // argument;113859AC stores it AFTER rules v328. It is not a TRG group.
    call.state->lastSuccessfulPoint = requestedPoint;
    return true;
}

bool CCSOLScenarioEliteServices::Spawn(bool follower, ConfigError& error)
{
    Call call(*this);
    if (!call.Current() || !error.message.empty()) return false;
    const int point = call.state->lastSuccessfulPoint;
    if (point <= 0) return true; // Original11384CF0 returns before any spawn work.
    const auto manager = call.runtime->eliteManager;
    if (!manager || !manager->Current()) return Fail(error, "elite spawn manager is unavailable");
    if (!m_settings) return Fail(error, "elite spawn settings are unavailable");
    const int difficulty = call.session->Difficulty().SelectedRow();
    if (!m_baseType)
    {
        if (!csol::scenario::MakeEliteMonsterType(*m_settings, difficulty, m_baseType, error)) return false;
        m_typeDifficulty = difficulty;
    }
    if (m_typeDifficulty != difficulty)
        return Fail(error, "elite spawn difficulty changed inside its map session");
    // Reuse one immutable manager type; allocating a new row on every clone
    // would grow the factory's borrowed-record list for the entire map.
    const bool spawned = manager->Spawn(follower, point, m_baseType, error);
    return call.Current() && call.runtime->eliteManager == manager && manager->Current() && spawned;
}

bool CCSOLScenarioEliteServices::SpawnFollower(CSOLMonsterSpawnCall& spawn, ConfigError& error)
{
    Call call(*this);
    if (!call.Current() || !error.message.empty() ||
        !spawn.BelongsTo(call.session->Map()) || !spawn.Current()) return false;
    const bool spawned = Spawn(true, error);
    return call.Current() && spawn.Current() && spawned;
}

bool CCSOLScenarioEliteServices::SpawnPrimary(ConfigError& error) { return Spawn(false, error); }

bool CCSOLScenarioEliteServices::ReadSpawnClock(CCSOLScenarioEliteManager::SpawnClock& clock,
    ConfigError& error) const
{
    Call call(*this);
    if (!call.Current() || !error.message.empty()) return false;
    auto* rules = dynamic_cast<CHalfLifeMultiplay*>(CSOL_ScenarioOwnerRules(call.session.get()));
    if (!call.Current()) return false;
    if (!rules || !gpGlobals) return Fail(error, "elite spawn rules clock is unavailable");
    const float now = gpGlobals->time;
    if (!std::isfinite(now)) return Fail(error, "elite spawn engine time is invalid");
    CCSOLScenarioEliteManager::SpawnClock result;
    // Host's real game-started state, set by ScenarioRestartRound and cleared
    // in selection/waiting. Do not substitute true or current CT population.
    result.enabled = rules->IsGameStarted();
    result.elapsed = call.state->savedElapsed > 0 ?
        (now - call.state->roundStarted) + static_cast<float>(call.state->savedElapsed) :
        now - call.session->ChallengeTimeOrigin();
    result.fragRateReference = call.state->fragRateReference;
    if (!std::isfinite(result.elapsed)) return Fail(error, "elite spawn elapsed time is invalid");
    clock = result;
    return true;
}

bool CCSOLScenarioEliteServices::Update(ConfigError& error)
{
    Call call(*this);
    if (!call.Current() || !error.message.empty()) return false;
    const auto manager = call.runtime->eliteManager;
    if (!manager || !manager->Current()) return Fail(error, "elite schedule manager is unavailable");
    CCSOLScenarioEliteManager::SpawnClock clock;
    if (!ReadSpawnClock(clock, error) || !call.Current()) return false;
    const bool updated = manager->UpdateSpawnSchedule(clock,
        [](void* context, ConfigError& status)
        { return static_cast<CCSOLScenarioEliteServices*>(context)->SpawnPrimary(status); }, this, error);
    return call.Current() && call.runtime->eliteManager == manager && manager->Current() && updated;
}

bool CCSOLScenarioEliteServices::UpdateRecovery(CSOLMonsterSpawnCall& spawn,
    CSOLOrdinaryMonsterState& ordinary, float& changedAt, int& previousArea,
    bool& relocate, ConfigError& error) const
{
    relocate = false;
    Call call(*this);
    if (!call.Current() || !error.message.empty() ||
        !spawn.BelongsTo(call.session->Map()) || !spawn.Current()) return false;
    const auto navigation = call.session->Navigation();
    if (!navigation || !gpGlobals || !std::isfinite(gpGlobals->time) ||
        !std::isfinite(changedAt) || gpGlobals->maxClients < 0 || gpGlobals->maxClients > 32)
        return Fail(error, "elite navigation recovery state is unavailable");
    const auto worldGeneration = navigation->World().Generation();
    const auto current = [&]
    {
        return call.Current() && spawn.Current() && call.session->Navigation() == navigation &&
            navigation->World().Generation() == worldGeneration &&
            (!ordinary.position.worldGeneration || ordinary.position.worldGeneration == worldGeneration);
    };
    if (!current()) return false;
    const float now = gpGlobals->time;
    const int maxClients = gpGlobals->maxClients;
    if (ordinary.position.lastArea)
    {
        const int area = std::bit_cast<std::int32_t>(ordinary.position.lastArea->GetID());
        if (area != previousArea)
        {
            previousArea = area;
            changedAt = now;
        }
        else
        {
            const EHANDLE target = ordinary.target;
            auto* player = dynamic_cast<CBasePlayer*>(target.Get<CBaseEntity>());
            bool hasTarget{};
            //11399B80 permits a dead player; its vBC excludes FL_SPECTATOR.
            if (player && player->pev && !(player->pev->flags & FL_SPECTATOR))
            {
                const bool isPlayer = player->IsPlayer() != FALSE;
                if (!current() || target.Get<CBaseEntity>() != player) return false;
                if (isPlayer)
                {
                    if (!g_engfuncs.pfnIndexOfEdict)
                        return Fail(error, "elite target index query is unavailable");
                    hasTarget = ENTINDEX(player->edict()) != 0;
                    if (!current() || target.Get<CBaseEntity>() != player) return false;
                }
            }
            if (hasTarget)
            {
                const Vector delta = player->pev->origin - spawn.Current()->pev->origin;
                const float squared = (delta.y * delta.y + delta.x * delta.x) + delta.z * delta.z;
                if (!std::isfinite(squared)) return Fail(error, "elite target distance is invalid");
                if (squared < 14400.0f) changedAt = now;
            }
            else if (call.state->retryCountdown > -1) changedAt = now;
        }
    }
    //10141CA0 initializes the original unnamed setting to20.0f. No NAV
    //area skips the refresh above, not this forced/strict-expiry test.
    if (!spawn.Current()->HostState().navigationExpired && !(now > changedAt + 20.0f)) return true;
    if (!g_engfuncs.pfnPEntityOfEntIndex || !g_engfuncs.pfnIndexOfEdict)
        return Fail(error, "elite recovery player enumeration is unavailable");
    struct Candidate { int index{}; EHANDLE handle; };
    std::array<Candidate, 32> candidates{};
    int count{};
    for (int index = 1; index <= maxClients; ++index)
    {
        auto* player = dynamic_cast<CBasePlayer*>(CSOL_ScenarioPlayerEntity(index, call.state->access));
        if (!current()) return false;
        if (!player || !player->pev || !UTIL_IsValidEntity(player->edict())) continue;
        const EHANDLE handle(player);
        const bool alive = player->IsAlive() != FALSE;
        if (!current()) return false;
        if (handle.Get<CBaseEntity>() != player || !alive) continue;
        const int actualIndex = ENTINDEX(player->edict());
        if (!current()) return false;
        if (handle.Get<CBaseEntity>() != player) continue;
        if (actualIndex != index) return Fail(error, "elite recovery player index changed");
        candidates[count++] = {index, handle};
    }
    //109676E0 has no team, stealth, class or enemy filter. The setter then
    //resolves its selected index again. Retain identity across engine calls.
    if (count)
    {
        if (!g_engfuncs.pfnRandomLong) return Fail(error, "elite recovery random source is unavailable");
        const int chosen = RANDOM_LONG(0, count - 1);
        if (!current()) return false;
        if (chosen < 0 || chosen >= count) return Fail(error, "elite recovery random index is invalid");
        const auto& selected = candidates[chosen];
        auto* player = dynamic_cast<CBasePlayer*>(CSOL_ScenarioPlayerEntity(selected.index, call.state->access));
        if (!current()) return false;
        if (player && selected.handle.Get<CBaseEntity>() == player && player->pev &&
            static_cast<std::uint32_t>(player->pev->flags) != 0x80000000u)
        {
            const bool alive = player->IsAlive() != FALSE;
            if (!current()) return false;
            if (alive && selected.handle.Get<CBaseEntity>() == player) ordinary.target = selected.handle;
        }
    }
    //11399F10(0) preserves the old target, but even an empty list reaches
    //these writes and Teleport. +1D54 is the F4 missing-area timer (113F1D83/
    //113F1DB5), NOT one of the ordinary findPath interval fields.
    spawn.Current()->HostState().navigationExpired = false;
    ordinary.position.missingAreaUntil = -1.0f;
    relocate = true;
    return true;
}

bool CCSOLScenarioEliteServices::SetFragRateReference(float value, ConfigError& error)
{
    Call call(*this);
    if (!call.MapCurrent() || !error.message.empty()) return false;
    if (!std::isfinite(value)) return Fail(error, "elite authoritative frag reference is invalid");
    call.state->fragRateReference = value;
    return true;
}

bool CCSOLScenarioEliteServices::CaptureContinuation(float now, ConfigError& error)
{
    Call call(*this);
    if (!call.Current() || !error.message.empty()) return false;
    if (!std::isfinite(now)) return Fail(error, "elite continuation clock is invalid");
    //10824FCB: each SSE operation rounds to float before integer truncation.
    const float elapsed = (now - call.state->roundStarted) + static_cast<float>(call.state->savedElapsed);
    call.state->savedElapsed = TruncateClock(elapsed);
    return true;
}

bool CCSOLScenarioEliteServices::AccumulateContinuation(float now, ConfigError& error)
{
    Call call(*this);
    if (!call.Current() || !error.message.empty()) return false;
    if (call.state->savedElapsed <= 0) return true;
    if (!std::isfinite(now)) return Fail(error, "elite continuation clock is invalid");
    //108290C0 adds both the current interval and saved previous duration.
    const float elapsed = ((now - call.state->roundStarted) + call.state->previousRoundDuration) +
        static_cast<float>(call.state->savedElapsed);
    call.state->savedElapsed = TruncateClock(elapsed);
    return true;
}

bool CCSOLScenarioEliteServices::ResetContinuation(ConfigError& error)
{
    Call call(*this);
    if (!call.MapCurrent() || !error.message.empty()) return false;
    //11433E17 clears only the global continuation elapsed, not rules+320.
    call.state->savedElapsed = 0;
    return true;
}
