// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

namespace csol::skull1 {

inline constexpr int Id=130, Clip=7, Reserve=35, AmmoBuyAmount=7, AmmoCost=70;
inline constexpr int Penetration=3, OriginalBulletType=27, PenetrationPower=30;
inline constexpr float PenetrationDistance=1000;
inline constexpr float Distance=4096, ReloadTime=2.5f, MaxSpeed=250;
inline constexpr float DeployTime=.75f, DeployIdle=1.5f, FireIdle=1.f, IdleTime=1.6f;
inline constexpr const char* Classname="weapon_skull1";
inline constexpr const char* AmmoName="50AZ";
inline constexpr const char* ViewModel="models/v_skull1.mdl";
inline constexpr const char* PlayerModel="models/p_skull1.mdl";
inline constexpr const char* WorldModel="models/w_skull1.mdl";
inline constexpr const char* Event="events/skull1.sc";
inline constexpr const char* RapidEvent="events/skull1_rapid.sc";
inline constexpr const char* FireSounds[]{"weapons/skull1-1.wav","weapons/skull1-2.wav"};
inline constexpr float PackShots(int shots){return float(std::clamp(shots,0,1000000))/128.f;}
inline int UnpackShots(float packed){return std::isfinite(packed)?int(std::clamp(packed*128.f,0.f,1000000.f)):0;}
inline float PackFireAge(float lastFire,float now){return std::clamp(lastFire-now,-1.f,0.f);}
inline constexpr std::array<std::string_view,6> DamageFields{
    "Damage","DamageZombie","DamageScenario",
    "SP_Damage","SP_DamageZombie","SP_DamageScenario"};
enum Animation { Idle, IdleEmpty, Shoot1, Shoot2, RapidShoot1, RapidShoot2, Reload, Draw };

struct Mode {
    float interval,accuracyMaximum,accuracyMinimum,accuracyModifier;
    float jumpSpread,moveSpread,duckSpread,standSpread;
};
inline constexpr Mode Primary{.24f,.95f,.5f,.3f,1.5f,.025f,.01f,.015f};
inline constexpr Mode Secondary{.15f,.9f,.3f,.325f,.17f,.09f,.01f,.02f};
inline constexpr const Mode& Parameters(bool rapid){return rapid?Secondary:Primary;}

// Both original attack entry points reset accuracy before computing spread.
// The recovery below happens later inside Fire and does not retroactively
// change this shot's already passed spread argument.
inline float Spread(bool rapid,bool grounded,float speed,bool ducking,
    bool forcedAirborne=false,bool forcedStanding=false)
{
    const auto& mode=Parameters(rapid);
    const float factor=forcedAirborne?mode.jumpSpread:forcedStanding?mode.standSpread:
        !grounded?mode.jumpSpread:speed>0?mode.moveSpread:ducking?mode.duckSpread:mode.standSpread;
    return mode.accuracyMaximum*factor;
}

inline bool TriggerAllowed(bool rapid,int& shots)
{
    ++shots;
    if(!rapid&&shots>1){shots=1;return false;}
    return true;
}

inline float AccuracyAfterAttempt(bool rapid,float previousFire,float now)
{
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
    const auto& mode=Parameters(rapid);
    if(previousFire==0)return mode.accuracyMaximum;
    const float elapsed=now-previousFire;
    const float recovery=(.275f-elapsed)*mode.accuracyModifier;
    return std::clamp(mode.accuracyMaximum-recovery,mode.accuracyMinimum,mode.accuracyMaximum);
}

struct Kick {float vertical,lateral,verticalModifier,lateralModifier,verticalMaximum,lateralMaximum;};
inline constexpr Kick Recoil(bool grounded,bool moving,bool ducking)
{
    if(!grounded)return {2,1,.5f,.25f,8,5};
    if(moving)return {.8f,.3f,.1f,.15f,5.5f,3.2f};
    if(ducking)return {.2f,.1f,.07f,.01f,3,1.5f};
    return {.35f,.25f,.15f,.03f,3.5f,2};
}

// SKULL-1 overrides the ordinary KickBack algorithm. The six random values
// are original C rand() results, supplied by the server/prediction adapter.
// Primary fire instead subtracts a fixed two degrees from pitch. Call only
// after TriggerAllowed succeeds, when the shot ordinal is at least one.
inline std::array<float,2> RapidRecoil(Kick kick,int shots,std::array<float,2> punch,
    const std::array<int,6>& random)
{
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
    const float verticalBase=float(random[0]%10)*kick.vertical*.1f;
    const float lateralBase=float(random[1]%10)*kick.lateral*.1f;
    const int verticalRange=int(float(shots)*3.5f),lateralRange=int(float(shots)*2.f);
    const float vertical=float(random[2]%verticalRange)*kick.verticalModifier+verticalBase;
    const float lateral=float(random[3]%lateralRange)*kick.lateralModifier+lateralBase;
    punch[0]=(random[4]&1)?std::max(punch[0]-vertical,-kick.verticalMaximum):
        std::min(punch[0]+vertical,kick.verticalMaximum);
    punch[1]=(random[5]%2==1)?std::min(punch[1]+lateral,kick.lateralMaximum):
        std::max(punch[1]-lateral,-kick.lateralMaximum);
    return punch;
}
}
