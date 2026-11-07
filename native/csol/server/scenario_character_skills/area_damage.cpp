// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "area_damage.hpp"
#include "area_expanded.hpp"
#include "area_rules.hpp"
#include "smoke_zone.hpp"
#include "effects.hpp"
#include "host_notifications.hpp"
#include "../scenario/core/scenario_player_runtime.h"
#include "../scenario/core/scenario_player_impulse.h"
#include "../scenario/monsters/scenario_monster_damage.h"
#include "../scenario/core/scenario_session.h"
#include "../scenario/core/game_rules_gate.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

#pragma clang fp contract(off)
namespace
{
using namespace csol::scenario::skills;
using csol::scenario::ConfigError;
constexpr int SkillDamageBits = 0x01000000;

struct Dispatch
{
    static thread_local Dispatch* active;
    Dispatch* previous{active};
    EHANDLE target, owner;
    std::shared_ptr<const CSOLScenarioEntityAccess> access;
    CCSOLMonsterPool* pool{};
    std::uint64_t lease{};
    int bits{SkillDamageBits};
    CSOLMonsterDeathContext context;
    bool consumed{};
    Dispatch(CCSOLScenarioMonster& receiver, CBasePlayer& player,
        std::shared_ptr<const CSOLScenarioEntityAccess> map, bool lotus = false)
        : target(&receiver), owner(&player), access(std::move(map)),
          pool(receiver.Pool()), lease(pool ? pool->Lease(receiver) : 0)
    {
        context.bytes[20] = 1;
        if (lotus) { bits = 0; context.bytes[23] = 1; }
        active = this;
    }
    ~Dispatch() { active = previous; }
};
thread_local Dispatch* Dispatch::active{};

struct Operation
{
    std::shared_ptr<CCSOLScenarioPlayerRuntime> players;
    std::shared_ptr<CSOLScenarioPlayerRuntimeState> state;
    std::shared_ptr<CCSOLScenarioSession> session;
    std::shared_ptr<CSOLScenarioActiveSkill> skill;
    std::shared_ptr<const CSOLScenarioEntityAccess> access;
    std::uint64_t life{};
    Operation(std::shared_ptr<CCSOLScenarioPlayerRuntime> p,
        std::shared_ptr<CSOLScenarioPlayerRuntimeState> s,
        std::shared_ptr<CCSOLScenarioSession> map)
        : players(std::move(p)), state(std::move(s)), session(std::move(map)),
          skill(state ? state->activeSkill : nullptr),
          access(session ? session->Map().EntityAccess() : nullptr),
          life(state ? state->skillLifeSerial : 0) {}
    bool Current() const
    {
        auto* player = state ? state->player.Get<CBasePlayer>() : nullptr;
        return players && state && session && access && access->Allowed() &&
            session->Map().EntityAccess() == access && players->Active() &&
            players->Find(state->player) == state && state->activeSkill == skill &&
            state->skillLifeSerial == life && player && player->pev &&
            player->pev->deadflag == DEAD_NO && player->pev->health > 0 &&
            !(player->pev->flags & (FL_KILLME | FL_DORMANT));
    }
};

struct PulseTarget
{
    EHANDLE handle;
    CBaseEntity* entity{};
    CCSOLScenarioMonster* monster{};
    CCSOLMonsterPool* pool{};
    std::uint64_t lease{};
    CBasePlayer* player{};
    std::shared_ptr<const CSOLScenarioPlayerRuntimeState> playerState;
    std::uint64_t life{};

