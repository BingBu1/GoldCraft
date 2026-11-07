// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "effects.hpp"
#include "area_damage.hpp"
#include "area_expanded.hpp"
#include "host_notifications.hpp"
#include "../scenario/core/scenario_player_runtime.h"
#include "../scenario/core/scenario_session.h"
#include "../scenario/core/scenario_runtime.h"
#include "../scenario/core/scenario_death_owner.h"
#include "../scenario/core/scenario_entity_lookup.h"
#include "../scenario/monsters/scenario_monster_damage.h"
#include "../weapons/weapons.h"
#include "../scenario/core/game_rules_gate.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace csol::scenario::skills
{
namespace
{
struct EffectOwner
{
    const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players;
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state;
    const std::shared_ptr<CCSOLScenarioSession>& session;
    std::shared_ptr<CSOLScenarioActiveSkill> skill;
    std::shared_ptr<const CSOLScenarioEntityAccess> access;
    std::uint64_t life{};
    EffectOwner(const std::shared_ptr<CCSOLScenarioPlayerRuntime>& p,
        const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& s,
        const std::shared_ptr<CCSOLScenarioSession>& map)
        : players(p), state(s), session(map), skill(s ? s->activeSkill : nullptr),
          access(map ? map->Map().EntityAccess() : nullptr),
          life(s ? s->skillLifeSerial : 0) {}
    bool Current(bool requireAlive = true) const
    {
        auto* player = state ? state->player.Get<CBasePlayer>() : nullptr;
        return players && state && session && access && access->Allowed() &&
            session->Map().EntityAccess() == access &&
            players->Active() && players->Find(state->player) == state &&
            state->activeSkill == skill && state->skillLifeSerial == life && player &&
            player->pev && (!requireAlive || (player->pev->deadflag == DEAD_NO && player->pev->health > 0)) &&
            !(player->pev->flags & (FL_KILLME | FL_DORMANT));
    }
};

// Original 106DCDA0 checks the actual inflictor's weapon identity, or the
// owner's active knife when the inflictor is that owner. It does not count
// attacks: StatBuff+110=25 selects a status label, not a use limit.
class KnifeCondition final : public CSOLScenarioBuffCondition
{
public:
    bool Evaluate(CSOLMonsterDamageCall& call, bool& applies, ConfigError& error) override
    {
        applies = false;
        if (!call.Current()) return false;
        auto* source = call.Inflictor();
        if (!source || !source->pev) return true;
        if (!g_engfuncs.pfnEntOffsetOfPEntity)
        { error.message = "character knife condition source API is unavailable"; return false; }
        const EHANDLE handle(source);
        const int offset = OFFSET(source->edict());
        if (!call.Current() || handle.Get<CBaseEntity>() != source) return call.Cancel();
        if (!offset) return true;
        int id{};
        if (const auto* weapon = dynamic_cast<CBasePlayerWeapon*>(source)) id = weapon->m_iId;
        else if (const auto* projectile = dynamic_cast<CSOLWeaponDamageSource*>(source)) id = projectile->DamageWeaponId();
        if (!call.Current() || handle.Get<CBaseEntity>() != source) return call.Cancel();
        if (id == WEAPON_KNIFE) { applies = true; return true; }
        auto* owner = call.Player();
        if (owner != source || !owner) return true;
        const auto* active = dynamic_cast<CBasePlayerWeapon*>(owner->m_pActiveItem);
        applies = active && active->pev && !(active->pev->flags & FL_KILLME) && active->m_iId == WEAPON_KNIFE;
        return true;
    }
};

// MP106E9E10/106EE3C0 use seven difficulty cases, with the last value also
// serving as the out-of-range fallback. These are finite shield capacities,
// independent of the caster's invulnerability StatBuff.
int SupportShieldCapacity(int subtype, int difficulty)
{
    constexpr std::array<int, 7> pink{3000, 3000, 3000, 3000, 3800, 4000, 4000};
    constexpr std::array<int, 7> emi{1500, 2500, 5000, 5500, 6000, 6500, 7000};
    constexpr std::array<int, 7> tender{1600, 2000, 2400, 2800, 3200, 3600, 4000};
    const auto& values = subtype == 20 ? pink : subtype == 40 ? tender : emi;
    return difficulty >= 1 && difficulty <= 7 ? values[difficulty - 1] : values.back();
}

bool StartSupportSkill(int subtype, EffectOwner& owner, ActivationState& timers,
    DamageState& damageState, std::uint64_t identity, ConfigError& error, bool* incomingUpgraded)
{
    #pragma clang fp contract(off)
    if (!owner.Current() || !gpGlobals || !error.message.empty()) return false;
    const auto& players = owner.players;
    const auto& state = owner.state;
    const auto& session = owner.session;
    auto* actor = state->player.Get<CBasePlayer>();
    if (subtype == 43 || subtype == 44 || subtype == 45 || subtype == 46 || subtype == 49 || subtype == 50) timers.effects.duration = 8;
    if (subtype == 38) timers.effects.duration = 6;
    const float duration = timers.effects.duration;
    const float now = gpGlobals->time;
    const float expires = duration < 0 ? -1 : now + duration;
    if (!std::isfinite(now) || !std::isfinite(duration) || !std::isfinite(expires))
    { error.message = "support character skill duration is invalid"; return false; }
    const Vector origin = actor->pev->origin;
    const auto makeBuff = [&]
    {
        CSOLScenarioStatBuff buff;
        buff.group = 3; buff.source = 10; buff.subsource = subtype; buff.expires = expires;
        buff.skillIdentity = identity;
        return buff;
    };
    const auto install = [&](const EHANDLE& handle, CSOLScenarioStatBuff buff)
    {
        const auto token = players->InstallStatBuff(handle, std::move(buff));
        if (!token && owner.Current()) error.message = "support character stat buff could not be installed";
        return token;
    };
    if (subtype == 19)
    {
        // 106E9730 directly adds half maximum health, with only an upper
        // clamp. It deliberately does not enter TakeHealth/recovery hooks.
        const float maximum = actor->pev->max_health;
        const float health = actor->pev->health + maximum * 0.5f;
        if (!std::isfinite(maximum) || !std::isfinite(health))
        { error.message = "mercenary character health is invalid"; return false; }
        actor->pev->health = health > maximum ? maximum : health;
        auto buff = makeBuff();
        buff.protection = true;
        if (!install(state->player, std::move(buff)) || !owner.Current()) return false;
        return PublishStatusDuration(*session, state->player, 40, duration,
            {255,255,255}, error) && owner.Current();
    }
    if (subtype == 34 || subtype == 48 || subtype == 49 || subtype == 50)
    {
        auto buff = makeBuff();
        buff.attackRatio = subtype == 34 ? 0.15f : subtype == 48 ? 0.35f : 0.3f;
        const auto token = install(state->player, std::move(buff));
        if (!token || !owner.Current()) return false;
        if (subtype != 34 &&
            (!players->HoldSkillProtectionRender(state->player, token, error) || !owner.Current())) return false;
        if ((subtype == 49 || subtype == 50) && incomingUpgraded) *incomingUpgraded = false;
    }
    if (subtype == 31)
    {
        if (!identity || identity > INT_MAX)
        { error.message = "character recovery buff identity is exhausted"; return false; }
        auto protection = makeBuff();
        protection.protection = true; protection.mask = 1; protection.expires = now + 5;
        const auto token = install(state->player, std::move(protection));
        if (!token || !owner.Current()) return false;
        damageState.installedBuffId = static_cast<int>(identity);
        if (!players->HoldSkillProtectionRender(state->player, token, error) || !owner.Current()) return false;
    }
    if (subtype == 47)
    {
        auto protection = makeBuff();
        protection.protection = true;
        const auto token = install(state->player, std::move(protection));
        if (!token || !owner.Current() ||
            !players->HoldSkillProtectionRender(state->player, token, error) || !owner.Current()) return false;
        // 106EE3C0 snapshots maximum health at activation. It installs a
        // periodic StatBuff, not the separately ratio-based healing ability.
        auto healing = makeBuff();
        healing.periodicRecovery = actor->pev->max_health * 0.03f;
        healing.periodicInterval = 1;
        if (!install(state->player, std::move(healing)) || !owner.Current()) return false;
        if (!PublishStatusDuration(*session, state->player, 27, duration,
            {255,255,255}, error) || !owner.Current()) return false;
    }

    const float radius = subtype == 31 ? 6.0f * 39.37f : subtype == 34 ? 200.0f :
        (subtype == 38 || subtype == 40 || subtype == 48 || subtype == 49 || subtype == 50 ? 8.0f : 10.0f) * 39.37f;
    //106E0214 multiplies metres squared by the stored conversion constant.
    const float radiusSquared = subtype == 38 ? (8.0f * 8.0f) * 1549.996826171875f : radius * radius;
    const int shield = subtype == 43 ? 2000 : subtype == 45 ? 3500 :
        SupportShieldCapacity(subtype, session->Difficulty().SelectedRow());
    for (int index = 1; index <= gpGlobals->maxClients; ++index)
    {
        auto* player = UTIL_PlayerByIndex(index);
        if (!owner.Current()) return false;
        if (!player || !player->pev || player->has_disconnected ||
            (player->pev->flags & (FL_DORMANT | FL_KILLME))) continue;
        const EHANDLE handle(player);
        const auto target = players->Find(handle);
        if (!target) continue;
        const auto life = target->skillLifeSerial;
        const auto current = [&]
        { return owner.Current() && handle.Get<CBasePlayer>() == player &&
            players->Find(handle) == target && target->skillLifeSerial == life; };
        // Type20's original callback intentionally has no IsAlive test.
        if (subtype != 20)
        {
            const bool alive = player->IsAlive();
            if (!owner.Current()) return false;
            if (!current() || !alive) continue;
        }
        const bool self = player == actor;
        if ((subtype == 31 || subtype == 47 || subtype == 48 || subtype == 49) && self) continue;
        // Type20/34 read the caster origin for each callback;47/48 capture it
        // before enumeration. MP player v128 (10612440) returns pev->origin.
        const auto delta = player->pev->origin -
            ((subtype == 20 || subtype == 34 || subtype == 38) ? actor->pev->origin : origin);
        const float squared = (delta.y * delta.y + delta.x * delta.x) + delta.z * delta.z;
        if (squared > radiusSquared) continue;
        if (subtype == 20)
        {
            if (!PublishStatusDuration(*session, handle, 40, 13, {255,255,255}, error)) return false;
            if (!owner.Current()) return false;
            if (!current()) continue;
            if (self)
            {
                auto buff = makeBuff(); buff.protection = true;
                if (!install(handle, std::move(buff)) || !owner.Current()) return false;
            }
            else if (!players->InstallSkillShield(handle, shield, 10, error)) return false;
            if (!owner.Current()) return false;
            if (!current()) continue;
            auto speed = makeBuff();
            speed.absoluteSpeed = 390;
            speed.expires = now + 10;
            const auto token = install(handle, std::move(speed));
            if (!token || !owner.Current()) return false;
            if (!players->InstallSkillLabel(handle, 2, 0, 10, token))
            { error.message = "pink character speed label could not be installed"; return false; }
            if (!owner.Current()) return false;
            if (!current()) continue;
            player->ResetMaxSpeed();
            if (!owner.Current()) return false;
        }
        else if (subtype == 31)
        {
            auto protection = makeBuff();
            protection.skillIdentity = 0; // Original only tags the owner's buff.
            protection.protection = true; protection.mask = 1; protection.expires = now + 2;
            const auto token = install(handle, std::move(protection));
            if (!token || !owner.Current()) return false;
            if (!players->HoldSkillProtectionRender(handle, token, error)) return false;
            if (!owner.Current()) return false;
            if (!current()) continue;
            if (!PublishStatusDuration(*session, handle, 40, 2, {255,255,255}, error)) return false;
            if (!owner.Current()) return false;
        }
        else if (subtype == 34)
        {
            auto buff = makeBuff();
            // Original+109/+10C expands capacity by4x, giving5x total.
            // It does not enable the independent infinite-reserve flag.
            buff.extraClip = 4;
            const auto token = install(handle, std::move(buff));
            if (!token || !owner.Current()) return false;
            if (!players->InstallSkillLabel(handle, 15, 0, duration, token))
            { error.message = "deepsea character ammunition label could not be installed"; return false; }
            if (!owner.Current()) return false;
            if (!current()) continue;
            if (!players->RefreshSkillAmmo(handle, true, false, error)) return false;
            if (!owner.Current()) return false;
            if (!current()) continue;
            if (!PublishAmmoDuration(*session, handle, duration, error)) return false;
            if (!owner.Current()) return false;
        }
        else if (subtype == 38)
        {
            const float targetDuration = self ? 6.0f : 3.0f;
            auto buff = makeBuff();
            buff.speed = 70; buff.protection = true; buff.expires = now + targetDuration;
            const auto token = install(handle, std::move(buff));
            if (!token || !owner.Current() ||
                !players->HoldSkillProtectionRender(handle, token, error) || !owner.Current()) return false;
            if (!current()) continue;
            if (!players->InstallSkillLabel(handle, 2, 0, targetDuration, token))
            { error.message = "character rally speed label could not be installed"; return false; }
            player->ResetMaxSpeed();
            if (!owner.Current()) return false;
            if (!current()) continue;
            if (!PublishStatusDuration(*session, handle, 40, targetDuration, {255,255,255}, error) || !owner.Current()) return false;
        }
        else if (subtype == 40 || subtype == 41 || subtype == 42)
        {
            // Both callbacks are installed on every recipient in MP, but
            // compare the recipient against the captured caster before
            // doing any work. Only the caster needs a retirement tag here.
            const EnergyShield::Callback callback(self ? subtype : 0, identity);
            if (subtype == 42)
            {
                auto healing = makeBuff();
                healing.expires = now + 8;
                healing.periodicRecovery = player->pev->max_health * 0.04f;
                healing.periodicInterval = 1;
                if (!install(handle, std::move(healing)) || !owner.Current()) return false;
            }
            else if (subtype == 41)
            {
                auto ammo = makeBuff(); ammo.expires = now + 8; ammo.extraClip = 2;
                const auto token = install(handle, std::move(ammo));
                if (!token || !owner.Current() ||
                    !players->RefreshSkillAmmo(handle, true, false, error) || !owner.Current()) return false;
            }
            if (subtype == 40 &&
                (!players->InstallSkillShield(handle, shield, 10, error, callback) || !owner.Current())) return false;
            if (!current()) continue;
            if (!PublishStatusDuration(*session, handle, 30, subtype == 40 ? 10 : 8,
                {255,255,255}, error) || !owner.Current()) return false;
            if (!current()) continue;
            if (subtype == 41 || subtype == 42)
            {
                if (!players->InstallSkillShield(handle, 2000, 8, error, callback) || !owner.Current()) return false;
            }
            else
            {
                auto speed = makeBuff(); speed.speed = 110; speed.expires = now + 10;
                const auto token = install(handle, std::move(speed));
                if (!token || !owner.Current() ||
                    !players->HoldSkillProtectionRender(handle, token, error) || !owner.Current()) return false;
                if (!current()) continue;
                player->ResetMaxSpeed();
                if (!owner.Current()) return false;
            }
        }
        else if (subtype == 43 || subtype == 45)
        {
            //106DC7A0: fixed-capacity shield, then the owner's key14 cloak
            // and protection, followed by a separate additive speed buff.
            if (!players->InstallSkillShield(handle, shield, duration, error) || !owner.Current()) return false;
            if (!current()) continue;
            if (subtype == 43 && self &&
                (!players->StartCloakSkill(handle, 14, identity, error, 43) || !owner.Current())) return false;
            if (!current()) continue;
            auto speed = makeBuff();
            speed.skillIdentity = 0; // The cloak ending removes only its tagged protection.
            speed.speed = 50;
            const auto token = install(handle, std::move(speed));
            if (!token || !owner.Current()) return false;
            if (!players->InstallSkillLabel(handle, 2, 0, duration, token))
            { error.message = "character escort speed label could not be installed"; return false; }
            if (!owner.Current()) return false;
            if (!current()) continue;
            player->ResetMaxSpeed();
            if (!owner.Current()) return false;
            if (!current()) continue;
            if (!PublishStatusDuration(*session, handle, 30, duration, {255,255,255}, error) || !owner.Current()) return false;
        }
        else if (subtype == 44)
        {
            //106E1090 keeps attack, clip expansion and the caster's mask0
            // protection on the same group3 item, tagged with this skill ID.
            auto buff = makeBuff();
            buff.attackRatio = 0.25f; buff.extraClip = 2; buff.protection = self;
            const auto token = install(handle, std::move(buff));
            if (!token || !owner.Current()) return false;
            if (self && (!players->HoldSkillProtectionRender(handle, token, error) || !owner.Current())) return false;
            if (!current()) continue;
            if (!players->InstallSkillLabel(handle, 0, 0, duration, token) ||
                !players->InstallSkillLabel(handle, 15, 0, duration, token))
            { error.message = "character escort attack/ammunition labels could not be installed"; return false; }
            if (!players->RefreshSkillAmmo(handle, true, false, error) || !owner.Current()) return false;
            if (!current()) continue;
            if (!PublishStatusDuration(*session, handle, 28, duration, {255,255,255}, error) || !owner.Current()) return false;
        }
        else if (subtype == 46)
        {
            //106DB8B0 receives the captured origin and10m radius from
            //106EDD00. It tags every recipient's35% attack buff.
            auto buff = makeBuff(); buff.attackRatio = 0.35f;
            const auto token = install(handle, std::move(buff));
            if (!token || !owner.Current()) return false;
            if (!players->InstallSkillLabel(handle, 0, 0, duration, token))
            { error.message = "character stored-attack label could not be installed"; return false; }
            if (!current()) continue;
            if (!PublishStatusDuration(*session, handle, 28, duration, {255,255,255}, error) || !owner.Current()) return false;
        }
        else if (subtype == 49 || subtype == 50)
        {
            if (!self)
            {
                auto attack = makeBuff();
                attack.skillIdentity = 0;
                attack.attackRatio = 0.2f;
                const auto token = install(handle, std::move(attack));
                if (!token || !owner.Current() ||
                    !players->HoldSkillProtectionRender(handle, token, error) || !owner.Current()) return false;
                if (!current()) continue;
                if (!PublishStatusDuration(*session, handle, 28, duration, {255,255,255}, error) || !owner.Current()) return false;
                if (!current()) continue;
            }
            if (subtype == 50)
            {
                const auto runtime = CSOL_ScenarioRuntimeForAccess(owner.access.get());
                if (!runtime || !owner.Current()) return false;
                CSOLPlayerRecoveryCall call(runtime, handle,
                    [](const void* value) { return static_cast<const EffectOwner*>(value)->Current(); }, &owner,
                    handle); // v4C receives the recipient's own pev.
                bool applied{};
                if (!players->ApplyRecovery(call, 300, applied, error) || !owner.Current()) return false;
            }
        }
        else if (subtype == 47)
        {
            if (!players->InstallSkillShield(handle, shield, duration, error)) return false;
            if (!owner.Current()) return false;
            if (!current()) continue;
            if (!PublishStatusDuration(*session, handle, 30, duration, {255,255,255}, error)) return false;
            if (!owner.Current()) return false;
        }
        else if (subtype == 48)
        {
            auto buff = makeBuff(); buff.attackRatio = 0.3f;
            const auto token = install(handle, std::move(buff));
            if (!token || !owner.Current()) return false;
            if (!players->HoldSkillProtectionRender(handle, token, error)) return false;
            if (!owner.Current()) return false;
            if (!current()) continue;
            if (!PublishStatusDuration(*session, handle, 28, duration, {255,255,255}, error)) return false;
            if (!owner.Current()) return false;
        }
    }
    if (subtype == 41 || subtype == 42)
        return PublishShieldSkillEffect(*session, state->player, subtype, error) && owner.Current();
    if (subtype == 43 || subtype == 44)
        return PublishEscortSkillEffect(*session, state->player, error) && owner.Current();
    if (subtype == 45 || subtype == 46)
        return PublishShieldReturnCircle(*session, {actor->pev->origin.x, actor->pev->origin.y,
            actor->pev->origin.z}, error, subtype == 46) && owner.Current();
    if (subtype == 49 || subtype == 50)
        return PublishStatusDuration(*session, state->player, 28, duration, {255,255,255}, error) && owner.Current();
    if (subtype == 31)
    {
        if (!PublishStatusDuration(*session, state->player, 40, 5, {255,255,255}, error) ||
            !owner.Current()) return false;
        if (!players->InstallSkillLabel(state->player, 13, 0, duration))
        { error.message = "character recovery skill label could not be installed"; return false; }
    }
    if (subtype == 48)
        return PublishStatusDuration(*session, state->player, 28, duration, {255,255,255}, error) && owner.Current();
    return owner.Current();
}
}

bool HasOtherSkillEffects(int subtype) noexcept
{
    return subtype == 2 || subtype == 3 || subtype == 4 || subtype == 6 ||
        subtype == 7 || subtype == 8 || subtype == 9 || subtype == 10 ||
        subtype == 11 || subtype == 12 || subtype == 13 || subtype == 15 ||
        subtype == 16 || subtype == 19 || subtype == 20 || subtype == 28 ||
        subtype == 31 || subtype == 34 || subtype == 38 || subtype == 40 || subtype == 41 || subtype == 42 || subtype == 43 || subtype == 44 ||
        subtype == 45 || subtype == 47 || subtype == 48 || subtype == 49 || subtype == 50 || subtype == 54;
}

bool StartLegacyShieldSkill(int subtype,
    const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session, const ActivationState& timers,
    std::uint64_t identity, ShieldAttackState& attack, ConfigError& error)
{
    #pragma clang fp contract(off)
    EffectOwner owner(players, state, session);
    if (!owner.Current() || !gpGlobals || !error.message.empty()) return false;
    if (subtype != 14 && subtype != 17 && subtype != 26 && subtype != 27 && subtype != 30)
    { error.message = "legacy character shield type is invalid"; return false; }
    if ((subtype != 17 && !identity) || !std::isfinite(gpGlobals->time) || !std::isfinite(timers.effects.duration))
    { error.message = "legacy character shield identity or duration is invalid"; return false; }
    constexpr std::array<int, 7> early{1000, 1000, 2000, 2500, 3000, 3300, 3300};
    constexpr std::array<int, 7> wall{1500, 1500, 2500, 2800, 3200, 3500, 3500};
    constexpr std::array<int, 7> linked{2000, 2500, 3000, 3500, 4000, 5000, 5000};
    constexpr std::array<int, 7> staged{1600, 2000, 2400, 2800, 3200, 3600, 4000};
    const auto& values = subtype == 14 ? early : subtype == 17 ? wall : subtype == 30 ? staged : linked;
    const int difficulty = session->Difficulty().SelectedRow();
    const int capacity = difficulty >= 1 && difficulty <= 7 ? values[difficulty - 1] : values.back();
    const bool earlyShield = subtype == 14 || subtype == 17;
    const float duration = earlyShield ? timers.effects.duration : 12.0f;
    constexpr float radius = 10.0f * 39.37f;
    const float radiusSquared = earlyShield ? (10.0f * 10.0f) * 1549.996826171875f : radius * radius;
    auto* caster = state->player.Get<CBasePlayer>();
    for (int slot = 1; slot <= gpGlobals->maxClients; ++slot)
    {
        auto* recipient = UTIL_PlayerByIndex(slot);
        if (!owner.Current()) return false;
        if (!recipient || !recipient->pev || recipient->has_disconnected ||
            (recipient->pev->flags & (FL_DORMANT | FL_KILLME))) continue;
        const EHANDLE handle(recipient);
        const auto target = players->Find(handle);
        if (!target) continue;
        const auto life = target->skillLifeSerial;
        const auto current = [&] { return owner.Current() && players->Find(handle) == target &&
            handle.Get<CBasePlayer>() == recipient && target->skillLifeSerial == life; };
        //106E0440/106DD9E0 intentionally omit IsAlive;26/27/30 test it.
        if (!earlyShield)
        {
            const bool alive = recipient->IsAlive();
            if (!owner.Current()) return false;
            if (!current() || !alive) continue;
        }
        const auto delta = recipient->pev->origin - caster->pev->origin;
        const float squared = earlyShield ?
            (delta.y * delta.y + delta.x * delta.x) + delta.z * delta.z :
            (delta.x * delta.x + delta.y * delta.y) + delta.z * delta.z;
        if (squared > radiusSquared) continue;
        //14/17 statuses precede the shield and independently last13/10s.
        if (earlyShield)
        {
            if (!PublishStatusDuration(*session, handle, 30, subtype == 14 ? 13 : 10, {255,255,255}, error) ||
                !owner.Current()) return false;
            if (!current()) continue;
        }
        const EnergyShield::Callback callback(subtype == 26 || subtype == 27 ? subtype : 0, identity);
        if (!players->InstallSkillShield(handle, capacity, duration, error, callback) ||
            !owner.Current()) return false;
        if (!current()) continue;
        if (subtype == 26 || subtype == 27)
        {
            //10799130 calls the initial callback after inserting the shield.
            //Both callbacks tag the independent12s buff for removal when
            //that shield expires or breaks; the recipient may outlive caster.
            CSOLScenarioStatBuff buff;
            buff.group = 3; buff.source = 10; buff.subsource = subtype;
            buff.skillIdentity = identity; buff.expires = gpGlobals->time + 12;
            if (subtype == 26) buff.attackRatio = .3f;
            else buff.speed = 110;
            const auto token = players->InstallStatBuff(handle, std::move(buff));
            if (!token || !owner.Current()) return false;
            if (!players->InstallSkillLabel(handle, subtype == 26 ? 0 : 2, 0, 12, token))
            { error.message = "legacy shield stat label could not be installed"; return false; }
            if (!current()) continue;
            if (subtype == 27)
            {
                if (!players->HoldSkillProtectionRender(handle, token, error) || !owner.Current()) return false;
                if (!current()) continue;
                recipient->ResetMaxSpeed();
                if (!owner.Current()) return false;
            }
            else if (!PublishStatusDuration(*session, handle, 28, 12, {255,255,255}, error) ||
                !owner.Current()) return false;
        }
        else if (subtype == 30 &&
            (!PublishStatusDuration(*session, handle, 30, 12, {255,255,255}, error) ||
                !owner.Current())) return false;
    }
    if (subtype == 30)
    {
        attack.nextStage = gpGlobals->time + 4;
        attack.remaining = 2;
        CSOLScenarioStatBuff buff;
        buff.group = 3; buff.source = 10; buff.subsource = 30;
        buff.skillIdentity = identity; buff.expires = gpGlobals->time + 4;
        buff.attackRatio = .5f;
        const auto token = players->InstallStatBuff(state->player, std::move(buff));
        if (!token || !owner.Current()) return false;
        if (!players->InstallSkillLabel(state->player, 0, 0, 4, token))
        { error.message = "staged shield attack label could not be installed"; return false; }
    }
    return owner.Current();
}

bool ActiveShieldAttack30(const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session,
    std::uint64_t identity, ShieldAttackState& attack, ConfigError& error)
{
    #pragma clang fp contract(off)
    EffectOwner owner(players, state, session);
    if (!owner.Current() || !gpGlobals || !error.message.empty()) return false;
    //106F0910 advances at most one stage, strictly after its deadline. A
    //delayed server frame must not run two replacement callbacks together.
    if (!(attack.nextStage > 0 && gpGlobals->time > attack.nextStage)) return true;
    const float reduction = (3.0f - static_cast<float>(attack.remaining)) * .15f;
    const float ratio = .5f - reduction;
    std::vector<std::uint64_t> tokens;
    for (const auto& buff : state->buffs)
        if (buff.skillIdentity == identity) tokens.push_back(buff.token);
    for (const auto token : tokens)
    {
        if (!owner.Current()) return false;
        const auto found = std::find_if(state->buffs.begin(), state->buffs.end(),
            [token](const auto& buff) { return buff.token == token; });
        if (found != state->buffs.end() && !players->RemoveStatBuff(state->player, token)) return false;
    }
    if (!owner.Current()) return false;
    CSOLScenarioStatBuff buff;
    buff.group = 3; buff.source = 10; buff.subsource = 30;
    buff.skillIdentity = identity; buff.expires = gpGlobals->time + 4;
    buff.attackRatio = ratio;
    const auto token = players->InstallStatBuff(state->player, std::move(buff));
    if (!token || !owner.Current()) return false;
    if (!players->InstallSkillLabel(state->player, 0, 0, 4, token))
    { error.message = "staged shield attack label could not be replaced"; return false; }
    --attack.remaining;
    attack.nextStage = attack.remaining > 0 ? gpGlobals->time + 4 : -1;
    return owner.Current();
}

bool StartGroupSkill(int subtype,
    const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session,
    const ActivationState& timers, ConfigError& error)
{
    #pragma clang fp contract(off)
    EffectOwner owner(players, state, session);
    if (!owner.Current() || !gpGlobals || !error.message.empty()) return false;
    if (subtype != 32 && subtype != 33)
    { error.message = "group character skill type is invalid"; return false; }
    auto* caster = state->player.Get<CBasePlayer>();
    const Vector center = caster->pev->origin - Vector(0, 0, (caster->pev->flags & FL_DUCKING) ? 18 : 36);
    constexpr float radius = 8.0f * 39.37f;
    std::array<AuraMember, 32> targets{};
    std::size_t count{};
    //106E6910 snapshots nearby living players before calculating its bonus;
    //membership is not recalculated between recipients or during healing.
    for (int slot = 1; slot <= gpGlobals->maxClients; ++slot)
    {
        auto* recipient = UTIL_PlayerByIndex(slot);
        if (!owner.Current()) return false;
        if (!recipient || !recipient->pev || recipient->has_disconnected ||
            (recipient->pev->flags & (FL_DORMANT | FL_KILLME))) continue;
        const EHANDLE handle(recipient);
        const auto target = players->Find(handle);
        if (!target) continue;
        const auto life = target->skillLifeSerial;
        const bool alive = recipient->IsAlive();
        if (!owner.Current()) return false;
        if (!alive || handle.Get<CBasePlayer>() != recipient || players->Find(handle) != target ||
            target->skillLifeSerial != life) continue;
        const auto delta = center - recipient->pev->origin;
        if ((delta.y * delta.y + delta.x * delta.x) + delta.z * delta.z > radius * radius) continue;
        if (count == targets.size())
        { error.message = "group character skill exceeds the supported player limit"; return false; }
        targets[count++] = {target, life};
    }
    const auto others = static_cast<std::uint32_t>(count) - 1u;
    const float bonus = static_cast<float>(others) * (subtype == 32 ? .05f : .1f);
    const float ratio = std::min(bonus + (subtype == 32 ? .2f : .5f), subtype == 32 ? .35f : .8f);
    for (std::size_t i = 0; i < count; ++i)
    {
        if (!owner.Current()) return false;
        const auto target = targets[i].state.lock();
        auto* recipient = target ? target->player.Get<CBasePlayer>() : nullptr;
        if (!recipient || !recipient->pev || players->Find(target->player) != target ||
            target->skillLifeSerial != targets[i].life) continue;
        const auto current = [&] { return owner.Current() &&
            target->player.Get<CBasePlayer>() == recipient && players->Find(target->player) == target &&
            target->skillLifeSerial == targets[i].life; };
        if (subtype == 32)
        {
            CSOLScenarioStatBuff buff;
            buff.group = 3; buff.source = 10; buff.subsource = subtype;
            buff.attackRatio = ratio; buff.expires = gpGlobals->time + 12;
            const auto token = players->InstallStatBuff(target->player, std::move(buff));
            if (!token || !owner.Current()) return false;
            if (!players->InstallSkillLabel(target->player, 0, 0, 12, token))
            { error.message = "group character attack label could not be installed"; return false; }
            //106EC46F enables the buff's render dispatcher (+20), but leaves
            //the protection-shell flag (+21) clear: attack label0 only.
            if (!current()) continue;
            if (!PublishStatusDuration(*session, target->player, 28, 12, {255,255,255}, error) ||
                !owner.Current()) return false;
        }
        else
        {
            const float amount = recipient->pev->max_health * ratio;
            const float total = recipient->pev->health + amount;
            if (!std::isfinite(amount) || !std::isfinite(total))
            { error.message = "group character recovery amount is invalid"; return false; }
            if (total > recipient->pev->max_health)
            {
                const double overflow = std::trunc(static_cast<double>(total - recipient->pev->max_health));
                if (overflow < std::numeric_limits<int>::min() || overflow > std::numeric_limits<int>::max())
                { error.message = "group character overflow shield is out of range"; return false; }
                if (!players->InstallSkillShield(target->player, static_cast<int>(overflow), 15, error) ||
                    !owner.Current()) return false;
            }
            if (!current()) continue;
            const auto runtime = CSOL_ScenarioRuntimeForAccess(owner.access.get());
            if (!runtime || !owner.Current()) return false;
            //106EBF6D supplies null inflictor to the actual recovery path.
            CSOLPlayerRecoveryCall call(runtime, target->player,
                [](const void* value) { return static_cast<const EffectOwner*>(value)->Current(); }, &owner, nullptr);
            bool applied{};
            if (!players->ApplyRecovery(call, amount, applied, error) || !owner.Current()) return false;
        }
    }
    if (subtype == 32)
    {
        if (!players->InstallSkillLabel(state->player, 13, 0, timers.effects.duration))
        { error.message = "group character active label could not be installed"; return false; }
        if (!owner.Current()) return false;
    }
    else if (!PublishStatusDuration(*session, state->player, 27, timers.effects.duration,
        {255,255,255}, error) || !owner.Current()) return false;
    if (!PublishGroupSkillEffect(*session, state->player, subtype, timers.effects.duration, error) ||
        !owner.Current()) return false;
    if (subtype == 33 && !players->InstallSkillLabel(state->player, 13, 0, timers.effects.duration))
    { error.message = "group character recovery label could not be installed"; return false; }
    return owner.Current();
}

bool StartDrainSupport37(const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session,
    ActivationState& timers, std::uint64_t identity, ConfigError& error)
{
    #pragma clang fp contract(off)
    EffectOwner owner(players, state, session);
    if (!owner.Current() || !gpGlobals || !identity || !error.message.empty()) return false;
    //106ED039 installs a one-second caster label before overriding duration.
    if (!players->InstallSkillLabel(state->player, 13, 0, 1))
    { error.message = "drain character active label could not be installed"; return false; }
    timers.effects.duration = 10;
    auto* caster = state->player.Get<CBasePlayer>();
    for (int slot = 1; slot <= gpGlobals->maxClients; ++slot)
    {
        auto* recipient = UTIL_PlayerByIndex(slot);
        if (!owner.Current()) return false;
        if (!recipient || !recipient->pev || recipient->has_disconnected ||
            (recipient->pev->flags & (FL_DORMANT | FL_KILLME))) continue;
        const EHANDLE handle(recipient);
        const auto target = players->Find(handle);
        if (!target) continue;
        const auto life = target->skillLifeSerial;
        const auto current = [&] { return owner.Current() && handle.Get<CBasePlayer>() == recipient &&
            players->Find(handle) == target && target->skillLifeSerial == life; };
        const bool alive = recipient->IsAlive();
        if (!owner.Current()) return false;
        if (!current() || !alive) continue;
        const auto delta = recipient->pev->origin - caster->pev->origin;
        if ((delta.y * delta.y + delta.x * delta.x) + delta.z * delta.z >
            (8.0f * 8.0f) * 1549.996826171875f) continue;
        CSOLScenarioStatBuff buff;
        buff.group = 3; buff.source = 10; buff.subsource = 37;
        buff.skillIdentity = identity; buff.expires = gpGlobals->time + 10;
        buff.attackRatio = .3f;
        const bool self = recipient == caster;
        if (self) buff.speed = 40;
        const auto token = players->InstallStatBuff(handle, std::move(buff));
        if (!token || !owner.Current()) return false;
        if (!players->InstallSkillLabel(handle, 0, 0, 10, token) ||
            (self && !players->InstallSkillLabel(handle, 2, 0, 10, token)))
        { error.message = "drain character buff labels could not be installed"; return false; }
        if (!current()) continue;
        if (!players->HoldSkillProtectionRender(handle, token, error) || !owner.Current()) return false;
        if (!current()) continue;
        if (self) { recipient->ResetMaxSpeed(); if (!owner.Current()) return false; }
        if (!current()) continue;
        if (!PublishStatusDuration(*session, handle, 28, 10, {255,255,255}, error) ||
            !owner.Current()) return false;
    }
    return PublishGroupSkillEffect(*session, state->player, 37, 10, error) && owner.Current();
}

bool RunAuraSkill(int subtype, AuraStage stage,
    const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session,
    AuraState& aura, std::uint64_t identity, ConfigError& error)
{
    EffectOwner owner(players, state, session);
    if ((subtype != 24 && subtype != 25) || !owner.Current(false)) return false;
    if (!gpGlobals || !std::isfinite(gpGlobals->time) || !identity)
    { error.message = "character aura clock or identity is unavailable"; return false; }
    // A notification can rebuild/retire this same skill synchronously. A
    // later invocation owns its tracker; the interrupted one must not resume.
    if (aura.revision == UINT64_MAX)
    { error.message = "character aura revision is exhausted"; return false; }
    const auto revision = ++aura.revision;
    const auto current = [&] { return owner.Current(false) && aura.revision == revision; };
    const auto resolve = [&](const AuraMember& member)
    {
        const auto target = member.state.lock();
        return target && players->Find(target->player) == target &&
            target->skillLifeSerial == member.life ? target : nullptr;
    };
    const auto targetCurrent = [&](const AuraMember& member,
        const std::shared_ptr<const CSOLScenarioPlayerRuntimeState>& target)
    {
        auto* player = target ? target->player.Get<CBasePlayer>() : nullptr;
        return current() && resolve(member) == target && player && player->pev &&
            !player->has_disconnected && !(player->pev->flags & (FL_KILLME | FL_DORMANT));
    };
    const auto leave = [&](const AuraMember& member)
    {
        if (!aura.active) return current();
        const auto target = resolve(member);
        if (!target || !targetCurrent(member, target)) return current();
        //106E68B0 removes the caster's held effect on any valid24 leave;
        //106E67D0 for25 only removes this skill's tagged target buffs.
        if (subtype == 24 && (!PublishAuraSkillEffect(*session, state->player,
            aura.effectId, false, error) || !current())) return false;
        if (!targetCurrent(member, target)) return current();
        std::vector<std::uint64_t> tokens;
        for (const auto& buff : target->buffs)
            if (buff.skillIdentity == identity) tokens.push_back(buff.token);
        for (const auto token : tokens)
        {
            if (!targetCurrent(member, target)) return current();
            // A previous removal callback can remove another tagged buff.
            if (std::none_of(target->buffs.begin(), target->buffs.end(),
                [token](const auto& buff) { return buff.token == token; })) continue;
            if (!players->RemoveStatBuff(target->player, token) || !current()) return false;
        }
        return current();
    };
    const auto clear = [&]
    {
        // Revoke each membership before its callback, so nested retirement
        // cannot deliver leave twice or retain a reference into this array.
        while (aura.count)
        {
            const auto member = aura.members[0];
            std::move(aura.members.begin() + 1, aura.members.begin() + aura.count,
                aura.members.begin());
            aura.members[--aura.count] = {};
            if (!leave(member) || !current()) return false;
        }
        return current();
    };
    const auto removeVisual = [&]
    { return PublishAuraSkillEffect(*session, state->player, aura.effectId, false, error) && current(); };
    if (stage == AuraStage::RemoveVisual) return removeVisual();
    if (stage == AuraStage::Retire)
    {
        if (!removeVisual() || !clear()) return false;
        aura.active = false; aura.initialized = false;
        return true;
    }
    if (stage == AuraStage::Rebuild)
    {
        auto* player = state->player.Get<CBasePlayer>();
        aura.team = static_cast<int>(player->m_iTeam); // Captured by the original filter.
        if (!clear()) return false;
        aura.active = false; aura.initialized = true;
        aura.nextUpdate = gpGlobals->time + 1;
        return true;
    }
    if (stage == AuraStage::Start)
    {
        //25's106EA81B removes the old visual before clearing recipients;
        //24 instead clears recipients first. Cancellation must keep this order.
        if (subtype == 25 && !removeVisual()) return false;
        if (!clear()) return false;
        aura.active = true;
        return PublishAuraSkillEffect(*session, state->player, aura.effectId, true, error) && current();
    }
    if (stage == AuraStage::End)
    {
        if (!clear()) return false;
        aura.active = false;
        return removeVisual();
    }
    if (!aura.initialized || gpGlobals->time < aura.nextUpdate) return true;
    aura.nextUpdate = gpGlobals->time + .1f; //106E87E0: no catch-up loop.
    auto* actor = state->player.Get<CBasePlayer>();
    const bool alive = actor->IsAlive();
    if (!current()) return false;
    if (!alive) return clear();
    if (!g_engfuncs.pfnFindEntityInSphere)
    { error.message = "character aura sphere iterator is unavailable"; return false; }

    //106DA450 gathers first;106DA810 removes old members before entering
    // new ones. Preserve the engine's order and include the caster itself.
    std::array<AuraMember, 32> candidates{};
    std::size_t count{};
    const Vector origin = actor->pev->origin;
    edict_t* cursor{};
    while (true)
    {
        cursor = FIND_ENTITY_IN_SPHERE(cursor, origin, 4.0f * 39.37f);
        if (!current()) return false;
        if (FNullEnt(cursor)) break;
        auto* entity = CBaseEntity::Instance(cursor);
        auto* player = dynamic_cast<CBasePlayer*>(entity);
        if (!player || !player->pev || player->has_disconnected ||
            (player->pev->flags & (FL_KILLME | FL_DORMANT))) continue;
        const EHANDLE handle(player);
        const auto target = players->Find(handle);
        if (!target) continue;
        const AuraMember candidate{target, target->skillLifeSerial};
        const bool targetAlive = player->IsAlive();
        if (!current()) return false;
        if (!targetCurrent(candidate, target) || !targetAlive ||
            static_cast<int>(player->m_iTeam) != aura.team) continue;
        if (count == candidates.size())
        { error.message = "character aura exceeded the player capacity"; return false; }
        candidates[count++] = candidate;
    }
    const auto equal = [&](const AuraMember& a, const AuraMember& b)
    { return a.life == b.life && a.state.lock() == b.state.lock(); };
    for (std::size_t index = 0; index < aura.count;)
    {
        const auto member = aura.members[index];
        const bool retained = resolve(member) && std::any_of(candidates.begin(), candidates.begin() + count,
            [&](const auto& candidate) { return equal(member, candidate); });
        if (retained) { ++index; continue; }
        std::move(aura.members.begin() + index + 1, aura.members.begin() + aura.count,
            aura.members.begin() + index);
        aura.members[--aura.count] = {};
        if (!leave(member) || !current()) return false;
    }
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto& member = candidates[index];
        const auto target = resolve(member);
        if (!target || !targetCurrent(member, target) ||
            std::any_of(aura.members.begin(), aura.members.begin() + aura.count,
                [&](const auto& existing) { return equal(member, existing); })) continue;
        aura.members[aura.count++] = member;
        if (!aura.active) continue;
        CSOLScenarioStatBuff buff;
        buff.group = 3; buff.source = 10; buff.subsource = subtype;
        buff.skillIdentity = identity; buff.expires = -1;
        if (subtype == 24) { buff.dodgeChance = 30; buff.attackRatio = .15f; }
        else
        {
            buff.incomingDamageRatio = -.2f;
            buff.periodicInterval = 1; buff.periodicRecovery = 80;
        }
        const auto token = players->InstallStatBuff(target->player, std::move(buff));
        if (!current()) return false;
        if (!targetCurrent(member, target)) continue;
        if (!token) { error.message = "character aura buff could not be installed"; return false; }
        for (const int label : (subtype == 24 ? std::array{26, 0} : std::array{31, 13}))
        {
            if (!players->InstallSkillLabel(target->player, label, 0, -1, token) || !current()) return false;
            if (!targetCurrent(member, target)) break;
        }
    }
    return current();
}

