// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "native/guest/cof/arsenal.hpp"
#include "native/guest/cof/catalog.hpp"

#include <algorithm>

namespace csol::server::cof
{
struct KickBackProfile
{
    float pitch, yaw, pitchGrowth, yawGrowth, pitchLimit, yawLimit;
    int directionChange;
};

// Use the host's light and heavy firearm families as the balance reference.
// Each group contains standing, crouching, moving and airborne limits.
inline constexpr KickBackProfile TmpRecoil[]{
    {.725f, .375f, .15f, .025f, 2.75f, 2.25f, 9},
    {.7f, .35f, .125f, .025f, 2.5f, 2.f, 10},
    {.8f, .4f, .2f, .03f, 3.f, 2.5f, 7},
    {1.1f, .5f, .35f, .045f, 4.5f, 3.5f, 6},
};
inline constexpr KickBackProfile Mp5Recoil[]{
    {.25f, .175f, .125f, .02f, 2.25f, 1.25f, 10},
    {.225f, .15f, .1f, .015f, 2.f, 1.f, 10},
    {.5f, .275f, .2f, .03f, 3.f, 2.f, 10},
    {.9f, .475f, .35f, .0425f, 5.f, 3.f, 6},
};
inline constexpr KickBackProfile M16Recoil[]{
    {.65f, .35f, .25f, .015f, 3.5f, 2.25f, 7},
    {.6f, .3f, .2f, .0125f, 3.25f, 2.f, 7},
    {1.f, .45f, .28f, .045f, 3.75f, 3.f, 7},
    {1.2f, .5f, .23f, .15f, 5.5f, 3.5f, 6},
};
inline constexpr KickBackProfile FamasRecoil[]{
    {.625f, .375f, .25f, .0125f, 3.5f, 2.25f, 8},
    {.575f, .325f, .2f, .011f, 3.25f, 2.f, 8},
    {1.f, .45f, .275f, .05f, 4.f, 2.5f, 7},
    {1.25f, .45f, .22f, .18f, 5.5f, 4.f, 5},
};

inline void ResetFirearmRecoil(CBasePlayerWeapon& weapon) noexcept
{
    weapon.m_iShotsFired = 0;
    weapon.m_iDirection = 0;
    weapon.m_bDelayFire = false;
    weapon.m_flDecreaseShotsFired = 0.f;
}

// CoF weapons own ItemPostFrame. Decay the automatic shot count at the same
// command boundary as the host, without running its weapon input a second time.
inline void RecoverFirearmRecoil(CBasePlayerWeapon& weapon, int buttons, float now) noexcept
{
    if (!weapon.m_iShotsFired) return;
    if (weapon.m_fInReload)
    {
        ResetFirearmRecoil(weapon);
        return;
    }
    if (buttons & (IN_ATTACK | IN_ATTACK2)) return;
    if (weapon.m_bDelayFire)
    {
        weapon.m_bDelayFire = false;
        weapon.m_iShotsFired = std::min(weapon.m_iShotsFired, 15);
        weapon.m_flDecreaseShotsFired = now + .4f;
    }
    else if (weapon.m_flDecreaseShotsFired < now)
    {
        --weapon.m_iShotsFired;
        weapon.m_flDecreaseShotsFired = now + .0225f;
    }
}

inline void ApplyFirearmRecoil(CBasePlayerWeapon& weapon) noexcept
{
    using guest::cof::Firearm;
    using guest::cof::FindFirearm;
    auto& owner = *weapon.m_pPlayer;
    auto& state = *owner.pev;
    const bool grounded = (state.flags & FL_ONGROUND) != 0;
    const KickBackProfile* profiles;
    bool movementFirst = false;
    switch (weapon.m_iId)
    {
    case FindFirearm(Firearm::TMP)->hostId: profiles = TmpRecoil; break;
    case FindFirearm(Firearm::MP5)->hostId: profiles = Mp5Recoil; break;
    case FindFirearm(Firearm::M16)->hostId: profiles = M16Recoil; movementFirst = true; break;
    case FindFirearm(Firearm::Famas)->hostId: profiles = FamasRecoil; movementFirst = true; break;
    case FindFirearm(Firearm::G43)->hostId:
        state.punchangle.x -= UTIL_SharedRandomFloat(owner.random_seed + 4u, .75f, 1.25f) +
            state.punchangle.x * .25f;
        state.punchangle.y += UTIL_SharedRandomFloat(owner.random_seed + 5u, -.75f, .75f);
        return;
    case FindFirearm(Firearm::Shotgun)->hostId:
        state.punchangle.x -= grounded ? UTIL_SharedRandomLong(owner.random_seed + 1u, 4, 6) :
            UTIL_SharedRandomLong(owner.random_seed + 1u, 8, 11);
        return;
    case FindFirearm(Firearm::Glock)->hostId:
    case FindFirearm(Firearm::P345)->hostId:
    case FindFirearm(Firearm::Revolver)->hostId:
    case FindFirearm(Firearm::VP70)->hostId:
    case guest::cof::arsenal::Browning:
    case guest::cof::arsenal::Rifle:
        state.punchangle.x -= 2.f;
        return;
    default:
        return;
    }
    const bool moving = state.velocity.x != 0.f || state.velocity.y != 0.f;
    const auto& profile = profiles[moving && (movementFirst || grounded) ? 2 :
        !grounded ? 3 : (state.flags & FL_DUCKING) ? 1 : 0];
    weapon.m_bDelayFire = true;
    if (weapon.m_iShotsFired < 255) ++weapon.m_iShotsFired;
    weapon.KickBack(profile.pitch, profile.yaw, profile.pitchGrowth, profile.yawGrowth,
        profile.pitchLimit, profile.yawLimit, profile.directionChange);
}
}