    PulseTarget(Operation& operation, CBaseEntity& target)
        : handle(&target), entity(&target), monster(dynamic_cast<CCSOLScenarioMonster*>(&target)),
          pool(monster ? monster->Pool() : nullptr), lease(pool ? pool->Lease(*monster) : 0),
          player(dynamic_cast<CBasePlayer*>(&target)),
          playerState(player ? operation.players->Find(handle) : nullptr),
          life(playerState ? playerState->skillLifeSerial : 0) {}
    bool Current(const Operation& operation) const
    {
        return operation.Current() && handle.Get<CBaseEntity>() == entity && entity->pev &&
            !(entity->pev->flags & (FL_KILLME | FL_DORMANT)) &&
            (!monster || (pool && monster->Pool() == pool && pool->Lease(*monster) == lease)) &&
            (!player || (operation.players->Find(handle) == playerState &&
                (!playerState || playerState->skillLifeSerial == life)));
    }
};

bool RecoverPulseTarget(Operation& operation, const EHANDLE& target,
    float amount, bool sourced, ConfigError& error)
{
    if (!operation.Current()) return false;
    const auto runtime = CSOL_ScenarioRuntimeForAccess(operation.access.get());
    if (!runtime) return false;
    const auto guard = [](const void* value) { return static_cast<const Operation*>(value)->Current(); };
    const auto recover = [&](CSOLPlayerRecoveryCall& call)
    {
        bool applied{};
        const bool result = operation.players->ApplyRecovery(call, amount, applied, error);
        if (!operation.Current()) return false;
        // Removal or respawn of this recipient cancels only its recovery.
        // The outer sphere continues while the original caster is current.
        return !call.Current() && error.message.empty() ? true : result;
    };
    if (sourced)
    {
        CSOLPlayerRecoveryCall call(runtime, target, guard, &operation, operation.state->player);
        return recover(call);
    }
    CSOLPlayerRecoveryCall call(runtime, target, guard, &operation, nullptr);
    return recover(call);
}

int Team(CBaseEntity& entity)
{
    const auto* player = dynamic_cast<CBasePlayer*>(&entity);
    return player ? static_cast<int>(player->m_iTeam) : entity.pev->team;
}

//107D2160,106DE490 and106E7DE0 use the buff recipient as both damage sources,
// including when the original caster has since disconnected or changed class.
bool StatBuffArea(Operation& operation, float radius, float damage,
    float horizontalImpulse, ConfigError& error)
{
    if (!operation.Current() || !error.message.empty()) return false;
    if (!std::isfinite(radius) || radius < 0 || !std::isfinite(damage) ||
        !std::isfinite(horizontalImpulse) || horizontalImpulse < 0 ||
        !gpGlobals || !std::isfinite(gpGlobals->time))
    { error.message = "stat buff area parameters are invalid"; return false; }
    if (!g_engfuncs.pfnFindEntityInSphere)
    { error.message = "stat buff sphere enumeration is unavailable"; return false; }
    edict_t* cursor{};
    while (operation.Current())
    {
        auto* owner = operation.state->player.Get<CBasePlayer>();
        const Vector origin = owner->pev->origin;
        if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z))
        { error.message = "stat buff area origin is invalid"; return false; }
        cursor = FIND_ENTITY_IN_SPHERE(cursor, origin, radius);
        if (!operation.Current()) return false;
        if (FNullEnt(cursor)) return true;
        auto* receiver = CBaseEntity::Instance(cursor);
        if (!receiver || !receiver->pev || (receiver->pev->flags & (FL_KILLME | FL_DORMANT))) continue;
        PulseTarget recipient(operation, *receiver);
        const auto target = recipient.handle;
        auto* monster = recipient.monster;
        const auto current = [&] { return recipient.Current(operation); };
        if (!current()) continue;
        owner = operation.state->player.Get<CBasePlayer>();
        if (receiver->pev->takedamage == DAMAGE_NO || receiver == owner ||
            (Team(*receiver) == owner->m_iTeam && friendlyfire.value == 0)) continue;
        const bool isPlayer = receiver->IsPlayer() != FALSE;
        if (!operation.Current()) return false;
        if (!current()) continue;
        const int classification = receiver->Classify();
        if (!operation.Current()) return false;
        if (!current() || !SkillAreaTargetAllowed(isPlayer, classification, true, true)) continue;
        if (monster)
        {
            Dispatch dispatch(*monster, *owner, operation.access);
            dispatch.bits = 0;
            dispatch.context = {};
            for (const auto offset : {4, 16, 18, 19, 20, 21, 23}) dispatch.context.bytes[offset] = 1;
            receiver->TakeDamage(owner->pev, owner->pev, damage, 0);
        }
        else receiver->TakeDamage(owner->pev, owner->pev, damage, 0);
        if (!operation.Current()) return false;
        if (!current()) continue;
        // Re-read classification after the callback, as the original does.
        const int afterClass = receiver->Classify();
        if (!operation.Current()) return false;
        if (!current()) continue;
        if (horizontalImpulse > 0)
        {
            const bool playerTarget = receiver->IsPlayer() != FALSE;
            if (!operation.Current()) return false;
            if (!current()) continue;
            const auto* type = monster ? monster->Type() : nullptr;
            const bool movable = playerTarget || afterClass == 45 ||
                (afterClass == 17 && type && type->skill < 1000 && type->eventClass <= 149);
            if (!movable) continue;
            const Vector delta = receiver->pev->origin - operation.state->player.Get<CBasePlayer>()->pev->origin;
            const float squared = delta.x * delta.x + delta.y * delta.y;
            if (!std::isfinite(squared) || squared < 0)
            { error.message = "stat buff burst impulse direction is invalid"; return false; }
            const float length = static_cast<float>(std::sqrt(static_cast<double>(squared)));
            const float inverse = length == 0 ? 0 : 1.0f / length;
            const Vector requested((delta.x * inverse) * horizontalImpulse,
                (delta.y * inverse) * horizontalImpulse, 100.0f);
            if (playerTarget)
            {
                bool applied{};
                if (!CSOL_ApplyScenarioPlayerImpulse(operation.session, target, requested, applied, error)) return false;
                if (!operation.Current()) return false;
                if (!current()) continue;
            }
            else
            {
                //114ECF00 with weapon=-1 skips the weapon>=536 impulse
                // accumulator. The verified scenario rules accept this vector.
                // Class45 additionally requires its original10A6CD30 consumer.
                if (afterClass == 45)
                { error.message = "stat buff burst class45 impulse consumer is unavailable"; return false; }
                receiver->pev->velocity = requested;
            }
        }
        if (afterClass == 17 && monster)
        {
            if (!std::isfinite(gpGlobals->time))
            { error.message = "stat buff hit clock is invalid"; return false; }
            // CMonster+12B8 gates F4 movement at113F1D30. This is not
            // the unrelated scenario manager's status-replay field at+12B8.
            monster->HostState().moveAfter = gpGlobals->time + 1.0f;
        }
    }
    return false;
}

