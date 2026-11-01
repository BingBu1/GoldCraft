// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "elite_manager.hpp"
#include "area_damage.hpp"
#include "host_notifications.hpp"
#include "../scenario_gameplay.h"
#include "../scenario_runtime.h"
#include "../scenario_player_runtime.h"
#include "../scenario_entity_lookup.h"
#include "../../scenario/monster_appearances.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

#pragma clang fp contract(off)

namespace
{
using csol::scenario::ConfigError;
bool Fail(ConfigError& error, const char* message) { error.message = message; return false; }
bool Alive(CCSOLScenarioMonster& monster)
{ return monster.pev && monster.pev->deadflag == DEAD_NO && monster.pev->health > 0; }
}

struct CCSOLScenarioEliteManager::Call
{
    CCSOLScenarioEliteManager& owner;
    std::shared_ptr<CCSOLScenarioRuntime> runtime;
    std::shared_ptr<CCSOLScenarioSession> session;
    std::shared_ptr<State> state;
    std::uint64_t revision{};
    explicit Call(CCSOLScenarioEliteManager& value)
        : owner(value), runtime(value.m_runtime.lock()),
          session(runtime ? runtime->session : nullptr), state(value.m_state),
          revision(state ? state->revision : 0) {}
    bool Current() const
    {
        return runtime && !runtime->Closing() && runtime->eliteManager.get() == &owner &&
            session && runtime->session == session && owner.m_state == state && state &&
            state->revision == revision && state->access && state->access->Allowed() &&
            session->Map().EntityAccess() == state->access &&
            session->Map().RoundGeneration() == state->round;
    }
    void Changed() { revision = ++state->revision; }
};

CCSOLScenarioEliteManager::Target::Target(CCSOLScenarioMonster& monster)
    : handle(&monster), pool(monster.Pool()), lease(pool ? pool->Lease(monster) : 0) {}

CCSOLScenarioMonster* CCSOLScenarioEliteManager::Target::Resolve() const
{
    auto* monster = handle.Get<CCSOLScenarioMonster>();
    return monster && monster->pev && !(monster->pev->flags & (FL_KILLME | FL_DORMANT)) &&
        pool && monster->Pool() == pool && lease && pool->Lease(*monster) == lease ? monster : nullptr;
}

CCSOLScenarioEliteManager::CCSOLScenarioEliteManager(std::weak_ptr<CCSOLScenarioRuntime> runtime)
    : m_runtime(std::move(runtime)), m_state(std::make_shared<State>()) {}

bool CCSOLScenarioEliteManager::Current() const
{
    const auto runtime = m_runtime.lock();
    const auto state = m_state;
    return runtime && !runtime->Closing() && runtime->eliteManager.get() == this && runtime->session &&
        state && state->access && state->access->Allowed() &&
        runtime->session->Map().EntityAccess() == state->access &&
        runtime->session->Map().RoundGeneration() == state->round;
}

CCSOLScenarioMonster* CCSOLScenarioEliteManager::Primary() const
{ return Current() ? m_state->primary.Resolve() : nullptr; }

bool CCSOLScenarioEliteManager::FollowerSpawn() const
{ return Current() && m_state->followerSpawn; }

bool CCSOLScenarioEliteManager::Disabled() const
{ return Current() && m_state->disabled; }

bool CCSOLScenarioEliteManager::WebActive() const
{ return Current() && m_state->webActive; }

bool CCSOLScenarioEliteManager::SetWebActive(bool active)
{
    if (!Current()) return false;
    m_state->webActive = active;
    return true;
}

void CCSOLScenarioEliteManager::Invalidate()
{ m_state.reset(); }