bool StartReflectionSkill36(const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session, const ActivationState& timers,
    ReflectionState& reflection, ConfigError& error)
{
    EffectOwner owner(players, state, session);
    if (!owner.Current() || !gpGlobals) return false;
    const float duration = timers.effects.duration;
    CSOLScenarioStatBuff buff;
    buff.group = 3; buff.source = 10; buff.subsource = 36;
    buff.expires = duration < 0 ? -1 : gpGlobals->time + duration;
    buff.attackRatio = .15f;
    const auto token = players->InstallStatBuff(state->player, std::move(buff));
    if (!token || !owner.Current()) return false;
    if (!players->InstallSkillLabel(state->player, 0, 0, duration, token) || !owner.Current() ||
        !PublishStatusDuration(*session, state->player, 45, duration, {255,255,255}, error) ||
        !owner.Current() || !PublishSkillScreenRGBA(*session, state->player, 46, duration,
            {200,200,200,200}, error) || !owner.Current() ||
        !PublishReflectionSkillEffect(*session, state->player, reflection.effectId, true, error) ||
        !owner.Current()) return false;
    //106ECBE4 resets only damage. The recovery deadline is constructor0
    // and persists between activations, instead of being reset on Start.
    reflection.accumulated = 0;
    return true;
}

bool ActiveReflectionSkill36(const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session, ReflectionState& reflection, ConfigError& error)
{
    EffectOwner owner(players, state, session);
    if (!owner.Current(false) || !gpGlobals) return false;
    auto* player = state->player.Get<CBasePlayer>();
    const bool alive = player->IsAlive();
    if (!owner.Current(false)) return false;
    if (!alive) return true;
    if (reflection.accumulated > 500)
    {
        if (!ActiveExpandedAreaSkill36(players, state, session, reflection.accumulated, error) ||
            !owner.Current() || !PublishReflectionSkillPulse(*session, state->player, error) ||
            !owner.Current() || !PublishSkillScreenRGBA(*session, state->player, 47, 1,
                {255,255,255,255}, error) || !owner.Current()) return false;
        // This also clears when the original collected no valid targets.
        reflection.accumulated = 0;
    }
    if (gpGlobals->time < reflection.nextRecovery) return true;
    const float recovery = player->pev->max_health * .05f;
    const auto runtime = CSOL_ScenarioRuntimeForAccess(owner.access.get());
    if (!runtime || !owner.Current()) return false;
    CSOLPlayerRecoveryCall call(runtime, state->player,
        [](const void* value) { return static_cast<const EffectOwner*>(value)->Current(); }, &owner, nullptr);
    bool applied{};
    if (!players->ApplyRecovery(call, recovery, applied, error) || !owner.Current()) return false;
    reflection.nextRecovery = gpGlobals->time + 1;
    return true;
}