bool Area(Operation& operation, const Vector& origin, float radius,
    float center, float edge, bool ordinaryOnly, ConfigError& error,
    bool radialFalloff = true, bool includeProps = true, int bits = SkillDamageBits,
    bool flatSkillContext = false, bool expandedEndContext = false)
{
    if (!operation.Current()) return false;
    if (!std::isfinite(radius) || !std::isfinite(center) || !std::isfinite(edge) || radius <= 0)
    { error.message = "character area attack parameters are invalid"; return false; }
    edict_t* cursor{};
    while (operation.Current())
    {
        // The engine iterator advances by edict index. A damage callback may
        // synchronously remove the C++ entity or recycle its monster lease;
        // neither event invalidates this edict cursor for the next search.
        cursor = FIND_ENTITY_IN_SPHERE(cursor, origin, radius);
        if (!operation.Current()) return false;
        if (FNullEnt(cursor)) return true;
        auto* receiver = CBaseEntity::Instance(cursor);
        if (!receiver || !receiver->pev || (receiver->pev->flags & FL_KILLME)) continue;
        const EHANDLE target(receiver);
        auto* monster = dynamic_cast<CCSOLScenarioMonster*>(receiver);
        auto* pool = monster ? monster->Pool() : nullptr;
        const auto lease = pool ? pool->Lease(*monster) : 0;
        const auto valid = [&]
        {
            return operation.Current() && target.Get<CBaseEntity>() == receiver &&
                !(receiver->pev->flags & FL_KILLME) &&
                (!monster || (pool && monster->Pool() == pool && pool->Lease(*monster) == lease));
        };
        if (!operation.Current()) return false;
        if (!valid()) continue;
        if (receiver->pev->takedamage == DAMAGE_NO) continue;
        auto* owner = operation.state->player.Get<CBasePlayer>();
        if (receiver == owner || (Team(*receiver) == owner->m_iTeam && friendlyfire.value == 0)) continue;
        const int classification = receiver->Classify();
        if (!operation.Current()) return false;
        if (!valid()) continue;
        if (ordinaryOnly)
        { if (classification != 17) continue; }
        else
        {
            const bool isPlayer = receiver->IsPlayer();
            if (!operation.Current()) return false;
            if (!valid()) continue;
            if (!SkillAreaTargetAllowed(isPlayer, classification, includeProps, true)) continue;
        }
        const auto delta = receiver->pev->origin - origin;
        const float squared = (delta.y * delta.y + delta.x * delta.x) + delta.z * delta.z;
        const float damage = radialFalloff ? SkillAreaDamage(squared, radius, center, edge) : center;
        if (!expandedEndContext && !(damage > 0)) continue;
        if (!std::isfinite(damage))
        { error.message = "character area attack produced invalid damage"; return false; }
        owner = operation.state->player.Get<CBasePlayer>();
        if (monster)
        {
            // The normal monster virtual consumes this context exactly once
            // before its behavior callback. Other/recursive hits cannot inherit.
            Dispatch dispatch(*monster, *owner, operation.access);
            dispatch.bits = bits;
            // Type39/49 set both original context bytes; older radial skills
            // and the independent smoke-zone producer keep their own flags.
            if (flatSkillContext) dispatch.context.bytes[16] = 1;
            if (expandedEndContext)
            {
                //106E75D9..7618 initializes a distinct damage context. It
                // must not inherit the ordinary active-weapon kill callback.
                dispatch.context = {};
                for (const auto offset : {4, 16, 18, 19, 20, 21})
                    dispatch.context.bytes[offset] = 1;
            }
            receiver->TakeDamage(owner->pev, owner->pev, damage, bits);
        }
        else receiver->TakeDamage(owner->pev, owner->pev, damage, bits);
        if (!operation.Current()) return false;
    }
    return false;
}
}