bool CCSOLScenarioEliteManager::ResetRound(float now, float spawnInterval, ConfigError& error)
{
    const auto runtime = m_runtime.lock();
    if (!runtime || runtime->Closing() || runtime->eliteManager.get() != this ||
        !runtime->session || !runtime->session->Map().EntityAccessAllowed()) return false;
    const float deadline = (now + 60.0f) - spawnInterval;
    if (!std::isfinite(now) || !std::isfinite(spawnInterval) || !std::isfinite(deadline))
        return Fail(error, "elite manager round clock is invalid");
    auto next = std::make_shared<State>();
    next->access = runtime->session->Map().EntityAccess();
    next->round = runtime->session->Map().RoundGeneration();
    next->lastSpawnCheck = deadline;
    if (m_state)
    {
        next->executions = m_state->executions;
        next->failedSpawns = m_state->failedSpawns;
        next->successfulSpawns = m_state->successfulSpawns;
        //10969D00 preserves byte135 outside creation. A host callback may
        // replace the round during a factory call; that cancelled call's
        // temporary follower flag must not escape into the new round.
        next->followerSpawn = !m_state->spawnGate && m_state->followerSpawn;
    }
    m_state = std::move(next);
    return true;
}

bool CCSOLScenarioEliteManager::Kill(const EHANDLE& target,
    const CSOLMonsterDeathArguments& arguments, ConfigError& error)
{
    Call call(*this);
    if (!call.Current() || !error.message.empty()) return false;
    auto* monster = target.Get<CCSOLScenarioMonster>();
    if (!monster || !monster->pev || (monster->pev->flags & (FL_KILLME | FL_DORMANT))) return true;
    const Target pinned(*monster);
    if (!pinned.Resolve()) return true;
    const auto gameplay = call.runtime->gameplay;
    if (!gameplay || !gameplay->Active()) return Fail(error, "elite monster death owner is unavailable");
    auto& requests = gameplay->Deaths().Requests();
    const auto result = requests.Request(*monster, arguments);
    // Death normally clears the primary and changes the manager revision.
    // Only map/round/owner retirement cancels this successful request.
    if (!Current() || m_state != call.state || call.runtime->gameplay != gameplay) return false;
    if (result == CSOLDeathRequestResult::Failed)
    { error.message = requests.Error(); return false; }
    return true;
}

bool CCSOLScenarioEliteManager::ClearFollowers(Call& call, ConfigError& error, bool requireType)
{
    // Copy records, never keep a vector iterator across a death callback.
    const auto followers = call.state->followers;
    for (const auto& target : followers)
    {
        if (!call.Current()) return false;
        auto* monster = target.Resolve();
        if (!monster || !Alive(*monster) || (requireType && !monster->Type())) continue;
        CSOLMonsterDeathArguments arguments;
        arguments.suppressSound = true;
        if (!Kill(target.handle, arguments, error) || !call.Current()) return false;
    }
    call.state->followers.clear();
    call.state->pendingCleanup = false;
    call.Changed();
    return true;
}

bool CCSOLScenarioEliteManager::SetPrimary(const EHANDLE& target, ConfigError& error)
{
    Call call(*this);
    if (!call.Current() || !error.message.empty()) return false;
    auto* monster = target.Get<CCSOLScenarioMonster>();
    if (!monster || !monster->pev || (monster->pev->flags & (FL_KILLME | FL_DORMANT))) return true;
    const Target next(*monster);
    if (!next.Resolve()) return true;
    //10969E30 installs104 BEFORE killing the old followers.
    call.state->primary = next;
    call.Changed();
    if (!ClearFollowers(call, error, true) || !call.Current()) return false;
    monster = next.Resolve();
    if (!monster || !Alive(*monster)) return true;
    if (!monster->Type() || !g_engfuncs.pfnIndexOfEdict)
        return Fail(error, "elite primary notification identity is unavailable");
    const int index = ENTINDEX(monster->edict());
    if (!call.Current() || next.Resolve() != monster) return false;
    const float maximum = monster->pev->max_health;
    if (!std::isfinite(maximum) || maximum < static_cast<double>(std::numeric_limits<int>::min()) ||
        maximum > static_cast<double>(std::numeric_limits<int>::max()) || index <= 0)
        return Fail(error, "elite primary notification values are invalid");
    // The original type+4 short is the appearance catalog index; type+0
    // is32800 and must not be sent as the HUD appearance.
    const auto* appearance = csol::scenario::FindMonsterAppearance(monster->Type()->appearance);
    if (!appearance) return Fail(error, "elite primary appearance is unavailable");
    const bool sent = call.session->Messages().SendBossHealthInit(appearance->index, static_cast<int>(maximum), index);
    if (!call.Current()) return false;
    return sent || Fail(error, "elite primary notification failed");
}