bool EndEscortSkill(const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session, ConfigError& error)
{
    EffectOwner owner(players, state, session);
    if (!owner.Current()) return false;
    const auto runtime = CSOL_ScenarioRuntimeForAccess(owner.access.get());
    if (!runtime || !owner.Current()) return false;
    //106E7330 calls the caster's v4C with its own pev,500,0. This is
    // recovery through the real module pipeline, not a raw health write.
    CSOLPlayerRecoveryCall call(runtime, state->player,
        [](const void* value) { return static_cast<const EffectOwner*>(value)->Current(); }, &owner,
        state->player);
    bool applied{};
    return players->ApplyRecovery(call, 500, applied, error) && owner.Current();
}

bool StartGuardSkill(int subtype, const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session, ActivationState& timers,
    std::uint64_t identity, float& accumulated, ConfigError& error)
{
    EffectOwner owner(players, state, session);
    if (!owner.Current() || !gpGlobals || (subtype != 51 && subtype != 52)) return false;
    //106EEF40/106EF0C0 override the ClassMod duration. The protected-hit
    // accumulator is reset after installation, immediately before status40.
    timers.effects.duration = 6;
    CSOLScenarioStatBuff buff;
    buff.group = 3; buff.source = 10; buff.subsource = subtype;
    buff.skillIdentity = identity; buff.expires = gpGlobals->time + 6;
    buff.protection = true; buff.mask = 1;
    const auto token = players->InstallStatBuff(state->player, std::move(buff));
    if (!token || !owner.Current() ||
        !players->HoldSkillProtectionRender(state->player, token, error) || !owner.Current()) return false;
    accumulated = 0;
    return PublishStatusDuration(*session, state->player, 40, 6, {255,255,255}, error) && owner.Current();
}

