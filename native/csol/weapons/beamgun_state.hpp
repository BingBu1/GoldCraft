// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace csol
{
enum class BeamCharge { None, Alpha, Beta };
enum class BeamDive { None, Start, Loop, Landed };
enum class BeamPending { None, First = 2, Second = 3 };
enum class BeamAttack { None, Empty, Primary, Secondary1, Secondary2, ChargedPrimary, DiveStart, DiveEnd, FullCharge };
enum BeamAnimation
{
    BeamIdle, BeamShoot, BeamShootB1, BeamShootB2, BeamReload, BeamDraw,
    BeamChargeEnter, BeamChargeExit, BeamChargeIdle, BeamChargeShoot,
    BeamChargeShootExit, BeamDiveStart, BeamDiveLoop, BeamDiveEnd,
    BeamDiveEndExit, BeamChargeReload, BeamChargeDraw
};

// One operation's presentation/physics requests. Applying damage, tracing and
// spawning effects belongs to the server/client adapters, not this state model.
struct BeamAction
{
    int animation = -1;
    int playerAnimation = -1;
    int eventMode = -1;
    int eventPhase = 0;
    BeamAttack attack = BeamAttack::None;
    bool clearGround = false;
    bool stopVelocity = false;
    bool descend = false;
};

// Shared by both skins. Timers are remaining seconds, matching GoldSrc's
// UseDecrement weapons. The adapter owns inventory, ammo transfer, command
// scheduling and replay suppression. No pointers or engine ABI live here.
struct BeamWeaponState
{
    int clip = 0;
    int energy = 0;
    bool reloading = false;
    bool energyFull = false;
    BeamCharge charge = BeamCharge::None;
    BeamDive dive = BeamDive::None;
    BeamPending pending = BeamPending::None;
    float nextPrimary = 0;
    float nextSecondary = 0;
    float idleTime = 0;
    float chargeTime = 0;
    float comboTime = 0;
    float delayedTime = 0;

    bool Charged() const;
    BeamAction Primary(bool fireOnEmpty);
    BeamAction Secondary();
    BeamAction FullCharge(unsigned pressedButtons);
    BeamAction DelayedAttack();
    BeamAction ExpireCharge();
    BeamAction UpdateDive(bool onGround, int moveType);
    BeamAction Idle();
    int ReloadAnimation() const;
    BeamAction ReloadStarted(bool succeeded);
    int BeginDeploy();
    void FinishDeploy(bool succeeded);
    void Attached(bool succeeded);
    void Holster();
    void Removed();
    void ResetAction();
    // Only the three CSOL extension timers. The engine already advances the
    // ordinary attack/idle countdowns; decrementing those twice breaks cadence.
    void Decrement(float seconds);
};
}