bool CCSOLScenarioEliteManager::AddFollower(const EHANDLE& target, ConfigError& error)
{
    Call call(*this);
    if (!call.Current() || !error.message.empty()) return false;
    auto* monster = target.Get<CCSOLScenarioMonster>();
    if (!monster || !monster->pev || (monster->pev->flags & (FL_KILLME | FL_DORMANT))) return true;
    const Target next(*monster);
    if (!next.Resolve()) return true;
    //10969DA0 appends, including duplicate handles.
    call.state->followers.push_back(next);
    call.Changed();
    return true;
}

bool CCSOLScenarioEliteManager::Spawn(bool follower, int pointGroup,
    std::shared_ptr<const csol::scenario::MonsterType> type, ConfigError& error)
{
    Call call(*this);
    if (!call.Current() || !error.message.empty()) return false;
    if (pointGroup <= 0) return true;
    if (!type || type->id != 32800)
        return Fail(error, "elite spawn requires its synthetic type32800 record");
    const auto gameplay = call.runtime->gameplay;
    if (!gameplay || !gameplay->Active()) return Fail(error, "elite spawn factory is unavailable");
    if (call.state->spawnGate) return Fail(error, "elite spawn reentered its factory transaction");
    struct SpawnScope
    {
        State& state;
        explicit SpawnScope(State& value) : state(value) { state.spawnGate = true; }
        ~SpawnScope() { state.spawnGate = false; }
    } scope(*call.state);
    if (!follower && call.state->primary.Resolve())
    {
        CSOLMonsterDeathArguments arguments;
        arguments.suppressSound = true;
        if (!Kill(call.state->primary.handle, arguments, error) || !Current() || m_state != call.state ||
            call.runtime->gameplay != gameplay) return false;
        // The old primary's real death intentionally mutates manager state.
        call.revision = call.state->revision;
    }
    struct FollowerScope
    {
        State& state;
        bool previous;
        FollowerScope(State& value, bool follower) : state(value), previous(value.followerSpawn)
        { if (follower) state.followerSpawn = true; }
        ~FollowerScope() { state.followerSpawn = previous; }
    };
    EHANDLE created;
    CSOLMonsterSpawnResult result;
    {
        const FollowerScope scope(*call.state, follower);
        result = gameplay->Factory().SpawnElite(std::move(type), pointGroup, follower, created);
    }
    if (!Current() || m_state != call.state || call.runtime->gameplay != gameplay) return false;
    if (result == CSOLMonsterSpawnResult::Cancelled) return false;
    if (result == CSOLMonsterSpawnResult::Failed)
    {
        error = gameplay->Factory().Error();
        return false;
    }
    if (result == CSOLMonsterSpawnResult::Skipped) return true;
    return follower ? AddFollower(created, error) : SetPrimary(created, error);
}

bool CCSOLScenarioEliteManager::RecordExecution(std::int32_t localIdentity, ConfigError& error)
{
    if (!Current() || !error.message.empty()) return false;
    if (localIdentity <= 0) return Fail(error, "elite execution local identity is invalid");
    m_state->executions.insert(localIdentity);
    return true;
}

std::int32_t CCSOLScenarioEliteManager::ExecutionCount(std::int32_t localIdentity) const
{
    const auto found = m_executionCounts.find(localIdentity);
    return found == m_executionCounts.end() ? 0 : found->second;
}

