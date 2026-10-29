// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
namespace actions_test
{
namespace
{
using R=CSOLMonsterMeleeResult;
bool ThreeClocks(const Fixture& f,float interval,float now)
{return Clock(f.clocks.attack,interval,now+interval,now)&&Clock(f.clocks.avoidance,interval,now+interval,now)&&Clock(f.clocks.idleObstacleCheck,interval,now+interval,now);}
}
void MeleeCases()
{
    {
        Fixture f;auto& victim=f.Target();EHANDLE target=&victim;
        Check(f.Attack(target)==R::Hit,"eligible attack reaches virtual damage synchronously");
        Require(victim.damage.size()==1,"one actual virtual damage call");const auto& d=victim.damage[0];
        Check(d.inflictor==f.monster->pev&&d.attacker==f.monster->pev&&d.amount==12&&d.flags==DMG_BULLET,"damage identity/type/base amount");
        Check(ThreeClocks(f,.8f,10)&&f.monster->pev->sequence==2&&f.monster->Animation().LockedUntil()==11,"walk animation before all three clocks");
        Check(f.queries==2,"real guarded SDK metadata uses two model queries");
        f.engine.globals.time=f.clocks.attack.deadline;
        Check(f.Attack(target)==R::Miss&&victim.damage.size()==1,"strict deadline equality blocks attack");
        f.engine.globals.time=std::nextafter(f.clocks.attack.deadline,100.0f);
        Check(f.Attack(target)==R::Hit&&victim.damage.size()==2&&f.queries==2,"next float after deadline attacks without redundant same-sequence reset");
    }
    for(int reason=0;reason<9;++reason)
    {
        Fixture f;auto& victim=f.Target();EHANDLE target=&victim;
        if(reason==0)target=nullptr;
        if(reason==1)victim.pev->health=0;
        if(reason==2)victim.pev->origin.x=std::nextafter(60.f,100.f);
        if(reason==3)victim.pev->origin.z=-41;
        if(reason==4)victim.pev->origin.z=113;
        if(reason==5){victim.player=true;victim.pev->rendermode=kRenderTransAlpha;victim.pev->renderamt=std::nextafter(1.f,0.f);}
        if(reason==6)f.monster->pev->angles.y=180;
        if(reason==7)victim.pev->origin=Vector(0,0,0);
        if(reason==8)victim.pev->origin.z=std::nextafter(113.f,0.f); // +36 rounds back to149.
        Check(f.Attack(target)==R::Miss&&victim.damage.empty()&&f.queries==0,"target/range/Z/transparent/facing exclusion");
        Check(Clock(f.clocks.attack,.3f,10+.3f,10)&&Clock(f.clocks.avoidance,0,-1,-1)&&Clock(f.clocks.idleObstacleCheck,0,-1,-1),"rejected eligible tick only schedules attack retry");
    }
    for(int boundary=0;boundary<4;++boundary)
    {
        Fixture f;auto& victim=f.Target();EHANDLE target=&victim;
        if(boundary==0)victim.pev->origin.x=60;
        if(boundary==1)victim.pev->origin.z=std::nextafter(-41.f,0.f);
        if(boundary==2)victim.pev->origin.z=std::nextafter(std::nextafter(113.f,0.f),0.f);
        if(boundary==3){victim.player=true;victim.pev->rendermode=kRenderTransAlpha;victim.pev->renderamt=1;}
        Check(f.Attack(target)==R::Hit&&victim.damage.size()==1,"inclusive planar range/strict Z adjacent float/transparency1 accepted");
    }
    for(int disabled=0;disabled<2;++disabled)
    {
        Fixture f;auto& victim=f.Target();EHANDLE target=&victim;
        if(disabled==0)f.state.skill=6;else f.types.front().attackDamage=0;
        Check(f.Attack(target)==R::Miss&&Clock(f.clocks.attack,99999,100009,10)&&victim.damage.empty(),"skill6/zero base damage installs long disable timer");
    }
    {
        Fixture f;auto& victim=f.Target();EHANDLE target=&victim;f.boostUntil=11;f.state.attributes.attackInterval=.6f;
        Check(f.Attack(target,21,0)==R::Hit&&victim.damage[0].amount==22&&ThreeClocks(f,.4f,10),"boost adds10 and interval floor0.4");
        Check((f.monster->pev->sequence==3||f.monster->pev->sequence==4)&&f.monster->Animation().LockedUntil()==10.5f,"running animation uses run variants and half-second lock");
    }
    {
        Fixture f;auto& victim=f.Target();EHANDLE target=&victim;f.suppressed=true;
        Check(f.Attack(target)==R::Hit&&victim.damage.empty()&&ThreeClocks(f,.8f,10)&&f.queries==2,"damage-suppressed still animates and updates clocks");
    }
    for(int mutation=0;mutation<3;++mutation)
    {
        Fixture f;auto& victim=f.Target();EHANDLE target=&victim;f.HookAnimation();
        f.onAnimation=[&](Hook* chain,CBaseAnimating* m)
        {f.engine.globals.time=20;f.state.attributes.attackInterval=1.2f;f.state.attributes.attackDamage=30;f.boostUntil=mutation==0?20:21;f.suppressed=mutation==2;chain->callNext(m);};
        Check(f.Attack(target)==R::Hit,"animation hook mutation attack completes");
        const float interval=mutation==0?1.2f:1.2f-.5f;
        Check(ThreeClocks(f,interval,20),"post-hook time/interval/boost read after animation");
        Check(victim.damage.size()==(mutation==2?0u:1u),"post-hook rule suppression controls actual virtual call");
        if(mutation!=2)Check(victim.damage[0].amount==(mutation==0?30.f:40.f),"post-hook damage and strict boost boundary");
        Check(f.monster->Animation().LockedUntil()==11,"animation lock retains pre-hook animation time");
    }
}
void MeleeCancellationCases()
{
    for(int stage=0;stage<7;++stage)
    {
        Fixture f;auto& victim=f.Target();EHANDLE target=&victim;f.HookAnimation();
        if(stage==0)victim.onIsPlayer=[&]{Require(f.requests.Reset(),"IsPlayer cancels owner");};
        else if(stage==1)f.onModel=[&]{if(f.queries==1)f.DestroyMapStorage();};
        else if(stage==2)f.onModel=[&]{if(f.queries==2)f.DestroyMapStorage();};
        else f.onAnimation=[&](Hook* chain,CBaseAnimating* m)
        {
            if(stage==3)f.monster->pev->deadflag=DEAD_DEAD;
            if(stage==4)Require(f.pool.ParkForDeath(*f.monster),"hook parks same lease");
            if(stage==5)f.ReborrowSameSlot();
            if(stage==6)Require(f.requests.Reset(),"hook dispatcher generation changes");
            chain->callOriginal(m);
        };
        Check(f.Attack(target)==R::Cancelled,"virtual/model/animation lifecycle invalidation cancels attack");
        Check(Clock(f.clocks.attack,.3f,10+.3f,10)&&Clock(f.clocks.avoidance,0,-1,-1),"cancel never commits success clocks after hooks");
        if(stage!=1&&stage!=2)Check(victim.damage.empty(),"cancel never delivers damage");
    }
    {
        Fixture f;auto& victim=f.Target();EHANDLE target=&victim;f.HookAnimation();auto* ed=victim.edict();
        f.onAnimation=[&](Hook* chain,CBaseAnimating* m){f.engine.Dispose(ed);chain->callNext(m);};
        Check(f.Attack(target)==R::Miss&&ThreeClocks(f,.8f,10),"target self-removal after animation yields miss after successful timing");
    }
    {
        Fixture f;auto& victim=f.Target();EHANDLE target=&victim;victim.onDamage=[&]{Require(f.requests.Reset(),"damage callback changes owner");};
        Check(f.Attack(target)==R::Cancelled&&victim.damage.size()==1&&ThreeClocks(f,.8f,10),"post-damage reset cancels result after actual one delivery");
    }
    {
        Fixture f;auto& victim=f.Target();EHANDLE target=&victim;++f.state.lease;
        Check(f.Attack(target)==R::Failed&&Clock(f.clocks.attack,0,-1,-1)&&victim.damage.empty(),"state lease mismatch rejected before timers");
    }
}
}