bool EndGuardRecovery52(const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session, float accumulated, ConfigError& error)
{
    #pragma clang fp contract(off)
    EffectOwner owner(players, state, session);
    if (!owner.Current() || !gpGlobals) return false;
    const auto integer = [&](float value, int& result)
    {
        const double truncated = std::trunc(static_cast<double>(value));
        if (!std::isfinite(value) || truncated < std::numeric_limits<int>::min() ||
            truncated > std::numeric_limits<int>::max())
        { error.message = "character guard recovery integer is out of range"; return false; }
        result = static_cast<int>(truncated); return true;
    };
    //106E7A90 truncates accumulated damage before imposing its500 floor.
    constexpr std::array<float, 7> factors{1,1.5f,2,3,5,10,15};
    const int difficulty = session->Difficulty().SelectedRow();
    const float factor = difficulty >= 1 && difficulty <= 7 ? factors[difficulty - 1] : factors.back();
    int total{}, before{};
    auto* player = state->player.Get<CBasePlayer>();
    if (!integer(accumulated, total) || !integer(player->pev->health, before)) return false;
    const float recovery = std::min(static_cast<float>(std::max(total,500)) * factor, 10000.0f);
    const auto runtime = CSOL_ScenarioRuntimeForAccess(owner.access.get());
    if (!runtime || !owner.Current()) return false;
    CSOLPlayerRecoveryCall call(runtime, state->player,
        [](const void* value) { return static_cast<const EffectOwner*>(value)->Current(); }, &owner,
        state->player);
    bool applied{};
    if (!players->ApplyRecovery(call, recovery, applied, error) || !owner.Current()) return false;
    int gained{}, remainder{};
    // Preserve the original two truncations, including the integer snapshot
    // of health before recovery. Only the unused amount becomes a10s shield.
    if (!integer(player->pev->health - static_cast<float>(before), gained) ||
        !integer(recovery - static_cast<float>(gained), remainder)) return false;
    if (!players->InstallSkillShield(state->player, remainder, 10, error) || !owner.Current()) return false;
    return PublishStatusDuration(*session, state->player, 30, 10, {255,255,255}, error) && owner.Current();
}

