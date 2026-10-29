// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_monster_boss_control.h"
#include "../scenario/monster_appearances.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>

#pragma clang fp contract(off)

namespace
{
using csol::scenario::BossSkillTimer;
void Start(BossSkillTimer& timer, float duration)
{
    timer.duration = duration;
    timer.deadline = duration + gpGlobals->time;
    timer.startedAt = gpGlobals->time;
}
void Expire(BossSkillTimer& timer) { timer.deadline = timer.startedAt = -1.0f; }
float Square3(const Vector& value) { return (value.y * value.y + value.x * value.x) + value.z * value.z; }
bool Finite(const Vector& value)
{ return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }
struct Busy
{
    bool& value;
    explicit Busy(bool& flag) : value(flag) { value = true; }
    ~Busy() { value = false; }
};
}

CCSOLScenarioMonster* CCSOLMonsterBossControl::Current(CSOLMonsterSpawnCall& call, const StatePtr& state) const
{
    const auto services = m_services.lock();
    if (!services || !services->OwnerCurrent() || !state || state != m_state || !state->spawn ||
        state->lease != state->spawn->lease || !m_skills) return nullptr;
    if (state->row && *state->row >= m_skills->Table().rows.size()) return nullptr;
    auto* monster = call.Current();
    return monster && monster->Behavior() == state->behavior && monster->Pool()->Borrowed(*monster) &&
        monster->Pool()->Lease(*monster) == state->lease ? monster : nullptr;
}

bool CCSOLMonsterBossControl::ResetMotion(CSOLMonsterSpawnCall& call, const StatePtr& state)
{
    auto* monster = Current(call, state);
    if (!monster || !gpGlobals || state->position.startingJump) return false;
    const auto* appearance = csol::scenario::FindMonsterAppearance(monster->Type()->appearance);
    if (!appearance) return false;
    Vector feet = monster->pev->origin;
    feet.z += appearance->feetZ;
    if (!Finite(feet)) return false;
    // Common ResetMotion -> Stop -> MoveTo(feet), then invalidate only X.
    state->position.destination = feet;
    state->position.follower.Reset();
    ++state->position.updateSerial;
    if (!state->knownGoodValid)
    {
        state->knownGoodValid = true;
        if (!(gpGlobals->time <= state->position.jumpUntil) && !state->position.motion.crouching)
        {
            if (gpGlobals->time > state->knownGoodClock.deadline)
            {
                Start(state->knownGoodClock, 1.0f);
                state->previousKnownGood = state->knownGood;
                state->previousKnownGoodValid = true;
            }
            state->knownGood = feet;
        }
    }
    monster->pev->velocity = g_vecZero;
    state->position.motion.moveType = std::min(0, state->position.motion.maxMoveType);
    state->position.destination.x = -99999.0f;
    state->position.motion.velocity = Vector2D(0, 0);
    state->deltaX = state->deltaY = state->motionRemainder = 0;
    return Current(call, state) != nullptr;
}