bool CCSOLScenarioEliteManager::PrimaryDied(const EHANDLE& victim, const EHANDLE& attacker,
    ConfigError& error)
{
    Call call(*this);
    if (!call.Current() || !error.message.empty() || !gpGlobals) return false;
    // A caller may pass the manager's own handle by reference.
    const EHANDLE victimHandle(victim);
    const EHANDLE attackerHandle(attacker);
    call.state->primary = {};
    call.Changed();
    auto* source = attackerHandle.Get<CBaseEntity>();
    if (source && source->pev && !(source->pev->flags & (FL_KILLME | FL_DORMANT)))
    {
        //10825490 increments once per set member, even if the killing source
        // differs from the player who previously recorded an execution.
        for (const auto key : call.state->executions)
        {
            auto& count = m_executionCounts[key];
            count = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(count) + 1u);
        }
    }
    call.state->executions.clear();
    auto* monster = victimHandle.Get<CCSOLScenarioMonster>();
    if (monster && monster->pev && !(monster->pev->flags & (FL_KILLME | FL_DORMANT)))
    {
        if (!monster->Type()) return Fail(error, "elite death type is unavailable");
        const auto* appearance = csol::scenario::FindMonsterAppearance(monster->Type()->appearance);
        if (!appearance) return Fail(error, "elite death appearance is unavailable");
        if (!csol::scenario::skills::PublishEliteDeath(*call.session, appearance->index, error) || !call.Current()) return false;
    }
    call.state->pendingCleanup = true;
    call.state->lastSpawnCheck = gpGlobals->time;
    return true;
}

bool CCSOLScenarioEliteManager::FlushFollowers(ConfigError& error)
{
    Call call(*this);
    if (!call.Current() || !error.message.empty()) return false;
    return !call.state->pendingCleanup || ClearFollowers(call, error);
}