std::optional<CSOLMonsterDeathContext> CSOL_ConsumeCharacterSkillDamageContext(
    CCSOLScenarioMonster& target, entvars_t* inflictor, entvars_t* attacker, int bits)
{
    if (auto context = CSOL_ConsumeSmokeSkillDamageContext(target, inflictor, attacker, bits))
        return context;
    auto* scope = Dispatch::active;
    if (!scope || scope->consumed || bits != scope->bits || !scope->access ||
        !scope->access->Allowed() || scope->target.Get<CBaseEntity>() != &target ||
        !scope->pool || target.Pool() != scope->pool || scope->pool->Lease(target) != scope->lease)
        return {};
    auto* owner = scope->owner.Get<CBasePlayer>();
    if (!owner || owner->pev != inflictor || owner->pev != attacker) return {};
    scope->consumed = true;
    return scope->context;
}

namespace csol::scenario::skills
{
bool ApplyCharacterTargetDamage(
    const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session, const EHANDLE& target,
    float damage, int bits, const CSOLMonsterDeathContext& context, ConfigError& error)
{
    Operation operation(players, state, session);
    if (!operation.Current() || !error.message.empty()) return false;
    if (!std::isfinite(damage))
    { error.message = "character target damage is not finite"; return false; }
    auto* receiver = target.Get<CBaseEntity>();
    if (!receiver || !receiver->pev || (receiver->pev->flags & (FL_KILLME | FL_DORMANT))) return true;
    PulseTarget recipient(operation, *receiver);
    if (!recipient.Current(operation)) return operation.Current();
    auto* owner = state->player.Get<CBasePlayer>();
    if (recipient.monster)
    {
        // Consume through the same one-shot scope as all existing area hits.
        // Nested weapon damage cannot inherit this skill's kill context.
        Dispatch dispatch(*recipient.monster, *owner, operation.access);
        dispatch.bits = bits;
        dispatch.context = context;
        receiver->TakeDamage(owner->pev, owner->pev, damage, bits);
    }
    else receiver->TakeDamage(owner->pev, owner->pev, damage, bits);
    // No target access after TakeDamage: death can recycle its private object
    // or pool lease while leaving the original player's skill current.
    return operation.Current();
}

bool StartDrainArea37(
    const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session, ConfigError& error)
{
    Operation operation(players, state, session);
    if (!operation.Current() || !error.message.empty()) return false;
    if (!g_engfuncs.pfnFindEntityInSphere)
    { error.message = "drain support sphere enumeration is unavailable"; return false; }
    //106ECCE0 first collects the complete target list, then damages it. Its
    // initial player v128 only returns origin and has no observable effect.
    std::vector<PulseTarget> targets;
    edict_t* cursor{};
    for (;;)
    {
        const Vector origin = state->player.Get<CBasePlayer>()->pev->origin;
        if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z))
        { error.message = "drain support origin is invalid"; return false; }
        cursor = FIND_ENTITY_IN_SPHERE(cursor, origin, 8.0f * 39.37f);
        if (!operation.Current()) return false;
        if (FNullEnt(cursor)) break;
        auto* receiver = CBaseEntity::Instance(cursor);
        if (!receiver || !receiver->pev || (receiver->pev->flags & (FL_KILLME | FL_DORMANT))) continue;
        PulseTarget target(operation, *receiver);
        if (!target.Current(operation)) continue;
        const bool alive = receiver->IsAlive() != FALSE;
        if (!operation.Current()) return false;
        if (!target.Current(operation) || !alive || receiver->pev->takedamage == DAMAGE_NO) continue;
        auto* owner = state->player.Get<CBasePlayer>();
        if (receiver == owner || (Team(*receiver) == owner->m_iTeam && friendlyfire.value == 0)) continue;
        const bool playerTarget = receiver->IsPlayer() != FALSE;
        if (!operation.Current()) return false;
        if (!target.Current(operation)) continue;
        const int classification = playerTarget ? 0 : receiver->Classify();
        if (!operation.Current()) return false;
        if (target.Current(operation) && SkillAreaTargetAllowed(playerTarget, classification, false, true))
            targets.push_back(std::move(target));
    }
    float total{};
    for (const auto& target : targets)
    {
        if (!operation.Current()) return false;
        if (!target.Current(operation)) continue;
        auto* owner = state->player.Get<CBasePlayer>();
        total += 10000.0f; // Original sequential float sum, before each call.
        if (target.monster)
        {
            Dispatch dispatch(*target.monster, *owner, operation.access);
            dispatch.bits = 0;
            dispatch.context = {};
            for (const auto offset : {4, 16, 18, 19, 20, 21, 23}) dispatch.context.bytes[offset] = 1;
            target.entity->TakeDamage(owner->pev, owner->pev, 10000.0f, 0);
        }
        else target.entity->TakeDamage(owner->pev, owner->pev, 10000.0f, 0);
    }
    if (!operation.Current()) return false;
    // This subtype uses the caster's pev as recovery source, including zero
    // targets. The two periodic drain subtypes deliberately use null instead.
    return RecoverPulseTarget(operation, state->player, total * 0.0025f, true, error);
}