bool StartDragonShield53(const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session,
    std::uint64_t identity, ConfigError& error)
{
    #pragma clang fp contract(off)
    EffectOwner owner(players, state, session);
    if (!owner.Current() || !gpGlobals || !identity) return false;
    constexpr float radius = 5.0f * 39.37f;
    for (int index = 1; index <= gpGlobals->maxClients; ++index)
    {
        auto* entity = CSOL_ScenarioPlayerEntity(index, owner.access);
        if (!owner.Current()) return false;
        auto* target = dynamic_cast<CBasePlayer*>(entity);
        if (!target || !target->pev || target->has_disconnected ||
            (target->pev->flags & (FL_KILLME | FL_DORMANT))) continue;
        const EHANDLE handle(target);
        const auto targetState = players->Find(handle);
        if (!targetState) continue;
        const auto life = targetState->skillLifeSerial;
        const auto current = [&] {
            return handle.Get<CBasePlayer>() == target && players->Find(handle) == targetState &&
                targetState->skillLifeSerial == life;
        };
        const bool alive = target->IsAlive();
        if (!owner.Current()) return false;
        if (!alive || !current()) continue;
        const auto delta = state->player.Get<CBasePlayer>()->pev->origin - target->pev->origin;
        const float squared = (delta.x * delta.x + delta.y * delta.y) + delta.z * delta.z;
        if (squared > radius * radius) continue;
        //106E6040 installs the anonymous shield before invoking106DD240 on
        // this recipient. The recipient, not the caster, owns its damage aura.
        if (!players->InstallSkillShield(handle, 3600, 7, error, {53, identity}) ||
            !owner.Current()) return false;
        if (!current()) continue;
        CSOLScenarioStatBuff buff;
        buff.group = 3; buff.source = 10; buff.subsource = 53;
        buff.skillIdentity = identity; buff.expires = gpGlobals->time + 7;
        buff.speed = 50;
        buff.periodicDamage = 1000;
        buff.periodicDamageInterval = 1;
        buff.periodicDamageRadius = 4.0f * 39.37f;
        const auto token = players->InstallStatBuff(handle, std::move(buff));
        if (!owner.Current()) return false;
        if (!current()) continue;
        if (!token)
        { error.message = "character dragon shield stat buff could not be installed"; return false; }
        if (!players->InstallSkillLabel(handle, 2, 0, 7, token))
        { error.message = "character dragon shield speed label could not be installed"; return false; }
        if (!owner.Current()) return false;
        if (!current()) continue;
        target->ResetMaxSpeed();
        if (!owner.Current()) return false;
        if (!current()) continue;
        if (!PublishStatusDuration(*session, handle, 30, 7, {255,255,255}, error) ||
            !owner.Current()) return false;
    }
    return owner.Current();
}

