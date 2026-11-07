// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <string_view>

namespace csol::skull5 {
// Original MP39f962f3 Skull5_prop111B4AE0 and HW WeaponMgr112.
inline constexpr int Id=112,Clip=24,Reserve=240,AmmoBuyAmount=30,AmmoCost=200;
inline constexpr int Penetration=8,OriginalBulletType=19;
inline constexpr float Distance=8192,RangeModifier=.98f;
inline constexpr float Interval=.28f,ZoomInterval=.33f,ReloadTime=1.9f;
inline constexpr float MaxSpeed=205,ZoomSpeed=165,NormalFov=90,ZoomFov1=80,ZoomFov2=35;
inline constexpr float DeployTime=.75f,DeployIdle=1.5f,FireIdle=1,IdleTime=1.5f,ZoomTime=.3f;
inline constexpr float InitialAccuracy=1,EmptyInterval=.2f;
inline constexpr const char* Classname="weapon_skull5";
inline constexpr const char* AmmoName="556az";
inline constexpr const char* ViewModel="models/v_skull5.mdl";
inline constexpr const char* PlayerModel="models/p_skull5.mdl";
inline constexpr const char* WorldModel="models/w_skull5.mdl";
inline constexpr const char* Event="events/skull5.sc";
inline constexpr const char* FireSound="weapons/skull5-1.wav";
inline constexpr const char* ShellModel="models/rshell.mdl";
inline constexpr std::array<std::string_view,3> DamageFields{"Damage","DamageZombie","DamageScenario"};
enum Animation {Idle,Reload,Draw,Shoot1,Shoot2};

// The installed GoldSrc delta sends fuser2 as a signed countdown. Recovery
// saturates after .8 seconds; a separate state bit preserves the original
// lastFire==0 sentinel even when a shot occurs at server time zero.
inline float PackFireAge(float lastFire,float now){return std::clamp(lastFire-now,-1.f,0.f);}

inline constexpr float NextFov(float fov){return fov==NormalFov?ZoomFov1:fov==ZoomFov1?ZoomFov2:NormalFov;}
inline constexpr float SpreadBase(bool ground,float speed,bool duck,bool forcedAirborne=false,bool forcedStanding=false)
{
    return forcedAirborne?1.f:forcedStanding?.04f:!ground?1.f:speed>0?.06f:duck?.02f:.04f;
}
inline float AccuracyAfterAttempt(float accuracy,float previousFire,float now)
{
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
    if(previousFire==0)return accuracy;
    const float elapsed=now-previousFire;
    const float scaled=elapsed*.5f;
    return std::min(.6f+scaled,1.f);
}
inline float Spread(float base,float accuracy,bool zoomed)
{
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
    if(!zoomed)base+=.07f;
    return (1.f-accuracy)*base;
}
struct Kick {float verticalMin,verticalMax,horizontalMin,horizontalMax;};
inline constexpr Kick Recoil(bool zoomed){return zoomed?Kick{2.5f,4,-1.4f,1.4f}:Kick{3,4,-2,2};}
inline std::array<float,2> ApplyRecoil(std::array<float,2> previous,float vertical,float horizontal)
{
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
    // Original caller multiplies in float, then performs x87 add/subtract
    // before the single store back to the player's pitch.
    const float recovery=previous[0]*.25f;
    return {float(double(previous[0])-(double(vertical)+double(recovery))),previous[1]+horizontal};
}
}