bool CCSOLMonsterBossControl::Reset(CSOLMonsterSpawnCall& call, CSOLMonsterSpawnState& spawn,
    csol::scenario::ConfigError& error)
{
    const auto keepAlive = shared_from_this();
    const auto services = m_services.lock();
    if (!services || !services->OwnerCurrent() ||
        (m_state && (m_state->busy || m_state->position.startingJump))) return false;
    auto* monster = call.Current();
    if (!monster || !monster->Behavior() || !gpGlobals || !m_skills || !monster->Type() ||
        monster->TypeId() != 30200 || monster->Type()->skill != 1002 || !std::isfinite(gpGlobals->time))
    { error.message = "Oberon skill control requires its live type30200/skill1002 spawn"; return false; }
    // The original explicit reset does not clear the active SK row/end clock.
    // Normal factory creation uses a new Oberon; retain these fields when a
    // caller explicitly resets an existing control instead of fabricating a
    // broader reset. Shared row state is never cloned here.
    if (m_state && m_state->position.updating)
    {
        ++m_state->position.updateSerial;
        m_state->position.follower.Reset();
    }
    auto state = m_state ? std::make_shared<CSOLBossControlState>(*m_state) :
        std::make_shared<CSOLBossControlState>();
    state->spawn = spawn.weak_from_this().lock();
    if (!state->spawn || state->spawn.get() != &spawn || !spawn.lease ||
        spawn.lease != monster->Pool()->Lease(*monster))
    { error.message = "Oberon skill control requires factory-owned spawn state"; return false; }
    state->behavior = monster->Behavior();
    state->lease = spawn.lease;
    state->target = nullptr;
    state->busy = false;
    state->position.updating = state->position.startingJump = false;
    m_state = state;
    monster->HostState().enhancedUntil = -1.0f;
    if (!ResetMotion(call, state))
    { if (Current(call, state)) error.message = "Oberon reset motion failed"; return false; }
    Expire(state->movement); Expire(state->avoidance); Expire(state->path);
    Expire(state->clocks.target); Expire(state->clocks.selection);
    Start(state->effect, 99999.0f);
    Start(state->clocks.startCheck, 99999.0f);
    Start(state->clocks.force, 99999.0f);
    monster->Animation().LockUntil(0);
    state->repeatCount = 0;
    state->launchOriginZ = -999999.0f;
    state->solidAfter = 0;
    // No shared SK cooldown/cache clearing, and no initial sequence selection:
    // the factory writes sequence1 after this behavior-reset stage.
    error = {};
    return Current(call, state) != nullptr;
}

bool CCSOLMonsterBossControl::Select(CSOLMonsterSpawnCall& call, csol::scenario::ConfigError& error)
{
    const auto keepAlive = shared_from_this();
    const auto services = m_services.lock();
    const auto state = m_state;
    auto* monster = Current(call, state);
    if (!services || !monster || state->busy) return false;
    Busy busy(state->busy);
    if (!gpGlobals || gpGlobals->maxClients < 0 || gpGlobals->maxClients > 32 || !g_engfuncs.pfnPEntityOfEntIndex)
    { error.message = "Boss target lookup requires the real client-slot API"; return false; }
    const auto group = m_skills->Table().groups.find(monster->Type()->skill);
    if (group == m_skills->Table().groups.end()) return true;
    const float health = (monster->pev->health / monster->pev->max_health) * 100.0f;
    csol::scenario::BossSkillCandidates candidates;
    std::vector<EHANDLE> targets;
    for (const auto rowIndex : group->second)
    {
        const auto& row = m_skills->Table().rows[rowIndex];
        if (!(gpGlobals->time > m_skills->State(rowIndex)->cooldownDeadline) ||
            !(health >= row.startHealthMin && health <= row.startHealthMax)) continue;
        // maxClients is deliberately re-read as in MP's loop. An engine
        // callback changing it cannot make the next slot escape the CS limit.
        for (int slot = 1; slot <= gpGlobals->maxClients; ++slot)
        {
            if (slot > 32) { error.message = "Boss client-slot limit changed during selection"; return false; }
            auto* edict = INDEXENT(slot);
            monster = Current(call, state);
            if (!monster) return false;
            if (!edict || edict->free) continue;
            auto* player = dynamic_cast<CBasePlayer*>(CBaseEntity::Instance(edict));
            if (!player || static_cast<int>(player->m_iTeam) == monster->pev->team ||
                static_cast<std::uint32_t>(player->pev->flags) == 0x80000000u) continue;
            const EHANDLE handle(player);
            const bool alive = player->IsAlive() != FALSE;
            monster = Current(call, state);
            if (!monster) return false;
            auto* target = handle.Get<CBaseEntity>();
            if (!alive || !target || (target->pev->flags & FL_KILLME)) continue;
            const float distance = Square3(target->pev->origin - monster->pev->origin);
            if (!(distance >= row.startDistanceMinSquared && distance <= row.startDistanceMaxSquared)) continue;
            candidates.entries.push_back({rowIndex, slot});
            targets.push_back(handle);
            if (!candidates.priority)
            {
                candidates.total = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(candidates.total) +
                    static_cast<std::uint32_t>(static_cast<std::int32_t>(row.ratio)));
                if (row.ratio == 255) candidates.priority = candidates.entries.size() - 1;
            }
        }
    }
    const auto random = candidates.NeedsRandom() ? std::optional<std::int32_t>(std::rand()) : std::nullopt;
    std::optional<csol::scenario::BossSkillCandidate> chosen;
    if (!m_skills->Choose(candidates, random, chosen, error)) return false;
    if (!chosen) return true;
    const auto selected = std::find_if(candidates.entries.begin(), candidates.entries.end(), [&](const auto& entry)
        { return entry.row == chosen->row && entry.playerSlot == chosen->playerSlot; });
    const auto& target = targets[static_cast<std::size_t>(selected - candidates.entries.begin())];
    if (!Current(call, state) || !target.Get<CBaseEntity>()) return false;
    state->target = target;
    state->row = chosen->row;
    const auto selectedRow = state->row;
    const auto& row = m_skills->Table().rows[*state->row];
    const float duration = row.forceAttackTime + row.animationTime;
    Start(state->clocks.target, duration);
    monster = Current(call, state);
    if (!monster) return false;
    monster->HostState().activeSkill = row.type;
    // CommitSelection has no callbacks; the shared deadline is written here.
    // Then resample each clock's time in the original engine order.
    csol::scenario::BossSkillClocks unused;
    if (!m_skills->CommitSelection(*state->row, gpGlobals->time, unused)) return false;
    Start(state->clocks.end, duration);
    Start(state->clocks.selection, duration + 0.5f);
    Start(state->clocks.startCheck, -1.0f);
    Start(state->clocks.force, row.forceAttackTime);
    monster->HostState().hasSkillRow = true;
    if (row.startMessage != -1)
    {
        const auto& messages = services->BossMessages();
        if (!Current(call, state) || state->row != selectedRow) return false;
        const bool sent = messages.SendBossSkillStart(row.startMessage);
        if (!Current(call, state) || state->row != selectedRow) return false;
        if (!sent) { error.message = "Boss skill-start message failed"; return false; }
    }
    return true;
}