bool ActiveDrainAreaSkill32_33(int subtype,
    const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session, ConfigError& error)
{
    Operation operation(players, state, session);
    if (!operation.Current() || !error.message.empty()) return false;
    if (subtype != 32 && subtype != 33)
    { error.message = "drain pulse subtype is invalid"; return false; }
    if (!g_engfuncs.pfnFindEntityInSphere)
    { error.message = "drain pulse sphere enumeration is unavailable"; return false; }
    auto* owner = state->player.Get<CBasePlayer>();
    Vector origin = owner->pev->origin;
    //1133CE00, used once per pulse. Targets are enumerated against their
    // origins; do not subtract the standing/ducking offset from both sides.
    origin.z -= (owner->pev->flags & FL_DUCKING) ? 18.0f : 36.0f;
    if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z))
    { error.message = "drain pulse origin is invalid"; return false; }
    const float damage = subtype == 32 ? 400.0f : 300.0f;
    int hits{};
    edict_t* cursor{};
    for (;;)
    {
        cursor = FIND_ENTITY_IN_SPHERE(cursor, origin, 8.0f * 39.37f);
        if (!operation.Current()) return false;
        if (FNullEnt(cursor)) break;
        auto* receiver = CBaseEntity::Instance(cursor);
        if (!receiver || !receiver->pev || (receiver->pev->flags & (FL_KILLME | FL_DORMANT))) continue;
        PulseTarget target(operation, *receiver);
        if (!target.Current(operation)) continue;
        bool playerTarget{};
        if (subtype == 33)
        {
            playerTarget = receiver->IsPlayer() != FALSE;
            if (!operation.Current()) return false;
            if (!target.Current(operation)) continue;
            if (playerTarget)
            {
                const bool alive = receiver->IsAlive() != FALSE;
                if (!operation.Current()) return false;
                if (!target.Current(operation)) continue;
                //106F0D12 has no team filter, and runs before enemy damage.
                if (alive && !RecoverPulseTarget(operation, target.handle, 85.0f, true, error)) return false;
                if (!target.Current(operation)) continue;
            }
        }
        const bool alive = receiver->IsAlive() != FALSE;
        if (!operation.Current()) return false;
        if (!target.Current(operation) || !alive || receiver->pev->takedamage == DAMAGE_NO) continue;
        owner = state->player.Get<CBasePlayer>();
        if (receiver == owner || (Team(*receiver) == owner->m_iTeam && friendlyfire.value == 0)) continue;
        if (subtype == 32)
        {
            playerTarget = receiver->IsPlayer() != FALSE;
            if (!operation.Current()) return false;
            if (!target.Current(operation)) continue;
        }
        const int classification = playerTarget ? 0 : receiver->Classify();
        if (!operation.Current()) return false;
        if (!target.Current(operation) || !SkillAreaTargetAllowed(playerTarget, classification, false, true)) continue;
        if (target.monster)
        {
            Dispatch dispatch(*target.monster, *owner, operation.access);
            dispatch.context = {};
            for (const auto offset : {4, 16, 18, 19, 20, 21}) dispatch.context.bytes[offset] = 1;
            receiver->TakeDamage(owner->pev, owner->pev, damage, SkillDamageBits);
        }
        else receiver->TakeDamage(owner->pev, owner->pev, damage, SkillDamageBits);
        if (!operation.Current()) return false;
        ++hits; // Original counts damage calls, not TakeDamage's return value.
        if (subtype == 33 && !RecoverPulseTarget(operation, state->player, damage * 0.05f, false, error)) return false;
    }
    if (subtype == 32 && hits > 0)
        return RecoverPulseTarget(operation, state->player, (static_cast<float>(hits) * damage) * 0.05f, false, error);
    return operation.Current();
}

bool EndDragonShieldArea53(
    const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session, ConfigError& error)
{
    Operation operation(players, state, session);
    return StatBuffArea(operation, 4.0f * 39.37f, 3000.0f, 500.0f, error);
}