bool CCSOLScenarioEliteManager::UpdateSpawnSchedule(const SpawnClock& clock,
    SpawnEvent event, void* context, ConfigError& error)
{
    Call call(*this);
    if (!call.Current() || !error.message.empty() || !gpGlobals) return false;
    //1096A100 drains pending followers even when later spawn gates are shut.
    if (call.state->scheduleGate) return Fail(error, "elite schedule reentered its update");
    struct ScheduleScope
    {
        State& state;
        explicit ScheduleScope(State& value) : state(value) { state.scheduleGate = true; }
        ~ScheduleScope() { state.scheduleGate = false; }
    } scope(*call.state);
    if (call.state->pendingCleanup && !ClearFollowers(call, error)) return false;
    if (!call.Current()) return false;
    if (call.session->Difficulty().SelectedRow() < 2 || !clock.enabled || call.state->disabled) return true;
    const float now = gpGlobals->time;
    if (!std::isfinite(now)) return Fail(error, "elite spawn clock is invalid");
    if (!(now > call.state->lastSpawnCheck + OriginalSpawnInterval)) return true;
    call.state->lastSpawnCheck = now;
    if (gpGlobals->maxClients < 0 || gpGlobals->maxClients > 32)
        return Fail(error, "elite spawn player capacity is invalid");
    std::int32_t total{};
    for (int index = 1; index <= gpGlobals->maxClients; ++index)
    {
        auto* entity = CSOL_ScenarioPlayerEntity(index, call.state->access);
        if (!call.Current()) return false;
        auto* player = dynamic_cast<CBasePlayer*>(entity);
        if (!player || !player->pev || player->has_disconnected ||
            (player->pev->flags & (FL_KILLME | FL_DORMANT))) continue;
        // Original converts the integer accumulator to float and truncates
        // after EACH addition, rather than truncating one floating sum.
        const float sum = static_cast<float>(total) + player->pev->frags;
        if (!std::isfinite(sum) || sum < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
            sum > static_cast<double>(std::numeric_limits<std::int32_t>::max()))
            return Fail(error, "elite spawn frag accumulation overflowed");
        total = static_cast<std::int32_t>(sum);
    }
    //11EF5650 default-1 (10141F60). An existing main target, even dead,
    // blocks a new attempt until the actual elite death stage clears it.
    if (!(static_cast<float>(total) > -1.0f) || call.state->primary.Resolve()) return true;
    if (!std::isfinite(clock.elapsed) || !std::isfinite(clock.fragRateReference))
        return Fail(error, "elite spawn requires the authoritative elapsed/rate state");
    const float epochs = clock.elapsed / 180.0f; //11EF55C0,101425A0.
    if (epochs < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
        epochs > static_cast<double>(std::numeric_limits<std::int32_t>::max()))
        return Fail(error, "elite spawn elapsed epoch overflowed");
    const auto epoch = static_cast<std::int32_t>(epochs);
    const auto debt = std::bit_cast<std::int32_t>(
        static_cast<std::uint32_t>(call.state->successfulSpawns) - static_cast<std::uint32_t>(epoch));
    //11EF54E8=7.5,11EF5758=2.5. The original x87 product+sum rounds once.
    float chance = static_cast<float>(2.5 * static_cast<float>(call.state->failedSpawns) + 7.5);
    const float surplus = call.state->successfulSpawns > 0 && debt > 0 ? static_cast<float>(debt) * -7.0f : 0;
    chance = surplus + chance;
    const float rate = (clock.elapsed == 0 ? 0 : static_cast<float>(total) / clock.elapsed) / clock.fragRateReference;
    float rateChance{};
    // rules+320 is initialized to ZERO at10820967. Its IEEE division may
    // produce infinity or NaN:1096A517's JB skips the rate term for unordered
    // values, while positive infinity is clamped to5. Neither is an error.
    if (rate >= 1.0f)
    {
        // Original clamps to settings11EF5530=1 and11EF57A0=5, then
        // multiplies11EF5608=0. Do not replace the zero reference with1.
        const float clamped = std::min(5.0f, std::max(1.0f, rate));
        rateChance = clamped * 0.0f;
    }
    //1096A67A draws before adding/scaling the final threshold.
    if (!g_engfuncs.pfnRandomLong) return Fail(error, "elite spawn random source is unavailable");
    const int roll = RANDOM_LONG(0, 9999);
    if (!call.Current()) return false;
    const float threshold = (rateChance + chance) * 100.0f;
    if (!std::isfinite(threshold) || threshold < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
        threshold > static_cast<double>(std::numeric_limits<std::int32_t>::max()))
        return Fail(error, "elite spawn probability overflowed");
    if (roll >= static_cast<std::int32_t>(threshold))
    {
        call.state->failedSpawns = std::bit_cast<std::int32_t>(
            static_cast<std::uint32_t>(call.state->failedSpawns) + 1u);
        return true;
    }
    if (!event) return Fail(error, "elite spawn command dispatch is unavailable");
    if (!event(context, error)) return false;
    // A successful dispatch normally SetPrimary's a new entity, changing
    // revision. Preserve it, but never write into a replaced round/session.
    if (!Current() || m_state != call.state) return false;
    call.state->successfulSpawns = std::bit_cast<std::int32_t>(
        static_cast<std::uint32_t>(call.state->successfulSpawns) + 1u);
    call.state->failedSpawns = 0;
    return true;
}

bool CCSOLScenarioEliteManager::ForwardFollowerDamage(const EHANDLE& target,
    const EHANDLE& inflictor, const EHANDLE& attacker, float damage, ConfigError& error)
{
    Call call(*this);
    if (!call.Current() || !error.message.empty()) return false;
    auto* follower = target.Get<CCSOLScenarioMonster>();
    if (!follower || !follower->pev || (follower->pev->flags & (FL_KILLME | FL_DORMANT)) ||
        call.state->followers.empty()) return true;
    auto* primary = call.state->primary.Resolve();
    if (!primary || primary == follower) return true;
    //10969C30 tests list non-emptiness, not membership or alive status. The
    // verified108257C0 rules branch selects Classify17/type32800 and requires
    // a resolved player attacker before calling this manager.
    const float remaining = primary->pev->health - damage;
    if (!std::isfinite(damage) || !std::isfinite(remaining) || !std::isfinite(follower->pev->max_health))
        return Fail(error, "elite forwarded damage is invalid");
    primary->pev->health = remaining;
    follower->pev->health = follower->pev->max_health;
    if (remaining > 0) return true;
    CSOLMonsterDeathArguments arguments;
    arguments.inflictor = inflictor;
    arguments.attacker = attacker;
    return Kill(call.state->primary.handle, arguments, error);
}