CSOLBossBeginResult CCSOLMonsterBossControl::TryBegin(CSOLMonsterSpawnCall& call,
    csol::scenario::ConfigError& error)
{
    const auto keepAlive = shared_from_this();
    using Result = CSOLBossBeginResult;
    const auto services = m_services.lock();
    const auto state = m_state;
    auto* monster = Current(call, state);
    if (!services || !monster || state->busy) return Result::Cancelled;
    Busy busy(state->busy);
    if (!gpGlobals) { error.message = "Boss begin has no engine clock"; return Result::Failed; }
    if (!state->clocks.startCheck.Elapsed(gpGlobals->time)) return Result::Waiting;
    Start(state->clocks.startCheck, 0.2f);
    if (!state->row) { Start(state->clocks.startCheck, 99999.0f); return Result::Waiting; }
    const auto selectedRow = state->row;
    const auto current = [&]() -> CCSOLScenarioMonster*
    { return state->row == selectedRow ? Current(call, state) : nullptr; };
    auto* target = state->target.Get<CBaseEntity>();
    if (!target) return Result::Waiting;
    const auto& row = m_skills->Table().rows[*state->row];
    if (row.type < 3 || row.type > 9)
    { error.message = "Oberon begin does not implement this SK type"; return Result::Failed; }
    const float distance = Square3(target->pev->origin - monster->pev->origin);
    if (!(row.approachDistanceSquared >= distance) && !state->clocks.force.Elapsed(gpGlobals->time))
        return Result::Waiting;
    Start(state->clocks.startCheck, 99999.0f);
    Start(state->clocks.force, 99999.0f);
    if (!ResetMotion(call, state))
    {
        if (!Current(call, state)) return Result::Cancelled;
        error.message = "Boss skill begin could not reset motion"; return Result::Failed;
    }
    Start(state->path, row.animationTime);
    if (m_skills->State(*state->row)->sequence == -1)
    {
        if (!g_engfuncs.pfnGetModelPtr)
        { error.message = "Boss sequence model query is unavailable"; return Result::Failed; }
        // The SDK wrapper dereferences the model immediately after the engine
        // callback. Check our borrow before entering the model reader instead.
        void* model = GET_MODEL_PTR(monster->edict());
        monster = current();
        if (!monster) return Result::Cancelled;
        const int sequence = ::LookupSequence(model, row.sequence.c_str());
        m_skills->CacheSequence(*state->row, sequence);
    }
    struct Context
    {
        CCSOLMonsterBossControl* owner;
        CSOLMonsterSpawnCall* call;
        StatePtr state;
        std::optional<std::size_t> row;
    } context{this, &call, state, selectedRow};
    const bool animated = monster->Animation().SetSequenceGuarded(*monster,
        m_skills->State(*state->row)->sequence, true, [](void* value)
        {
            const auto& context = *static_cast<Context*>(value);
            return CSOL_ResetMonsterSequenceInfo(*context.call, [](const void* value)
            {
                const auto& context = *static_cast<const Context*>(value);
                return context.state->row == context.row &&
                    context.owner->Current(*context.call, context.state) != nullptr;
            }, &context);
        }, &context);
    monster = current();
    if (!monster) return Result::Cancelled;
    if (!animated || !monster->Animation().LockUntil(row.animationTime + gpGlobals->time))
    { error.message = "Boss skill animation could not be set"; return Result::Failed; }
    Start(state->effect, row.damageDelay);
    Start(state->clocks.end, row.animationTime);
    Start(state->clocks.selection, row.animationTime + 0.5f);
    target = state->target.Get<CBaseEntity>();
    if (!target) return Result::Cancelled;
    state->position.faceGoal = target->pev->origin;
    state->position.faceRequested = true; // original v24 queues facing for F4
    csol::scenario::ConfigError diagnostic;
    const bool notified = services->RulesSkillStarted(call, diagnostic);
    if (!current()) return Result::Cancelled;
    if (!notified)
    {
        error = std::move(diagnostic);
        if (error.message.empty()) error.message = "Boss skill-start rule service failed";
        return Result::Failed;
    }
    return Result::Started;
}