bool EndGuardBurst51(
    const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session,
    float accumulatedDamage, ConfigError& error)
{
    Operation operation(players, state, session);
    if (!operation.Current() || !error.message.empty()) return false;
    const double truncated = std::trunc(static_cast<double>(accumulatedDamage));
    if (!std::isfinite(truncated) || truncated < std::numeric_limits<int>::min() ||
        truncated > std::numeric_limits<int>::max())
    { error.message = "guard burst accumulated damage is out of integer range"; return false; }
    //106E7E2F..7F8B truncates before the500 floor, then applies the
    // difficulty coefficient and20000 ceiling. Invalid rows use the last row.
    constexpr std::array<float, 7> factors{1, 1.5f, 2, 3, 5, 10, 15};
    const int row = session->Difficulty().SelectedRow();
    const float factor = row >= 1 && row <= 7 ? factors[row - 1] : factors.back();
    const float damage = std::min(static_cast<float>(std::max(static_cast<int>(truncated), 500)) * factor, 20000.0f);
    return StatBuffArea(operation, 12.0f * 39.37f, damage, 300.0f, error);
}

bool ApplyPeriodicBuffAreaDamage(
    const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session,
    float radius, float damage, ConfigError& error)
{
    Operation operation(players, state, session);
    return StatBuffArea(operation, radius, damage, 0, error);
}

bool ActiveExpandedAreaSkill36(
    const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session,
    float accumulatedDamage, ConfigError& error)
{
    Operation operation(players, state, session);
    if (!error.message.empty() || !operation.Current()) return false;
    const float total = accumulatedDamage * 6.0f;
    if (!std::isfinite(total))
    { error.message = "skill36 accumulated damage is invalid"; return false; }
    const Vector origin = state->player.Get<CBasePlayer>()->pev->origin;
    if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z))
    { error.message = "skill36 area origin is invalid"; return false; }
    struct Target
    {
        EHANDLE handle;
        CBaseEntity* entity{};
        CCSOLScenarioMonster* monster{};
        CCSOLMonsterPool* pool{};
        std::uint64_t lease{};
        bool Current() const
        {
            return handle.Get<CBaseEntity>() == entity && entity->pev &&
                !(entity->pev->flags & FL_KILLME) &&
                (!monster || (pool && monster->Pool() == pool && pool->Lease(*monster) == lease));
        }
    };
    std::vector<Target> targets;
    edict_t* cursor{};
    for (;;)
    {
        cursor = FIND_ENTITY_IN_SPHERE(cursor, origin, 2.0f * 39.37f);
        if (!operation.Current()) return false;
        if (FNullEnt(cursor)) break;
        auto* receiver = CBaseEntity::Instance(cursor);
        if (!receiver || !receiver->pev || (receiver->pev->flags & FL_KILLME)) continue;
        auto* monster = dynamic_cast<CCSOLScenarioMonster*>(receiver);
        auto* pool = monster ? monster->Pool() : nullptr;
        Target target{EHANDLE(receiver), receiver, monster, pool, pool ? pool->Lease(*monster) : 0};
        if (!target.Current()) continue;
        const bool alive = receiver->IsAlive() != FALSE;
        if (!operation.Current()) return false;
        if (!target.Current() || !alive || receiver->pev->takedamage == DAMAGE_NO) continue;
        auto* owner = state->player.Get<CBasePlayer>();
        if (receiver == owner || (Team(*receiver) == owner->m_iTeam && friendlyfire.value == 0)) continue;
        const bool player = receiver->IsPlayer() != FALSE;
        if (!operation.Current()) return false;
        if (!target.Current()) continue;
        const int classification = player ? 0 : receiver->Classify();
        if (!operation.Current()) return false;
        if (!target.Current() || !SkillAreaTargetAllowed(player, classification, false, true)) continue;
        targets.push_back(target);
    }
    //106F1593..15BF divides by the complete original target count. A target
    // removed by an earlier hit does not redistribute its share to survivors.
    if (targets.empty()) return true;
    const float damage = (total > 3000.0f ? 3000.0f : total) / static_cast<float>(targets.size());
    for (const auto& target : targets)
    {
        if (!operation.Current()) return false;
        if (!target.Current()) continue;
        auto* owner = state->player.Get<CBasePlayer>();
        if (target.monster)
        {
            Dispatch dispatch(*target.monster, *owner, operation.access);
            dispatch.bits = 0;
            dispatch.context = {};
            for (const auto offset : {4, 16, 18, 19, 20, 21, 23}) dispatch.context.bytes[offset] = 1;
            target.entity->TakeDamage(owner->pev, owner->pev, damage, 0);
        }
        else target.entity->TakeDamage(owner->pev, owner->pev, damage, 0);
        if (!operation.Current()) return false;
    }
    return true;
}

