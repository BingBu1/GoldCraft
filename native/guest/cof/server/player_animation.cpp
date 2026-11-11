// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "player_animation.hpp"

#include <cmath>

namespace guest::cof::server
{
std::optional<PlayerAnimationPlan> RecommendedPistolAnimationPlan(Pistol weapon,
                                                                PistolAnimationRig rig) noexcept
{
    if (weapon >= Pistol::Count || (rig != PistolAnimationRig::Stock && rig != PistolAnimationRig::CSO))
        return std::nullopt;
    const PlayerActionLabels melee = rig == PistolAnimationRig::CSO ?
        PlayerActionLabels{"ref_shoot_nataknife", "crouch_shoot_nataknife"} :
        PlayerActionLabels{"ref_shoot_knife", "crouch_shoot_knife"};
    return PlayerAnimationPlan{{
        {"ref_aim_onehanded", "crouch_aim_onehanded"},
        {"ref_aim_onehanded", "crouch_aim_onehanded"},
        {"ref_shoot_onehanded", "crouch_shoot_onehanded"},
        {"ref_reload_onehanded", "crouch_reload_onehanded"},
        {"ref_aim_onehanded", "crouch_aim_onehanded"}, melee}};
}

std::optional<PlayerAnimationPlan> RecommendedLongGunAnimationPlan(LongGun weapon,
                                                                 PistolAnimationRig rig) noexcept
{
    if (weapon >= LongGun::Count || (rig != PistolAnimationRig::Stock && rig != PistolAnimationRig::CSO))
        return std::nullopt;
    PlayerActionLabels aim, fire, reload;
    const bool compact = weapon == LongGun::TMP || weapon == LongGun::MP5;
    if (compact)
    {
        aim = {"ref_aim_mp5", "crouch_aim_mp5"};
        fire = {"ref_shoot_mp5", "crouch_shoot_mp5"};
        reload = {"ref_reload_mp5", "crouch_reload_mp5"};
    }
    else if (weapon == LongGun::M16 && rig == PistolAnimationRig::CSO)
    {
        aim = {"ref_aim_m16a1ep", "crouch_aim_m16a1ep"};
        fire = {"ref_shoot_m16a1ep", "crouch_shoot_m16a1ep"};
        reload = {"ref_reload_m16a1ep", "crouch_reload_m16a1ep"};
    }
    else if (weapon == LongGun::Shotgun)
    {
        aim = {"ref_aim_shotgun", "crouch_aim_shotgun"};
        fire = {"ref_shoot_shotgun", "crouch_shoot_shotgun"};
        reload = {"ref_reload_shotgun", "crouch_reload_shotgun"};
    }
    else
    {
        aim = {"ref_aim_rifle", "crouch_aim_rifle"};
        fire = {"ref_shoot_rifle", "crouch_shoot_rifle"};
        reload = {"ref_reload_rifle", "crouch_reload_rifle"};
    }
    const PlayerActionLabels melee = rig == PistolAnimationRig::Stock ?
        PlayerActionLabels{"ref_shoot_knife", "crouch_shoot_knife"} : compact ?
        PlayerActionLabels{"ref_shoot_nataknife", "crouch_shoot_nataknife"} :
        PlayerActionLabels{"ref_shoot2_m16a1ep", "crouch_shoot2_m16a1ep"};
    return PlayerAnimationPlan{{aim, aim, fire, reload, aim, melee}};
}

void PlayerAnimationAdapter::Reset() noexcept
{
    bindings_.Reset();
    owner_ = nullptr;
    ownerSerial_ = modelIndex_ = 0;
    weapon_ = WeaponCount;
}

bool PlayerAnimationAdapter::Prepare(CBasePlayer& player, std::span<const std::byte> model,
                                    PlayerModelGeneration generation, Pistol weapon,
                                    const PlayerAnimationPlan& plan, PlayerActionLabelCompare compare,
                                    std::string& error)
{
    return PrepareWeapon(player, model, generation,
                         weapon < Pistol::Count ? static_cast<unsigned>(weapon) : WeaponCount,
                         plan, compare, error);
}

bool PlayerAnimationAdapter::Prepare(CBasePlayer& player, std::span<const std::byte> model,
                                    PlayerModelGeneration generation, LongGun weapon,
                                    const PlayerAnimationPlan& plan, PlayerActionLabelCompare compare,
                                    std::string& error)
{
    return PrepareWeapon(player, model, generation,
                         weapon < LongGun::Count ? static_cast<unsigned>(Pistol::Count) + static_cast<unsigned>(weapon) : WeaponCount,
                         plan, compare, error);
}

bool PlayerAnimationAdapter::PrepareWeapon(CBasePlayer& player, std::span<const std::byte> model,
                                          PlayerModelGeneration generation, unsigned weapon,
                                          const PlayerAnimationPlan& plan, PlayerActionLabelCompare compare,
                                          std::string& error)
{
    Reset();
    if (!player.pev || !player.edict() || player.pev->modelindex <= 0 || weapon >= WeaponCount ||
        GET_MODEL_PTR(player.edict()) != model.data())
    {
        error = "Player animation requires a current owner, model and weapon";
        return false;
    }
    if (!bindings_.Bind(model, generation, plan, compare, error)) return false;
    owner_ = &player;
    ownerSerial_ = player.edict()->serialnumber;
    modelIndex_ = player.pev->modelindex;
    weapon_ = weapon;
    return true;
}

std::optional<PlayerActionSequence> PlayerAnimationAdapter::Select(PlayerModelGeneration current,
                                                                 Pistol weapon, PlayerAction action,
                                                                 bool crouching) const noexcept
{
    if (weapon >= Pistol::Count) return std::nullopt;
    return SelectWeapon(current, static_cast<unsigned>(weapon), action, crouching);
}

std::optional<PlayerActionSequence> PlayerAnimationAdapter::Select(PlayerModelGeneration current,
                                                                 LongGun weapon, PlayerAction action,
                                                                 bool crouching) const noexcept
{
    if (weapon >= LongGun::Count) return std::nullopt;
    return SelectWeapon(current, static_cast<unsigned>(Pistol::Count) + static_cast<unsigned>(weapon), action, crouching);
}

std::optional<PlayerActionSequence> PlayerAnimationAdapter::SelectWeapon(PlayerModelGeneration current,
                                                                       unsigned weapon, PlayerAction action,
                                                                       bool crouching) const noexcept
{
    if (weapon != weapon_ || weapon >= WeaponCount) return std::nullopt;
    return bindings_.Select(current, static_cast<std::size_t>(action), crouching);
}

PlayerAnimationResult PlayerAnimationAdapter::Apply(CBasePlayer& player, PlayerModelGeneration current,
                                                   Pistol weapon, PlayerAction action, float now) const
{
    return ApplyWeapon(player, current, weapon < Pistol::Count ? static_cast<unsigned>(weapon) : WeaponCount, action, now);
}

PlayerAnimationResult PlayerAnimationAdapter::Apply(CBasePlayer& player, PlayerModelGeneration current,
                                                   LongGun weapon, PlayerAction action, float now) const
{
    return ApplyWeapon(player, current,
                       weapon < LongGun::Count ? static_cast<unsigned>(Pistol::Count) + static_cast<unsigned>(weapon) : WeaponCount,
                       action, now);
}

PlayerAnimationResult PlayerAnimationAdapter::ApplyWeapon(CBasePlayer& player, PlayerModelGeneration current,
                                                         unsigned weapon, PlayerAction action, float now) const
{
    if (&player != owner_ || !player.pev || !player.edict() || !gpGlobals ||
        player.edict()->serialnumber != ownerSerial_ || player.pev->modelindex != modelIndex_ ||
        !std::isfinite(now) || now != gpGlobals->time || !std::isfinite(player.pev->health) ||
        player.pev->health <= 0.0f || player.pev->deadflag != DEAD_NO)
        return PlayerAnimationResult::Rejected;
    const auto selected = SelectWeapon(current, weapon, action, (player.pev->flags & FL_DUCKING) != 0);
    if (!selected) return PlayerAnimationResult::Rejected;

    // Keep native movement/life and damage reactions in charge. Caller can
    // retry the ordinary pose after that native activity has completed.
    if ((player.pev->flags & FL_FROZEN) || player.m_flFlinchTime > now ||
        player.m_Activity == ACT_SWIM || player.m_Activity == ACT_DIESIMPLE ||
        player.m_Activity == ACT_HOVER || player.m_Activity == ACT_HOP || player.m_Activity == ACT_LEAP)
        return PlayerAnimationResult::PreservedNative;
    const bool shot = action == PlayerAction::Fire;
    const bool melee = action == PlayerAction::WeaponMelee;
    const bool reload = action == PlayerAction::Reload;
    const bool pose = !shot && !melee && !reload;
    if (pose && !player.m_fSequenceFinished &&
        (player.m_Activity == ACT_RANGE_ATTACK1 || player.m_Activity == ACT_RANGE_ATTACK2 ||
         player.m_Activity == ACT_RELOAD || player.m_Activity == ACT_FLINCH ||
         player.m_Activity == ACT_LARGE_FLINCH))
        return PlayerAnimationResult::PreservedNative;

    const bool changed = player.pev->sequence != selected->sequence;
    const bool restart = shot || melee || changed || (reload && !player.m_fSequenceLoops);
    if (shot || melee) player.m_flLastFired = now;
    if (shot || melee)
        player.m_Activity = player.m_IdealActivity = ACT_RANGE_ATTACK1;
    else if (reload)
        player.m_Activity = player.m_IdealActivity = ACT_RELOAD;
    else
        player.m_Activity = player.m_IdealActivity = player.m_Activity == ACT_RUN ? ACT_RUN : ACT_WALK;
    // Pose requests preserve native run, gait and speed, and release a finished
    // attack/reload activity before ResetSequenceInfo clears its finished bit.
    if (reload && !player.m_fSequenceLoops) player.pev->effects |= EF_NOINTERP;
    if (restart)
    {
        player.pev->sequence = selected->sequence;
        player.pev->frame = 0.0f;
        player.ResetSequenceInfo();
    }
    if (TheBots)
    {
        if (shot) TheBots->OnEvent(EVENT_WEAPON_FIRED, &player);
        else if (reload) TheBots->OnEvent(EVENT_WEAPON_RELOADED, &player);
    }
    return restart ? PlayerAnimationResult::Applied : PlayerAnimationResult::Unchanged;
}
}
