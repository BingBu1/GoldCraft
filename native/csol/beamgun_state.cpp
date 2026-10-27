// SPDX-License-Identifier: GPL-3.0-or-later
#include "beamgun_state.hpp"
#include <algorithm>
#include <cmath>

namespace csol
{
bool BeamWeaponState::Charged() const
{
    return charge == BeamCharge::Alpha || charge == BeamCharge::Beta;
}

BeamAction BeamWeaponState::Primary(bool fireOnEmpty)
{
    BeamAction action;
    if (Charged())
    {
        if (chargeTime < 1.4f)
            charge = BeamCharge::None;
        action.animation = Charged() ? BeamChargeShoot : BeamChargeShootExit;
        action.playerAnimation = 5;
        action.attack = BeamAttack::ChargedPrimary;
        nextPrimary = nextSecondary = 1.4f;
        idleTime = 2.53f;
    }
    else if (clip > 0)
    {
        --clip;
        action.animation = BeamShoot;
        action.playerAnimation = 5;
        action.attack = BeamAttack::Primary;
        nextPrimary = nextSecondary = 0.3f;
        idleTime = 1.03f;
    }
    else if (fireOnEmpty)
    {
        action.attack = BeamAttack::Empty;
        nextPrimary = 0.2f;
    }
    return action;
}

BeamAction BeamWeaponState::Secondary()
{
    BeamAction action;
    if (Charged())
    {
        if (chargeTime < 0)
            return action;
        action.animation = BeamDiveStart;
        action.playerAnimation = 5;
        action.eventMode = 4;
        action.eventPhase = 1;
        action.attack = BeamAttack::DiveStart;
        action.clearGround = true;
        nextPrimary = nextSecondary = 0.4f + 0.8f;
        idleTime = 0.4f + 1.03f;
        dive = BeamDive::Start;
        comboTime = 0.4f;
        delayedTime = 0.2f;
    }
    else if (comboTime > 0)
    {
        action.animation = BeamShootB2;
        pending = BeamPending::Second;
        nextPrimary = nextSecondary = 0.8f;
        idleTime = 1.53f;
        comboTime = 0;
        delayedTime = 0.3f;
    }
    else
    {
        action.animation = BeamShootB1;
        pending = BeamPending::First;
        nextPrimary = nextSecondary = 1.0f;
        idleTime = 2.03f;
        comboTime = 1.2f;
        delayedTime = 0.2f;
    }
    return action;
}

BeamAction BeamWeaponState::FullCharge(unsigned pressedButtons)
{
    BeamAction action;
    energyFull = energy >= 100;
    // This is the pressed-button field, not a newly fabricated edge from the
    // current buttons. The engine adapter must supply its actual input state.
    if (!energyFull || Charged() || reloading || (pressedButtons & 0x801) != 0x801)
        return action;
    action.animation = BeamChargeEnter;
    action.eventMode = 6;
    action.attack = BeamAttack::FullCharge;
    nextPrimary = nextSecondary = 0.3f;
    idleTime = 0.33f;
    charge = BeamCharge::Beta;
    chargeTime = 7.0f;
    pending = BeamPending::None;
    delayedTime = 0;
    energy -= 100;
    return action;
}

BeamAction BeamWeaponState::DelayedAttack()
{
    BeamAction action;
    if (pending == BeamPending::None || delayedTime > 0)
        return action;
    if (pending == BeamPending::First)
    {
        action.attack = BeamAttack::Secondary1;
        action.playerAnimation = 6;
        action.eventMode = 1;
    }
    else if (pending == BeamPending::Second)
    {
        action.attack = BeamAttack::Secondary2;
        action.playerAnimation = 5;
        charge = BeamCharge::Alpha;
        chargeTime = 2.5f;
    }
    pending = BeamPending::None;
    return action;
}

BeamAction BeamWeaponState::ExpireCharge()
{
    BeamAction action;
    if (Charged() && dive == BeamDive::None && chargeTime < 0)
    {
        action.animation = BeamChargeExit;
        // A holstered dive can expire after it is drawn again. Its remaining
        // attack delay still applies even though the charge animation ends.
        nextPrimary = std::max(nextPrimary, 0.33f);
        nextSecondary = std::max(nextSecondary, 0.33f);
        idleTime = 0.33f;
        charge = BeamCharge::None;
    }
    return action;
}

BeamAction BeamWeaponState::UpdateDive(bool onGround, int moveType)
{
    BeamAction action;
    if (dive == BeamDive::Landed)
    {
        dive = BeamDive::None;
        if (chargeTime < 0.8f)
            charge = BeamCharge::None;
        action.animation = chargeTime < 0.8f ? BeamDiveEndExit : BeamDiveEnd;
        action.attack = BeamAttack::DiveEnd;
        nextPrimary = nextSecondary = 0.8f;
        idleTime = 1.03f;
        comboTime = delayedTime = 0;
    }
    else if (dive == BeamDive::Start || dive == BeamDive::Loop)
    {
        if (comboTime < 0)
            action.descend = true;
        else if (comboTime + 1.03f < 0.4f && dive == BeamDive::Start)
        {
            dive = BeamDive::Loop;
            action.animation = BeamDiveLoop;
        }
        // The original helper is movetype == MOVETYPE_FLY (5). It is not
        // CheckBottom, and a nearby floor alone must not end the aerial move.
        if (delayedTime < 0 && (onGround || moveType == 5))
        {
            dive = BeamDive::Landed;
            action.stopVelocity = true;
        }
    }
    return action;
}

BeamAction BeamWeaponState::Idle()
{
    BeamAction action;
    if (idleTime <= 0)
    {
        action.animation = Charged() ? BeamChargeIdle : BeamIdle;
        idleTime = 20.150001525878906f;
    }
    return action;
}

int BeamWeaponState::ReloadAnimation() const
{
    return Charged() ? BeamChargeReload : BeamReload;
}

BeamAction BeamWeaponState::ReloadStarted(bool succeeded)
{
    BeamAction action;
    if (succeeded)
    {
        action.playerAnimation = 9;
        idleTime = 2.37f;
    }
    return action;
}

int BeamWeaponState::BeginDeploy()
{
    if (chargeTime < 0)
        charge = BeamCharge::None;
    return Charged() ? BeamChargeDraw : BeamDraw;
}

void BeamWeaponState::FinishDeploy(bool succeeded)
{
    if (succeeded)
        ResetAction();
}

void BeamWeaponState::Attached(bool succeeded)
{
    if (succeeded)
    {
        charge = BeamCharge::None;
        chargeTime = 0;
    }
}

void BeamWeaponState::Holster()
{
    // CSO's server deactivate callback preserves both charge modes. The
    // countdown keeps advancing in inventory; BeginDeploy handles expiry.
    ResetAction();
    reloading = false;
}

void BeamWeaponState::Removed()
{
    charge = BeamCharge::None;
    chargeTime = 0;
}

void BeamWeaponState::ResetAction()
{
    pending = BeamPending::None;
    dive = BeamDive::None;
    comboTime = delayedTime = 0;
    idleTime = 1.37f;
    energyFull = energy >= 100;
}

void BeamWeaponState::Decrement(float seconds)
{
    if (!std::isfinite(seconds) || seconds < 0)
        return;
    chargeTime = std::max(-0.001f, chargeTime - seconds);
    comboTime = std::max(-0.001f, comboTime - seconds);
    delayedTime = std::max(-0.001f, delayedTime - seconds);
}
}