bool EndExpandedAreaSkill46(
    const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session,
    float accumulatedDamage, ConfigError& error)
{
    Operation operation(players, state, session);
    if (!error.message.empty() || !operation.Current()) return false;
    const float scaled = accumulatedDamage * 0.5f;
    if (!std::isfinite(scaled))
    { error.message = "expanded skill accumulated damage is invalid"; return false; }
    //106E747F..7502 caps only the upper bound. A zero value still reaches
    // the target's damage callback; do not substitute the radial skill gate.
    const float damage = scaled > 60000.0f ? 60000.0f : scaled;
    const Vector origin = state->player.Get<CBasePlayer>()->pev->origin;
    return Area(operation, origin, 8.0f * 39.37f, damage, damage, false,
        error, false, false, 0, false, true);
}

bool ApplyFlatCharacterAreaDamage(
    const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session,
    const Vector& origin, float radius, float damage, int bits, ConfigError& error)
{
    Operation operation(players, state, session);
    if (!error.message.empty() || !operation.Current()) return false;
    if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z))
    { error.message = "character area attack origin is invalid"; return false; }
    return Area(operation, origin, radius, damage, damage, false, error, false, false, bits, true);
}

bool ActiveLotusSkill(const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session, LotusState& lotus, ConfigError& error)
{
    Operation operation(players, state, session);
    if (!operation.Current() || !error.message.empty()) return false;
    // The active host owns duration. A new life cannot replay the preceding
    // life's saved field even if it retains the same skill/cooldown object.
    if (!lotus.started || lotus.life != state->skillLifeSerial) return true;
    if (!gpGlobals || !std::isfinite(gpGlobals->time))
    { error.message = "lotus pulse clock is invalid"; return false; }
    const float now = gpGlobals->time;
    if (lotus.nextPulse > now) return true;
    lotus.nextPulse = now + 0.5f; // equality fires; no catch-up loop.
    const Vector origin(lotus.origin[0], lotus.origin[1], lotus.origin[2]);
    constexpr float radius = 6.0f * 39.37f;
    edict_t* cursor{};
    while (operation.Current())
    {
        cursor = FIND_ENTITY_IN_SPHERE(cursor, origin, radius);
        if (!operation.Current()) return false;
        if (FNullEnt(cursor)) return true;
        auto* receiver = CBaseEntity::Instance(cursor);
        if (!receiver || !receiver->pev ||
            (receiver->pev->flags & (FL_KILLME | FL_DORMANT))) continue;
        PulseTarget recipient(operation, *receiver);
        const auto target = recipient.handle;
        auto* monster = recipient.monster;
        auto* pool = recipient.pool;
        const auto current = [&] { return recipient.Current(operation); };
        const bool alive = receiver->IsAlive();
        if (!operation.Current()) return false;
        if (!current() || !alive || receiver->pev->takedamage == DAMAGE_NO) continue;
        auto* actor = state->player.Get<CBasePlayer>();
        if (receiver == actor || (Team(*receiver) == actor->m_iTeam && friendlyfire.value == 0)) continue;
        const bool isPlayer = receiver->IsPlayer();
        if (!operation.Current()) return false;
        if (!current()) continue;
        const int classification = receiver->Classify();
        if (!operation.Current()) return false;
        if (!current() || !SkillAreaTargetAllowed(isPlayer, classification, false, true)) continue;
        const auto* type = monster ? monster->Type() : nullptr;
        const bool knockback = isPlayer || classification == 45 ||
            (classification == 17 && type && LotusMonsterKnockback(type->skill, type->eventClass, type->id));
        const float damage = knockback ? lotus.values.ordinaryDamage : lotus.values.fixedDamage;
        if (!std::isfinite(damage) || damage <= 0)
        { error.message = "lotus pulse damage is invalid"; return false; }
        if (monster)
        {
            Dispatch dispatch(*monster, *actor, operation.access, true);
            receiver->TakeDamage(actor->pev, actor->pev, damage, 0);
        }
        else receiver->TakeDamage(actor->pev, actor->pev, damage, 0);
        if (!operation.Current()) return false;
        // A killed or recycled recipient must neither acquire postmortem
        // movement nor abort the remaining area targets. Check the lease
        // before reading any recipient field following its damage callback.
        if (!knockback || !current() || receiver->pev->deadflag != DEAD_NO ||
            receiver->pev->health <= 0 || (monster && !pool->Borrowed(*monster))) continue;
        if (!isPlayer && !monster) continue; // Class45 has its own impulse consumer.
        const auto delta = receiver->pev->origin - origin;
        const float squared = delta.y * delta.y + delta.x * delta.x;
        if (!std::isfinite(squared) || squared < 0)
        { error.message = "lotus pulse direction is invalid"; return false; }
        const float length = static_cast<float>(std::sqrt(static_cast<double>(squared)));
        const float inverse = length == 0 ? 0 : 1.0f / length;
        const Vector requested((delta.x * inverse) * 1350.0f,
            (delta.y * inverse) * 1350.0f, 200.0f);
        if (isPlayer)
        {
            //113E77C0 forwards weapon=-1 to the player's rules and StatBuff
            //impulse path. The recipient's life was captured before damage.
            bool applied{};
            if (!CSOL_ApplyScenarioPlayerImpulse(session, target, requested, applied, error) ||
                !operation.Current()) return false;
        }
        else
        {
            monster->SpeedState().impulse = Vector2D(requested.x, requested.y);
            monster->pev->velocity.z = requested.z;
            monster->SpeedState().impulseUntil = now + 0.2f;
        }
    }
    return false;
}