bool EndRallySkill38(const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session, ConfigError& error)
{
    #pragma clang fp contract(off)
    EffectOwner owner(players, state, session);
    if (!owner.Current() || !gpGlobals) return false;
    const auto runtime = CSOL_ScenarioRuntimeForAccess(owner.access.get());
    if (!runtime || !owner.Current()) return false;
    for (int index = 1; index <= gpGlobals->maxClients; ++index)
    {
        auto* target = UTIL_PlayerByIndex(index);
        if (!owner.Current()) return false;
        if (!target || !target->pev || target->has_disconnected ||
            (target->pev->flags & (FL_KILLME | FL_DORMANT))) continue;
        const EHANDLE handle(target);
        const auto targetState = players->Find(handle);
        if (!targetState) continue;
        const auto life = targetState->skillLifeSerial;
        const bool alive = target->IsAlive();
        if (!owner.Current()) return false;
        if (!alive || handle.Get<CBasePlayer>() != target || players->Find(handle) != targetState ||
            targetState->skillLifeSerial != life) continue;
        // Original End enumerates again around the caster's current origin;
        // entering the area after Start still qualifies for the final heal.
        const auto delta = target->pev->origin - state->player.Get<CBasePlayer>()->pev->origin;
        const float squared = (delta.y * delta.y + delta.x * delta.x) + delta.z * delta.z;
        if (squared > (8.0f * 8.0f) * 1549.996826171875f) continue;
        CSOLPlayerRecoveryCall call(runtime, handle,
            [](const void* value) { return static_cast<const EffectOwner*>(value)->Current(); }, &owner,
            state->player);
        bool applied{};
        if (!players->ApplyRecovery(call, target->pev->max_health * 0.5f, applied, error) || !owner.Current()) return false;
    }
    return owner.Current();
}

bool EndShieldSkill45(const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session, ConfigError& error)
{
    #pragma clang fp contract(off)
    EffectOwner owner(players, state, session);
    if (!owner.Current() || !gpGlobals) return false;
    //106E7770 samples feature21+18 without refreshing it inside this callback.
    // This is the total shield, including other concurrent shield entries.
    const float energy = state->skillShield.Energy();
    const float fraction = std::min(energy, 3500.0f) / 3500.0f;
    const float attack = fraction * 0.35f + 0.1f;
    if (!std::isfinite(attack))
    { error.message = "character shield return attack is invalid"; return false; }
    CSOLScenarioStatBuff buff;
    buff.group = 3; buff.source = 10; buff.subsource = 45;
    buff.expires = gpGlobals->time + 8; buff.attackRatio = attack;
    const auto token = players->InstallStatBuff(state->player, std::move(buff));
    if (!token || !owner.Current()) return false;
    if (!players->InstallSkillLabel(state->player, 0, 0, 8, token))
    { error.message = "character shield return attack label could not be installed"; return false; }
    return PublishStatusDuration(*session, state->player, 28, 8, {255,255,255}, error) && owner.Current();
}

bool EndSmokeCloakSkill(int subtype,
    const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session, ConfigError& error)
{
    EffectOwner owner(players, state, session);
    if (!owner.Current() || !gpGlobals || (subtype != 22 && subtype != 23)) return false;
    constexpr float duration = 10;
    CSOLScenarioStatBuff bonus;
    bonus.group = 3; bonus.source = 10; bonus.subsource = subtype;
    bonus.expires = gpGlobals->time + duration;
    if (subtype == 22)
    { bonus.criticalEnabled = true; bonus.criticalChance = 10; bonus.criticalMultiplier = 1.8f; }
    else bonus.dodgeChance = 20;
    const auto token = players->InstallStatBuff(state->player, std::move(bonus));
    if (!token || !owner.Current()) return false;
    if (!players->InstallSkillLabel(state->player, subtype == 22 ? 24 : 26, 0, duration, token))
    { error.message = "smoke cloak ending bonus label failed"; return false; }
    CSOLScenarioStatBuff attack;
    attack.group = 3; attack.source = 10; attack.subsource = subtype;
    attack.attackRatio = .2f; attack.expires = gpGlobals->time + duration;
    attack.condition = std::make_shared<KnifeCondition>();
    const auto attackToken = players->InstallStatBuff(state->player, std::move(attack));
    if (!attackToken || !owner.Current()) return false;
    if (!players->InstallSkillLabel(state->player, 25, 0, duration, attackToken))
    { error.message = "smoke cloak ending knife label failed"; return false; }
    return PublishStatusDuration(*session, state->player, 28, duration, {255,255,255}, error) && owner.Current();
}

bool UpgradeIncomingSkill(int subtype,
    const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session,
    std::uint64_t identity, ConfigError& error)
{
    EffectOwner owner(players, state, session);
    if (!owner.Current() || !gpGlobals || (subtype != 49 && subtype != 50) || !identity) return false;
    float remaining{};
    std::vector<std::uint64_t> tokens;
    for (const auto& buff : state->buffs)
        if (buff.skillIdentity == identity)
        {
            tokens.push_back(buff.token);
            if (buff.Active(gpGlobals->time)) remaining = std::max(remaining,
                buff.expires <= 0 ? std::numeric_limits<float>::max() : buff.expires - gpGlobals->time);
        }
    // 106E38E0 replaces49's identified buff.106E3720 leaves50's original
    // buff in place and installs a separate strongest-in-group effect.
    if (subtype == 49)
        for (auto token : tokens)
            if (!players->RemoveStatBuff(state->player, token) || !owner.Current()) return false;
    CSOLScenarioStatBuff buff;
    buff.group = 3; buff.source = 10; buff.subsource = subtype;
    buff.skillIdentity = subtype == 49 ? identity : 0;
    buff.attackRatio = static_cast<float>(static_cast<double>(0.3f) + static_cast<double>(0.15f));
    buff.expires = gpGlobals->time + remaining;
    if (subtype == 50) { buff.periodicRecovery = 200; buff.periodicInterval = 1; }
    const auto token = players->InstallStatBuff(state->player, std::move(buff));
    return token && owner.Current() && players->HoldSkillProtectionRender(state->player, token, error) && owner.Current();
}

bool StartLotusSkill(const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session,
    ActivationState& timers, LotusState& lotus, std::uint64_t identity, ConfigError& error)
{
    EffectOwner owner(players, state, session);
    if (!owner.Current() || !gpGlobals || !error.message.empty()) return false;
    if (!identity || !std::isfinite(gpGlobals->time) || !std::isfinite(timers.effects.duration))
    { error.message = "lotus skill identity or clock is invalid"; return false; }
    auto* actor = state->player.Get<CBasePlayer>();
    Vector origin = actor->pev->origin;
    // 1133CE00 derives the feet from the current hull, without tracing. The
    // two-second pulse stays here even when the caster subsequently moves.
    origin.z -= (actor->pev->flags & FL_DUCKING) ? 18.0f : 36.0f;
    if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z))
    { error.message = "lotus skill origin is invalid"; return false; }
    lotus.origin = {origin.x, origin.y, origin.z};
    lotus.values = LotusDifficultyValues(session->Difficulty().SelectedRow());
    lotus.nextPulse = 0;
    lotus.life = state->skillLifeSerial;
    lotus.started = true;
    if (!PublishLotusSkillEffect(*session, lotus.origin, timers.effects.duration, error) ||
        !owner.Current()) return false;

    constexpr float duration = 8.0f;
    constexpr float radius = 6.0f * 39.37f;
    for (int slot = 1; slot <= gpGlobals->maxClients; ++slot)
    {
        auto* recipient = UTIL_PlayerByIndex(slot);
        if (!owner.Current()) return false;
        if (!recipient || !recipient->pev || recipient->has_disconnected ||
            (recipient->pev->flags & (FL_DORMANT | FL_KILLME))) continue;
        const EHANDLE handle(recipient);
        const auto target = players->Find(handle);
        const auto life = target ? target->skillLifeSerial : 0;
        const auto targetCurrent = [&]
        { return owner.Current() && handle.Get<CBasePlayer>() == recipient &&
            players->Find(handle) == target && target && target->skillLifeSerial == life; };
        const bool alive = recipient->IsAlive();
        if (!owner.Current()) return false;
        if (!targetCurrent() || !alive) continue;
        const auto delta = recipient->pev->origin - origin;
        const float squared = (delta.x * delta.x + delta.y * delta.y) + delta.z * delta.z;
        if (squared > radius * radius) continue;
        const bool self = recipient == actor;
        CSOLScenarioStatBuff buff;
        buff.group = 3; buff.source = 10; buff.subsource = 55;
        buff.skillIdentity = identity;
        buff.expires = gpGlobals->time + duration;
        buff.attackRatio = self ? 0.35f : 0.2f;
        // Original+20 retains protection rendering, but+21 remains false:
        // the separately installed finite energy shield absorbs damage.
        const auto token = players->InstallStatBuff(handle, std::move(buff));
        if (!owner.Current()) return false;
        if (!targetCurrent()) continue;
        if (!token || !players->InstallSkillLabel(handle, 0, 0, duration, token) ||
            !players->HoldSkillProtectionRender(handle, token, error))
        { if (targetCurrent() && error.message.empty()) error.message = "lotus attack buff failed"; return false; }
        if (!owner.Current()) return false;
        if (!targetCurrent()) continue;
        if (!players->InstallSkillShield(handle,
            self ? lotus.values.selfShield : lotus.values.allyShield, duration, error)) return false;
        if (!owner.Current()) return false;
        if (!targetCurrent()) continue;
        if (!PublishStatusDuration(*session, handle, 49, duration, {255,255,255}, error)) return false;
        if (!owner.Current()) return false;
    }
    return owner.Current();
}

