// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_player_runtime.h"

#include <cmath>

#pragma clang fp contract(off)

bool CCSOLScenarioPlayerRuntime::TryMonsterFrost(const EHANDLE& handle, float duration,
    float speed, bool& applied, csol::scenario::ConfigError& error)
{
    return AddMonsterMotionStatus(handle, 53, duration, speed, -9999.0f, applied, error);
}

bool CCSOLScenarioPlayerRuntime::AddMonsterMotionStatus(const EHANDLE& handle, int id,
    float duration, float speed, float gravity, bool& applied, csol::scenario::ConfigError& error)
{
    applied = false;
    const auto keepAlive = shared_from_this();
    const auto state = Resolve(handle);
    auto* player = state ? dynamic_cast<CBasePlayer*>(handle.Get<CBaseEntity>()) : nullptr;
    if (!player || !player->pev || !gpGlobals) return false;
    const auto life = state->monsterLifeSerial;
    const auto current = [&]
    { return Resolve(handle) == state && state->monsterLifeSerial == life && handle.Get<CBaseEntity>() == player; };
    const float now = gpGlobals->time;
    if ((id != 17 && id != 53) || !std::isfinite(now) || !std::isfinite(duration) || !std::isfinite(speed) ||
        !std::isfinite(gravity) ||
        !std::isfinite(now + duration))
    { error.message = "scenario player motion status parameters are invalid"; return false; }
    if (!(duration > 0)) return true;
    // Scenario ClassMod/Addon IPlayerModule vC0 returns false, v80 and v2C
    // are no-ops. Do not confuse active-skill virtuals with those callbacks,
    // or apply the zombie-mode module overrides to this mode15 entry.
    if (csol::scenario::PlayerSlowImmune(std::span<const CSOLScenarioStatBuff>(state->buffs), now, -1))
        return true;
    applied = state->monsterMotion.Add({id, now + duration, speed, gravity, -1, -1});
    if (id == 17 && applied) state->monsterTrapUntil = now + duration;
    // The original wrapper adds the label even when a shorter/equal repeat
    // did not extend the ring. Natural label expiry is independent of it.
    const auto label = InstallSkillLabel(handle, speed <= 2 ? 4 : 3, 0, duration);
    if (!label)
    { error.message = "scenario player motion status label could not be installed"; return false; }
    state->labels.back().motionStatus = id;
    player->ResetMaxSpeed();
    return current(); // Service success, not the original AddStatus bool.
}

bool CCSOLScenarioPlayerRuntime::ApplyMonsterStatusMovement(CBasePlayer& player,
    float weaponSpeed, float now, bool& active, csol::scenario::ConfigError& error)
{
    active = false;
    const auto state = Resolve(EHANDLE(&player));
    if (!state) return true;
    if (!std::isfinite(now) || !std::isfinite(weaponSpeed))
    { error.message = "scenario player movement clock or weapon speed is invalid"; return false; }
    if (state->monsterMotion.Empty())
    {
        state->monsterMotionApplied = false;
        state->monsterMotionWeaponSpeed = weaponSpeed;
        return true;
    }
    state->monsterMotion.ExpireEnds(now);
    if (!state->monsterMotion.Contains(17)) state->monsterTrapUntil = 0;
    const float base = state->monsterMotionWeaponSpeed > 0 ? state->monsterMotionWeaponSpeed : 240.0f;
    const auto selected = state->monsterMotion.Select(base);
    float ratio = 1;
    if (selected.speed >= 1 && !csol::scenario::PlayerSlowRatio(
        std::span<const CSOLScenarioStatBuff>(state->buffs), now, selected.group, ratio))
    { error.message = "scenario player movement resistance is invalid"; return false; }
    const float speed = csol::scenario::PlayerStatusSpeed(base, selected.speed, ratio);
    if (!std::isfinite(speed))
    { error.message = "scenario player movement result is invalid"; return false; }
    player.pev->maxspeed = speed;
    // Frost53 preserves gravity. The default selection writes1, including
    // the pass that removes the last expired endpoint.
    if (selected.gravity != -9999.0f)
    {
        float gravityRatio = 1;
        if (!csol::scenario::PlayerGravityRatio(std::span<const CSOLScenarioStatBuff>(state->buffs),
            now, selected.group, gravityRatio))
        { error.message = "scenario player gravity resistance is invalid"; return false; }
        const float gravity = 1.0f + (selected.gravity - 1.0f) * gravityRatio;
        if (!std::isfinite(gravity))
        { error.message = "scenario player gravity result is invalid"; return false; }
        player.pev->gravity = gravity;
        state->monsterTrapMovementApplied = selected.id == 17;
    }
    active = state->monsterMotion.AnyActive(now);
    state->monsterMotionApplied = active;
    if (active && player.m_pActiveItem && weaponSpeed > 0 && player.pev->maxspeed > weaponSpeed)
        player.pev->maxspeed = weaponSpeed;
    if (!active) state->monsterMotionWeaponSpeed = weaponSpeed;
    return true;
}

bool CCSOLScenarioPlayerRuntime::RemoveMonsterFrost(const EHANDLE& handle)
{
    const auto keepAlive = shared_from_this();
    const auto state = Resolve(handle);
    auto* player = state ? dynamic_cast<CBasePlayer*>(handle.Get<CBaseEntity>()) : nullptr;
    if (!player) return true;
    const auto life = state->monsterLifeSerial;
    state->monsterMotion.Remove(53);
    if (std::erase_if(state->labels, [](const auto& label) { return label.motionStatus == 53; }))
    { state->labelsDirty = true; ++state->buffsRevision; }
    player->ResetMaxSpeed();
    return Resolve(handle) == state && state->monsterLifeSerial == life && handle.Get<CBaseEntity>() == player;
}