bool StartCharacterAreaDamage(int subtype,
    const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session, ConfigError& error)
{
    if (subtype != 7 && subtype != 18 && subtype != 28)
    { error.message = "character area attack subtype is unsupported"; return false; }
    Operation operation(players, state, session);
    if (!operation.Current()) return false;
    const Vector origin = state->player.Get<CBasePlayer>()->pev->origin;
    if (subtype == 7 || subtype == 18)
    {
        // 106EFC60 checks the native form flag and water contents. The other
        // area skill does not inherit this gate or the Classify17 restriction.
        if (state->profile.weaponEffectsBlocked21F2) return true;
        const int contents = POINT_CONTENTS(origin);
        if (!operation.Current()) return false;
        if (contents == CONTENTS_WATER) return true;
        return subtype == 7 ? Area(operation, origin, 150, 32000, 3000, true, error) :
            Area(operation, origin, 170, 25000, 3000, true, error);
    }
    return Area(operation, origin, 10.0f * 39.37f, 11000, 3000, false, error);
}

bool ApplyLoveBombDamage(const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session,
    DamageState& effect, DamageHost& recovery, CSOLMonsterDamageCall& call,
    float damage, ConfigError& error)
{
    Operation operation(players, state, session);
    if (!operation.Current() || !call.Current() || !recovery.Current() || !error.message.empty()) return false;
    bool active{};
    if (!recovery.Active(active, error) || !operation.Current() || !recovery.Current()) return false;
    if (!active) return true;
    const float recovered = damage * 0.01f;
    if (!std::isfinite(recovered))
    { error.message = "love bomb recovery is invalid"; return false; }
    // Recovery is before cooldown, source and target checks in106E22F0.
    if (!recovery.RecoverSelf(recovered, error) || !operation.Current() || !call.Current() || !recovery.Current()) return false;
    if (!gpGlobals || !std::isfinite(gpGlobals->time))
    { error.message = "love bomb clock is unavailable"; return false; }
    if (!(effect.nextArea28 < gpGlobals->time) || !call.Inflictor()) return true;
    auto* victim = call.Current();
    if (!victim || !victim->pev || (victim->pev->flags & FL_DORMANT)) return true;
    const Vector origin = victim->pev->origin;
    effect.nextArea28 = gpGlobals->time + 2.0f;
    float attack{};
    if (!players->ReadUpgradeStat(state->player, csol::scenario::UpgradeStat::Damage, attack) ||
        !operation.Current()) return false;
    if (!Area(operation, origin, 8.0f * 39.37f, 250.0f * attack,
        240.0f * attack, false, error)) return false;
    // The triggering victim can legitimately die in the nested area hit.
    // Its captured position remains the explosion origin; its old damage
    // call must not cancel the remaining recipients or this visual.
    return PublishLoveBombExplosion(*session, {origin.x, origin.y, origin.z}, error) && operation.Current();
}

bool ApplyDragonBreathDamage(const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session,
    DamageState& effect, CSOLMonsterDamageCall& call, ConfigError& error)
{
    Operation operation(players, state, session);
    if (!operation.Current() || !call.Current() || !error.message.empty()) return false;
    if (!gpGlobals || !std::isfinite(gpGlobals->time))
    { error.message = "dragon breath clock is unavailable"; return false; }
    // 106E2C50 is called only after the active-duration query. Its own +30
    // clock persists across activations; unlike subtype28, v44 does not reset.
    if (!(effect.nextArea12 < gpGlobals->time) || !call.Inflictor()) return true;
    const Vector origin = call.Current()->pev->origin;
    effect.nextArea12 = gpGlobals->time + 1.0f;
    float attack{};
    if (!players->ReadUpgradeStat(state->player, csol::scenario::UpgradeStat::Damage, attack) ||
        !operation.Current()) return false;
    if (!Area(operation, origin, 8.0f * 39.37f, 2180.0f * attack,
        1500.0f * attack, false, error)) return false;
    return PublishLoveBombExplosion(*session, {origin.x, origin.y, origin.z}, error) && operation.Current();
}
}