bool CCSOLMonsterBossControl::Interrupt(CSOLMonsterSpawnCall& call, std::int16_t& interruptState,
    csol::scenario::ConfigError& error)
{
    const auto keepAlive = shared_from_this();
    const auto services = m_services.lock();
    const auto state = m_state;
    auto* monster = Current(call, state);
    if (!services || !monster || state->busy) return false;
    Busy busy(state->busy);
    if (!state->row) return true; // original leaves host1322 intact on this path
    if (!gpGlobals || !ResetMotion(call, state))
    { if (Current(call, state)) error.message = "Boss interruption motion reset failed"; return false; }
    Expire(state->movement); Expire(state->avoidance); Expire(state->path); Expire(state->clocks.target);
    Start(state->effect, 99999.0f); Start(state->clocks.startCheck, 99999.0f); Start(state->clocks.force, 99999.0f);
    state->repeatCount = 0;
    if (interruptState == -1)
    {
        m_skills->Interrupt(*state->row, gpGlobals->time, interruptState);
        monster->Animation().LockUntil(gpGlobals->time);
        Expire(state->clocks.end); Expire(state->clocks.selection);
        monster->HostState().activeSkill = 0;
        monster->HostState().hasSkillRow = false;
        state->row.reset();
    }
    interruptState = 0;
    return Current(call, state) != nullptr;
}

bool CCSOLMonsterBossControl::Finish(CSOLMonsterSpawnCall& call, csol::scenario::ConfigError& error)
{
    const auto keepAlive = shared_from_this();
    const auto services = m_services.lock();
    const auto state = m_state;
    auto* monster = Current(call, state);
    if (!services || !monster || state->busy) return false;
    Busy busy(state->busy);
    if (!gpGlobals) { error.message = "Boss finish has no engine clock"; return false; }
    if (!state->clocks.end.Elapsed(gpGlobals->time)) return true;
    Start(state->clocks.end, 99999.0f);
    Expire(state->movement);
    monster->pev->maxspeed = state->spawn->attributes.speed;
    const auto& type = *monster->Type();
    monster->pev->rendercolor = Vector(type.color.data());
    monster->pev->rendermode = static_cast<int>(type.column21[0]);
    monster->pev->renderfx = static_cast<int>(type.column21[1]);
    monster->pev->renderamt = type.column21[2];
    monster->HostState().activeSkill = 0;
    monster->HostState().hasSkillRow = false;
    state->row.reset();
    state->launchOriginZ = -999999.0f;
    state->repeatCount = 0;
    return true;
}