bool PrepareOtherSkillEffects(int subtype,
    const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session,
    ActivationState& timers, bool& prepared, ConfigError& error)
{
    prepared = false;
    if (subtype != 10 && subtype != 11 && subtype != 13)
    { error.message = "character skill has no automatic prepare effect"; return false; }
    EffectOwner owner(players, state, session);
    if (!owner.Current() || !gpGlobals || !error.message.empty()) return false;
    if (!g_engfuncs.pfnRandomLong || !std::isfinite(gpGlobals->time))
    { error.message = "automatic character skill random clock is unavailable"; return false; }
    // The original v40 first consumes RANDOM_LONG(1,1000), then returns false
    // on failure without committing cooldown, voice or activation messages.
    const int roll = RANDOM_LONG(1,1000);
    if (!owner.Current()) return false;
    if (roll > (subtype == 11 ? 150 : 100)) return true;
    const float duration = timers.effects.duration;
    if (!std::isfinite(duration))
    { error.message = "automatic character skill duration is invalid"; return false; }
    timers.delay = 0;
    if (subtype != 13) timers.actionConstraint = 0;
    auto* player = state->player.Get<CBasePlayer>();
    const Vector origin = player->pev->origin;
    if (subtype == 13 &&
        (!PublishSkillCircle(*session, {origin.x, origin.y, origin.z}, true, error) || !owner.Current())) return false;
    CSOLScenarioStatBuff attack;
    attack.group = 3; attack.source = 10; attack.subsource = subtype;
    attack.expires = duration < 0 ? -1 : gpGlobals->time + duration;
    attack.attackRatio = subtype == 10 ? 0.2f : 0.3f;
    const auto attackToken = players->InstallStatBuff(state->player, std::move(attack));
    if (!owner.Current()) return false;
    if (!attackToken || !players->InstallSkillLabel(state->player, 0, 0, duration, attackToken))
    { error.message = "automatic character attack buff failed"; return false; }
    if (subtype == 10)
    {
        CSOLScenarioStatBuff speed;
        speed.group = 3; speed.source = 10; speed.subsource = subtype;
        speed.expires = duration < 0 ? -1 : gpGlobals->time + duration;
        speed.absoluteSpeed = 350;
        const auto token = players->InstallStatBuff(state->player, std::move(speed));
        if (!owner.Current()) return false;
        if (!token || !players->InstallSkillLabel(state->player, 2, 0, duration, token))
        { error.message = "automatic character speed buff failed"; return false; }
        player->ResetMaxSpeed();
        if (!owner.Current()) return false;
    }
    if (!PublishStatusDuration(*session, state->player, 28, duration, {255,255,255}, error) ||
        !owner.Current()) return false;
    CSOLScenarioStatBuff protection;
    protection.group = 3; protection.source = 10; protection.subsource = subtype;
    protection.expires = gpGlobals->time + (subtype == 13 ? 3.0f : 2.0f);
    protection.protection = true; protection.mask = 1;
    const auto protectionToken = players->InstallStatBuff(state->player, std::move(protection));
    if (!owner.Current()) return false;
    if (!protectionToken || !players->HoldSkillProtectionRender(state->player, protectionToken, error) ||
        !owner.Current()) return false;
    if (subtype == 13)
    {
        // This original callback has no IsAlive or team predicate. Its
        // radius is computed as5*5*1549.996826, preserving the SSE order.
        for (int slot=1;slot<=gpGlobals->maxClients;++slot)
        {
            auto* recipient = UTIL_PlayerByIndex(slot);
            if (!owner.Current()) return false;
            if (!recipient || !recipient->pev || (recipient->pev->flags & (FL_DORMANT | FL_KILLME))) continue;
            const EHANDLE handle(recipient);
            const auto target = players->Find(handle);
            if (!target) continue;
            const auto life = target->skillLifeSerial;
            const auto current = [&] { return owner.Current() && players->Find(handle) == target &&
                handle.Get<CBasePlayer>() == recipient && target->skillLifeSerial == life; };
            const auto delta = recipient->pev->origin - origin;
            const float squared = (delta.y*delta.y + delta.x*delta.x) + delta.z*delta.z;
            if (squared > (5.0f*5.0f)*1549.996826171875f) continue;
            CSOLScenarioStatBuff speed;
            speed.group = 3; speed.source = 10; speed.subsource = subtype;
            speed.expires = duration < 0 ? -1 : gpGlobals->time + duration;
            speed.absoluteSpeed = 370;
            const auto token = players->InstallStatBuff(handle, std::move(speed));
            if (!current()) return false;
            if (!token || !players->InstallSkillLabel(handle, 2, 0, duration, token))
            { error.message = "automatic character ally speed failed"; return false; }
            recipient->ResetMaxSpeed();
            if (!current()) return false;
        }
    }
    prepared = true;
    return owner.Current();
}

bool StartOtherSkillEffects(int subtype,
    const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session,
    ActivationState& timers, DamageState& damageState, ConfigError& error, std::uint64_t identity,
    bool* incomingUpgraded)
{
    if (!HasOtherSkillEffects(subtype) && subtype != 46)
    { error.message = "character skill has no concrete area-player effect"; return false; }
    if (!players || !state || !session || !gpGlobals || !error.message.empty()) return false;
    EffectOwner owner(players,state,session);
    const auto current = [&] { return owner.Current(); };
    if (!current()) return false;
    if (subtype == 49 || subtype == 50) timers.effects.duration = 8;
    if (subtype == 49 && (!StartExpandedAreaSkill49(players, state, session, error) || !current())) return false;
    if (subtype == 54)
    {
        // 106EF340 shields other living players first, then installs the
        // caster's separate attack/protection buff. Its duration is fixed.
        constexpr std::array<int, 7> capacities{2000,2500,4500,5000,5500,6000,6500};
        constexpr float radius = 8.0f * 39.37f;
        constexpr float duration = 6;
        const int difficulty = session->Difficulty().SelectedRow();
        const int capacity = difficulty >= 1 && difficulty <= 7 ? capacities[difficulty - 1] : capacities.back();
        auto* caster = state->player.Get<CBasePlayer>();
        for (int slot = 1; slot <= gpGlobals->maxClients; ++slot)
        {
            auto* recipient = UTIL_PlayerByIndex(slot);
            if (!current()) return false;
            if (!recipient || !recipient->pev || recipient == caster || recipient->has_disconnected ||
                (recipient->pev->flags & (FL_DORMANT | FL_KILLME))) continue;
            const EHANDLE handle(recipient);
            const auto target = players->Find(handle);
            if (!target) continue;
            const auto life = target->skillLifeSerial;
            const auto targetCurrent = [&] { return current() && handle.Get<CBasePlayer>() == recipient &&
                players->Find(handle) == target && target->skillLifeSerial == life; };
            const bool alive = recipient->IsAlive();
            if (!current()) return false;
            if (!targetCurrent() || !alive) continue;
            const auto delta = caster->pev->origin - recipient->pev->origin;
            const float squared = (delta.x * delta.x + delta.y * delta.y) + delta.z * delta.z;
            if (squared > radius * radius) continue;
            if (!players->InstallSkillShield(handle, capacity, duration, error) || !current()) return false;
            if (!targetCurrent()) continue;
            if (!PublishStatusDuration(*session, handle, 30, duration, {255,255,255}, error) || !current()) return false;
        }
        CSOLScenarioStatBuff buff;
        buff.group = 3; buff.source = 10; buff.subsource = 54;
        buff.skillIdentity = identity; buff.expires = gpGlobals->time + duration;
        buff.attackRatio = .2f; buff.protection = true;
        const auto token = players->InstallStatBuff(state->player, std::move(buff));
        if (!token || !current()) return false;
        if (!players->InstallSkillLabel(state->player, 0, 0, duration, token) ||
            !players->InstallSkillLabel(state->player, 7, 0, duration, token))
        { error.message = "character team shield status labels failed"; return false; }
        return PublishStatusDuration(*session, state->player, 28, duration, {255,255,255}, error) && current();
    }
    if (subtype == 19 || subtype == 20 || subtype == 31 || subtype == 34 ||
        subtype == 38 || subtype == 40 || subtype == 41 || subtype == 42 || subtype == 43 || subtype == 44 || subtype == 45 || subtype == 46 || subtype == 47 || subtype == 48 ||
        subtype == 49 || subtype == 50)
        return StartSupportSkill(subtype, owner, timers, damageState, identity, error, incomingUpgraded);
    // Their original v44 is proven empty; their actual effects run in v40.
    if (subtype == 10 || subtype == 11 || subtype == 13) return true;
    const float duration = timers.effects.duration;
    const float expires = duration < 0 ? -1 : gpGlobals->time + duration;
    if (!std::isfinite(duration) || !std::isfinite(expires))
    { error.message = "character skill duration is invalid"; return false; }
    const Vector effectOrigin = state->player.Get<CBasePlayer>()->pev->origin;
    if (subtype == 12)
        return PublishStatusDuration(*session, state->player, 28, duration,
            {255,255,255}, error) && current();
    if (subtype == 7 || subtype == 28)
    {
        if (subtype == 28) damageState.nextArea28 = 0;
        if (!StartCharacterAreaDamage(subtype, players, state, session, error) || !current()) return false;
        if (subtype == 7) return true;
    }

    // 1152FF00 enumerates connected, non-dormant players in slot order. The
    // original callbacks then test IsAlive and distance; no team filter is
    // present. Keep their strict (<, type2) versus inclusive (3/4) edge.
    const int first = subtype == 9 ? state->player.Get<CBasePlayer>()->entindex() : 1;
    const int last = subtype == 9 ? first : gpGlobals->maxClients;
    if (!current()) return false;
    for (int index = first; index <= last; ++index)
    {
        if (!current()) return false;
        auto* player = UTIL_PlayerByIndex(index);
        if (!current()) return false;
        if (!player || !player->pev || (player->pev->flags & FL_DORMANT)) continue;
        const EHANDLE handle(player);
        const auto targetState = players->Find(handle);
        const auto targetLife = targetState ? targetState->skillLifeSerial : 0;
        const auto targetCurrent = [&]
        { return current() && handle.Get<CBasePlayer>() == player &&
            players->Find(handle) == targetState && (!targetState || targetState->skillLifeSerial == targetLife); };
        const bool alive = subtype == 15 || player->IsAlive();
        if (!targetCurrent()) return false;
        if (!alive) continue;
        if (!targetState)
        { error.message = "character skill target has no scenario player state"; return false; }
        if (subtype != 9)
        {
            const auto origin = subtype == 28 ? effectOrigin : state->player.Get<CBasePlayer>()->pev->origin;
            const auto delta = player->pev->origin - origin;
            // Preserve the original SSE addition order (y*y+x*x)+z*z.
            const float distance = (delta.y * delta.y + delta.x * delta.x) + delta.z * delta.z;
            const float radius = subtype == 28 ? 10.0f * 39.37f : subtype == 6 ? 300.0f : 200.0f;
            const float radiusSquared = subtype == 15 ? 100.0f * 1549.996826171875f : radius * radius;
            if ((subtype == 2 || subtype == 8) ? !(radiusSquared > distance) :
                distance > radiusSquared) continue;
        }

        if (subtype == 3)
        {
            // 106DAFE0 sends this one-second status first, then *assigns*
            // armor from ClassMod value2. It is not timed protection or a
            // capped armor purchase (1000 is the current original default).
            if (!PublishStatusDuration(*session, handle, 29, 1, {255, 255, 255}, error) ||
                !targetCurrent()) return false;
            player->pev->armorvalue = duration;
            player->m_iKevlar = ARMOR_VESTHELM;
            continue;
        }

        if (subtype == 6)
        {
            // The status duration comes from 11EC8E18 (15), while energy
            // expiry comes from this installed ClassMod's duration.
            if (!PublishStatusDuration(*session, handle, 30, 15, {255, 255, 255}, error) ||
                !targetCurrent()) return false;
            if (!players->InstallSkillShield(handle, SentryShieldCapacity(session->Difficulty().SelectedRow()),
                duration, error) || !targetCurrent()) return false;
            continue;
        }

        CSOLScenarioStatBuff buff;
        buff.group = 3;
        buff.source = 10;
        buff.subsource = subtype;
        buff.expires = expires;
        if (subtype == 2) buff.attackRatio = 0.2f; // 11EC6AA8, 10043990.
        if (subtype == 8 || subtype == 16) buff.attackRatio = 0.3f;
        if (subtype == 28)
        {
            // This separate ally buff lasts its parameter's10 seconds,
            // independently of the active capability's ClassMod duration.
            buff.attackRatio = 0.3f;
            buff.expires = gpGlobals->time + 10.0f;
        }
        if (subtype == 4) buff.absoluteSpeed = 390; // 114EE9A0 channel0.
        if (subtype == 9 || subtype == 15) buff.protection = true; // +21=true, mask+24=0.
        const auto token = players->InstallStatBuff(handle, std::move(buff));
        if (!targetCurrent()) return false;
        if (!token)
        { error.message = "character skill could not install its stat effect"; return false; }
        if (subtype == 2 || subtype == 8 || subtype == 16 || subtype == 28)
            if (!players->InstallSkillLabel(handle, 0, 0, subtype == 28 ? 10.0f : duration, token) || !targetCurrent())
            { if (current()) error.message = "character attack skill label failed"; return false; }
        if (subtype == 4)
        {
            if (players->InstallSkillLabel(handle, 2, 0, duration, token) == 0 || !targetCurrent())
            { if (current()) error.message = "character speed skill label failed"; return false; }
            player->ResetMaxSpeed();
            if (!targetCurrent()) return false;
        }
        else
        {
            int status = subtype == 9 || subtype == 15 ? 40 : 28;
            if (subtype == 16)
            {
                CSOLScenarioStatBuff ammo;
                ammo.group = 3; ammo.source = 10; ammo.subsource = subtype;
                ammo.expires = expires; ammo.infiniteAmmo = true; ammo.extraClip = 2;
                const auto ammoToken = players->InstallStatBuff(handle, std::move(ammo));
                if (!targetCurrent()) return false;
                if (!ammoToken || !players->InstallSkillLabel(handle, 15, 0, duration, ammoToken))
                { error.message = "character blood feast ammo effect failed"; return false; }
                if (!players->RefreshSkillAmmo(handle, true, false, error) || !targetCurrent()) return false;
            }
            if (subtype == 8 && handle.Get<CBasePlayer>() == state->player.Get<CBasePlayer>())
            {
                status = 31;
                if (!players->InstallSkillTemporaryHealth(handle, 1000, duration, error) ||
                    !targetCurrent()) return false;
                if (players->InstallSkillLabel(handle, 16, 0, duration) == 0 || !targetCurrent())
                { if (current()) error.message = "character temporary health label failed"; return false; }
            }
            if (!PublishStatusDuration(*session, handle, status, subtype == 28 ? 10.0f : duration,
                {255, 255, 255}, error) || !targetCurrent()) return false;
            if (subtype == 16 && player == state->player.Get<CBasePlayer>())
                if (!players->InstallSkillLabel(handle, 13, 0, duration) || !targetCurrent())
                { if (current()) error.message = "character blood feast recovery label failed"; return false; }
        }
    }
    if (subtype == 28)
        return PublishSkillCircle(*session, {effectOrigin.x, effectOrigin.y, effectOrigin.z},
            true, error) && current();
    return current();
}

