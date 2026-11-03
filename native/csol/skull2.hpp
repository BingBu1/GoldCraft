// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

namespace csol::skull2
{
inline constexpr int Id=312,Clip=7,DualClip=14,Reserve=35,AmmoBuyAmount=7,AmmoCost=70;
inline constexpr float SwitchTime=3,DeployTime=.75f,DeployIdle=1.5f,IdleTime=30;
inline constexpr float MaxSpeed=250,Distance=4096,VisualDistance=8192,RangeModifier=.8f;
inline constexpr int Penetration=3,SingleBulletType=27,DualBulletType=1,VisualBulletType=27;
inline constexpr const char* Classname="weapon_skull2";
inline constexpr const char* AmmoName="50AZ";
inline constexpr const char* ViewModel="models/v_skull2.mdl";
inline constexpr const char* PlayerModel="models/p_skull2.mdl";
inline constexpr const char* DualPlayerModel="models/p_skull2dual.mdl";
inline constexpr const char* WorldModel="models/w_skull2.mdl";
inline constexpr const char* FireSound="weapons/skull2-1.wav";
// GoldSrc script paths for the original skull2AMode/skull2B_left/skull2B_right events.
inline constexpr const char* SingleEvent="events/skull2_a.sc";
inline constexpr const char* LeftEvent="events/skull2_left.sc";
inline constexpr const char* RightEvent="events/skull2_right.sc";
inline constexpr std::array<std::string_view,6> DamageFields{
    "Damage","DamageZombie","DamageScenario","Damage_B","DamageZombie_B","DamageScenario_B"};

enum Flag : int { Left=2,Single=4,Dual=8,ToDual=16,ToSingle=32 };
enum Animation : int
{
    SingleIdle=0,SingleReload=1,SingleDraw=2,SingleShoot=3,
    DualIdle=5,DualIdleLast=6,LeftShoot=7,LeftShootLast=9,
    RightShoot=10,RightShootLast=12,DualReload=13,DualDraw=14,
    SingleToDual=15,DualToSingle=16
};

struct Mode
{
    float interval,reloadTime,accuracyMaximum,accuracyMinimum;
    float accuracyModifier,accuracyRecovery;
    float jumpSpread,moveSpread,duckSpread,standSpread;
    float fireIdle;
};
inline constexpr Mode SingleMode{.24f,2.5f,.95f,.5f,.3f,.275f,1.5f,.02f,.01f,.015f,1};
inline constexpr Mode DualMode{.16f,3.3f,.9f,.5f,.3f,.275f,1.5f,.03f,.01f,.02f,2};
inline constexpr bool IsDual(int flags){return (flags&Dual)!=0;}
inline constexpr bool IsSwitching(int flags){return (flags&(ToDual|ToSingle))!=0;}
inline constexpr const Mode& Parameters(int flags){return IsDual(flags)?DualMode:SingleMode;}

// MP10F82B80 tests only the committed Dual bit. In a B->A transition the
// existing clip may still exceed seven; querying capacity must not trim it.
inline constexpr int MaxClip(int flags){return IsDual(flags)?DualClip:Clip;}

// Return the actual draw sequence, or -1 when no transition starts. The
// absolute deadline belongs to the server; attack/idle countdowns belong to
// its adapter and must not be confused with this clock (MP10F837E0).
inline int BeginSwitch(int& flags,float now,float& deadline)
{
    int animation=-1;
    if(flags&Dual)
    {
        flags=(flags&~(Single|Dual|ToDual))|ToSingle;
        animation=DualToSingle;
    }
    else if(flags&Single)
    {
        flags=(flags&~(Single|Dual|ToSingle))|ToDual;
        animation=SingleToDual;
    }
    if(animation!=-1)deadline=now+SwitchTime;
    return animation;
}

// Holster restores the source mode without loading or returning ammunition.
// The old absolute deadline is harmless once neither pending bit remains.
inline void CancelSwitch(int& flags)
{
    if(flags&(Single|ToDual))flags=(flags&~(Dual|ToDual|ToSingle))|Single;
    else if(flags&(Dual|ToSingle))flags=(flags&~(Single|ToDual|ToSingle))|Dual;
}

inline void NormalizeForDeploy(int& flags,int clip)
{
    // MP10F82A20 restores the source mode and recomputes the alternating
    // side only for dual pistols. Single mode leaves that side bit intact.
    CancelSwitch(flags);
    if(IsDual(flags))
    {
        if((clip&1)==0)flags|=Left;
        else flags&=~Left;
    }
}

inline void LoadSecondMagazine(int& clip,int& reserve)
{
    if(clip>=DualClip)return;
    const int loaded=std::min({DualClip-clip,reserve,Clip});
    clip+=loaded;
    reserve-=loaded;
}

inline void PrepareDrop(int& flags,int& clip,int& reserve)
{
    // MP DropPlayerItem calls SKULL2 v1BC before packing the weapon box.
    // Unlike a completed transition, this callback does not update side,
    // player model or accuracy. It is not the initial pickup callback.
    if(!(flags&(Single|ToDual)))return;
    flags=(flags&~(Single|ToDual|ToSingle))|Dual;
    LoadSecondMagazine(clip,reserve);
}

// The original override commits only after the deadline, not at equality.
// Call after the base ItemPostFrame, preserving its reload/input ordering.
inline bool CompleteSwitch(int& flags,int& clip,int& reserve,float now,float deadline)
{
    if(!(now>deadline))return false;
    if(flags&ToDual)
    {
        flags=(flags&~(Single|ToDual|ToSingle))|Dual;
        LoadSecondMagazine(clip,reserve);
        if((clip&1)==0)flags|=Left;
        else flags&=~Left;
        return true;
    }
    if(flags&ToSingle)
    {
        flags=(flags&~(Dual|ToDual|ToSingle))|Single;
        if(clip>Clip)
        {
            reserve+=clip-Clip;
            clip=Clip;
        }
        return true;
    }
    return false;
}

inline constexpr int DrawSequence(int flags){return IsDual(flags)?DualDraw:SingleDraw;}
// Reload tests Single, whereas Primary and capacity test Dual. Pending modes
// therefore select the B reload with a seven-round capacity (MP10F836F1).
inline constexpr const Mode& ReloadParameters(int flags){return flags&Single?SingleMode:DualMode;}
inline constexpr int ReloadSequence(int flags){return flags&Single?SingleReload:DualReload;}
inline constexpr int IdleSequence(int flags,int clip)
{return flags&Single?SingleIdle:IsDual(flags)?(clip==1?DualIdleLast:DualIdle):-1;}
inline constexpr int ShootSequence(bool dual,bool left,int remainingClip)
{
    if(!dual)return SingleShoot;
    return left?(remainingClip>1?LeftShoot:LeftShootLast):
        (remainingClip>1?RightShoot:RightShootLast);
}

// Both modes are semiautomatic. Only A clamps an over-limit attempt back to
// one; B keeps the incremented count until the base input path releases it.
inline bool TriggerAllowed(int flags,int& shots)
{
    ++shots;
    if(shots<=1)return true;
    if(!IsDual(flags))shots=1;
    return false;
}

inline float Spread(int flags,bool grounded,float speed,bool ducking,
    bool forcedAirborne=false,bool forcedStanding=false)
{
    const auto& mode=Parameters(flags);
    const float factor=forcedAirborne?mode.jumpSpread:forcedStanding?mode.standSpread:
        !grounded?mode.jumpSpread:speed>0?mode.moveSpread:ducking?mode.duckSpread:mode.standSpread;
    return (IsDual(flags)?1.f-mode.accuracyMaximum:mode.accuracyMaximum)*factor;
}

inline float AccuracyAfterAttempt(int flags,float previousFire,float now)
{
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
    const auto& mode=Parameters(flags);
    if(previousFire==0)return mode.accuracyMaximum;
    const float elapsed=now-previousFire;
    const float recovery=(mode.accuracyRecovery-elapsed)*mode.accuracyModifier;
    return std::clamp(mode.accuracyMaximum-recovery,mode.accuracyMinimum,mode.accuracyMaximum);
}

// The installed delta transmits fuser3 at scale128. Preserve the half-auto
// gate without overflowing its signed22-bit payload after a long held input.
inline constexpr float PackShots(int shots){return float(std::clamp(shots,0,1000000))/128.f;}
inline int UnpackShots(float value)
{return std::isfinite(value)?int(std::clamp(value*128.f,0.f,1000000.f)):0;}
inline float PackFireAge(float lastFire,float now){return std::clamp(lastFire-now,-1.f,0.f);}
inline constexpr int HasPreviousFire=64;
}