bool CCSOLScenarioEliteManager::SetDisabled(bool disabled, bool killPrimary, ConfigError& error)
{
    Call call(*this);
    if (!call.Current() || !error.message.empty()) return false;
    call.state->disabled = disabled;
    if (!disabled || !killPrimary) return true;
    auto* primary = call.state->primary.Resolve();
    if (!primary) return true;
    primary->pev->health = 0;
    //10969760 uses null sources but does not suppress the death sound.
    return Kill(call.state->primary.handle, {}, error);
}

namespace csol::scenario::skills
{
bool PrepareEliteSkill21(const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session,
    const std::shared_ptr<CCSOLScenarioEliteManager>& elites,
    const ActivationState& timers, bool& prepared, ConfigError& error)
{
    prepared = false;
    if (!players || !state || !session || !elites || !gpGlobals || !error.message.empty()) return false;
    const auto access = session->Map().EntityAccess();
    if (!access || !access->Allowed() || !elites->Current()) return false;
    const auto skill = state->activeSkill;
    const auto life = state->skillLifeSerial;
    auto* actor = state->player.Get<CBasePlayer>();
    const auto current = [&]
    {
        return access && access->Allowed() && session->Map().EntityAccess() == access &&
            players->Active() && players->Find(state->player) == state &&
            state->activeSkill == skill && state->skillLifeSerial == life &&
            state->player.Get<CBasePlayer>() == actor && actor && actor->pev &&
            !(actor->pev->flags & (FL_KILLME | FL_DORMANT)) && elites->Current();
    };
    if (!current()) return false;
    auto* target = elites->Primary();
    if (!target) return true;
    const EHANDLE handle(target);
    auto* pool = target->Pool();
    const auto lease = pool ? pool->Lease(*target) : 0;
    const auto targetCurrent = [&]
    {
        return current() && handle.Get<CCSOLScenarioMonster>() == target &&
            elites->Primary() == target && pool && target->Pool() == pool && pool->Lease(*target) == lease;
    };
    const auto delta = target->pev->origin - actor->pev->origin;
    const float squared = (delta.y * delta.y + delta.x * delta.x) + delta.z * delta.z;
    const float radius = 15.0f * 39.37f;
    if (!std::isfinite(squared)) return Fail(error, "elite skill target distance is invalid");
    if (squared > radius * radius) return true;
    const float duration = timers.effects.duration;
    const float expires = duration < 0 ? -1 : gpGlobals->time + duration;
    if (!std::isfinite(duration) || !std::isfinite(expires)) return Fail(error, "elite skill duration is invalid");
    CSOLScenarioStatBuff buff;
    buff.group = 3; buff.source = 10; buff.subsource = 21;
    buff.expires = expires; buff.attackRatio = .35f;
    if (!players->InstallStatBuff(state->player, std::move(buff)) || !targetCurrent()) return false;
    if (!PublishStatusDuration(*session, state->player, 28, duration, {255,255,255}, error) || !targetCurrent()) return false;
    if (!g_engfuncs.pfnRandomLong) return Fail(error, "elite skill random source is unavailable");
    const int roll = RANDOM_LONG(0, 99);
    if (!targetCurrent()) return false;
    if (roll < 50)
    {
        std::int32_t localIdentity{};
        if (!players->ReadLocalIdentity(state->player, localIdentity, error) || !targetCurrent()) return false;
        if (!elites->RecordExecution(localIdentity, error)) return false;
        CSOLMonsterDeathArguments arguments;
        arguments.inflictor = state->player;
        arguments.attacker = state->player;
        if (!elites->Kill(handle, arguments, error) || !current()) return false;
    }
    else
    {
        // The original rounds30/100 to float before multiplying max_health.
        const float damage = (30.0f / 100.0f) * target->pev->max_health;
        CSOLMonsterDeathContext context;
        context.bytes[16] = 1;
        context.bytes[20] = 1;
        if (!ApplyCharacterTargetDamage(players, state, session, handle, damage, 0, context, error) || !current()) return false;
    }
    prepared = true;
    return true;
}
}