bool AbsorbOtherSkillDamage(const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const EHANDLE& target, const EHANDLE& attacker, float& damage,
    bool& absorbed, ConfigError& error)
{
    absorbed = false;
    if (!players || !players->Active() || !gpGlobals) return false;
    const auto state = players->Find(target);
    auto* player = state ? target.Get<CBasePlayer>() : nullptr;
    if (!player) return true;
    const auto life = state->skillLifeSerial;
    const auto current = [&]
    { return players->Active() && players->Find(target) == state &&
        target.Get<CBasePlayer>() == player && state->skillLifeSerial == life; };
    // 10799693 vBC is IsPlayer. Friendly player attacks do not consume energy
    // when friendly fire is off; self damage continues through the shield.
    if (auto* source = attacker.Get<CBaseEntity>())
    {
        const bool isPlayer = source->IsPlayer();
        if (!current() || attacker.Get<CBaseEntity>() != source) return false;
        if (isPlayer && source != player && friendlyfire.value == 0 &&
            static_cast<CBasePlayer*>(source)->m_iTeam == player->m_iTeam) return true;
    }
    return players->AbsorbSkillShield(target, damage, absorbed, error) && current();
}
}

bool CCSOLScenarioPlayerRuntime::InstallSkillShield(const EHANDLE& handle,
    int amount, float duration, csol::scenario::ConfigError& error,
    csol::scenario::skills::EnergyShield::Callback callback)
{
    const auto state = Resolve(handle);
    if (!state || !gpGlobals) return false;
    return state->skillShield.Install(amount, duration, gpGlobals->time, error, callback);
}

bool CCSOLScenarioPlayerRuntime::AbsorbSkillShield(const EHANDLE& handle,
    float& damage, bool& absorbed, csol::scenario::ConfigError& error)
{
    absorbed = false;
    const auto state = Resolve(handle);
    if (!state || !gpGlobals) return false;
    std::vector<csol::scenario::skills::EnergyShield::Retired> retired;
    if (!state->skillShield.Absorb(damage, gpGlobals->time, absorbed, error, &retired)) return false;
    return RetireSkillShields(handle, retired, error);
}

bool CCSOLScenarioPlayerRuntime::RetireSkillShields(const EHANDLE& handle,
    const std::vector<csol::scenario::skills::EnergyShield::Retired>& retired,
    csol::scenario::ConfigError& error)
{
    using namespace csol::scenario::skills;
    const auto state = Resolve(handle);
    auto* player = state ? handle.Get<CBasePlayer>() : nullptr;
    if (!player || !gpGlobals) return false;
    const auto life = state->skillLifeSerial;
    const auto current = [&] { return Resolve(handle) == state &&
        handle.Get<CBasePlayer>() == player && state->skillLifeSerial == life; };
    // 107994C0 calls the second function with0 on expiry;10799640 calls
    // that same function with1 on a breaking hit. Run after list retirement
    // so notifications can legitimately install/remove another shield.
    for (const auto& entry : retired)
    {
        if (!current()) return false;
        if (entry.callback.kind == 40)
        {
            if (!entry.broken) continue;
            float remaining{};
            std::vector<std::uint64_t> tokens;
            for (const auto& buff : state->buffs)
                if (buff.skillIdentity == entry.callback.identity)
                {
                    tokens.push_back(buff.token);
                    if (buff.Active(gpGlobals->time))
                        remaining = std::max(remaining, buff.expires <= 0 ?
                            std::numeric_limits<float>::max() : buff.expires - gpGlobals->time);
                }
            for (auto token : tokens)
                if (!RemoveStatBuff(handle, token) || !current()) return false;
            CSOLScenarioStatBuff speed;
            speed.group = 3; speed.source = 10; speed.subsource = 40;
            speed.speed = 110; speed.expires = gpGlobals->time + (remaining + 3);
            const auto speedToken = InstallStatBuff(handle, std::move(speed));
            if (!speedToken || !current() || !HoldSkillProtectionRender(handle, speedToken, error) || !current()) return false;
            CSOLScenarioStatBuff protection;
            protection.group = 3; protection.source = 10; protection.subsource = 40;
            protection.protection = true; protection.expires = gpGlobals->time + 3;
            const auto token = InstallStatBuff(handle, std::move(protection));
            if (!token || !current() || !HoldSkillProtectionRender(handle, token, error) || !current()) return false;
            player->ResetMaxSpeed();
            if (!current() || !PublishStatusDuration(*m_session, handle, 40, 3, {255,255,255}, error) || !current()) return false;
        }
        else if (entry.callback.kind == 41 || entry.callback.kind == 42)
        {
            CSOLScenarioStatBuff buff;
            buff.group = 3; buff.source = 10; buff.subsource = entry.callback.kind;
            buff.expires = gpGlobals->time + 6;
            const bool speed = entry.callback.kind == 41 && entry.broken;
            const bool attack = entry.callback.kind == 41 ? !entry.broken : entry.broken;
            if (speed) buff.speed = 50;
            else if (attack) buff.attackRatio = 0.4f;
            else { buff.periodicRecovery = player->pev->max_health * 0.05f; buff.periodicInterval = 1; }
            const auto token = InstallStatBuff(handle, std::move(buff));
            if (!token || !current()) return false;
            if ((speed || attack) && (!HoldSkillProtectionRender(handle, token, error) || !current())) return false;
            if (speed) { player->ResetMaxSpeed(); if (!current()) return false; }
            if (!PublishStatusDuration(*m_session, handle, speed ? 29 : attack ? 28 : 27,
                6, {255,255,255}, error) || !current()) return false;
        }
        else if (entry.callback.kind == 26 || entry.callback.kind == 27)
        {
            //106E61F0 ignores the broken flag and removes every buff with
            //the captured identity for this recipient on both exit paths.
            std::vector<std::uint64_t> tokens;
            for (const auto& buff : state->buffs)
                if (buff.skillIdentity == entry.callback.identity) tokens.push_back(buff.token);
            for (const auto token : tokens)
            {
                if (!current()) return false;
                const auto found = std::find_if(state->buffs.begin(), state->buffs.end(),
                    [token](const auto& buff) { return buff.token == token; });
                if (found != state->buffs.end() && !RemoveStatBuff(handle, token)) return false;
            }
        }
        else if (entry.callback.kind == 53)
        {
            //106DE490 clears the status before removing the tagged aura,
            // then deals area damage; its model and sound are emitted last.
            if (!PublishClearSkillStatus(*m_session, handle, 30, error) || !current()) return false;
            std::vector<std::uint64_t> tokens;
            for (const auto& buff : state->buffs)
                if (buff.skillIdentity == entry.callback.identity) tokens.push_back(buff.token);
            for (const auto token : tokens)
            {
                if (!current()) return false;
                const auto found = std::find_if(state->buffs.begin(), state->buffs.end(),
                    [token](const auto& buff) { return buff.token == token; });
                if (found != state->buffs.end() && !RemoveStatBuff(handle, token)) return false;
            }
            if (!current() || !EndDragonShieldArea53(shared_from_this(), state, m_session, error) ||
                !current()) return false;
            if (!PublishDragonShieldRetirementEffect(*m_session, handle, error) || !current()) return false;
        }
        else { error.message = "character shield retirement callback is unknown"; return false; }
    }
    return current();
}
