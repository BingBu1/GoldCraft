// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
namespace motion_test
{
void SpeedCases()
{
    {
        CSOLMonsterSpeedState state;entvars_t vars{};state.baseSpeed=200;vars.maxspeed=93;vars.gravity=.7f;
        Check(state.UpdateMaxSpeed(vars,10)&&vars.maxspeed==93,"empty ring and no boost preserves maxspeed");
        state.boostUntil=11;state.boostAdd=.5;
        Check(state.UpdateMaxSpeed(vars,10)&&vars.maxspeed==300,"active double boost scales base speed");
        Check(state.UpdateMaxSpeed(vars,11)&&vars.maxspeed==300&&state.boostUntil==0&&state.boostAdd==1,"expired boost with no ring preserves last speed");
        state.limits={{1,20,150,.5f},{2,9,50,.2f},{3,20,140,.4f}};
        Check(state.UpdateMaxSpeed(vars,10)&&state.limits.size()==3&&vars.maxspeed==50&&vars.gravity==.2f,"expired middle survives while both ends remain valid");
        state.limits.front().until=9;state.limits.back().until=9;
        Check(state.UpdateMaxSpeed(vars,10)&&state.limits.empty()&&vars.maxspeed==200&&vars.gravity==.2f,"end expiration drains ring, restores base cap but retains gravity");
        state.limits={{1,10,120,.6f},{2,10,120,.8f}};vars.framerate=4;
        Check(state.UpdateMaxSpeed(vars,10)&&state.limits.size()==2&&vars.gravity==.8f&&vars.framerate==4,"ring deadline equality remains, equal-cap later gravity wins");
        state.limits.back().gravity=-9999;vars.gravity=.3f;
        Check(state.UpdateMaxSpeed(vars,10)&&vars.gravity==.3f,"last selected gravity sentinel preserves entity gravity");
    }
    {
        CSOLMonsterSpeedState state;
        Check(!state.AddLimit(4,0,20,1,0,10)&&state.limits.empty(),"ring rejects zero duration");
        Check(state.AddLimit(4,5,20,1,-1,10)&&state.limits.front().group==5,"known ring group5");
        Check(!state.AddLimit(4,4,99,.5f,0,10)&&state.limits.front().speed==20,"nonpositive source tags match and shorter update is rejected");
        Check(state.AddLimit(4,6,30,.6f,0,10)&&state.limits.size()==1&&state.limits.front().sourceTag==0,"later matching deadline replaces in place");
        Check(state.AddLimit(4,2,40,.7f,3,10)&&state.limits.size()==2,"positive distinct source creates another ring record");
        Check(state.AddLimit(72,2,50,1,0,10)&&state.limits.back().group==3,"group3 differs from effect priority");
        Check(state.AddLimit(999,2,60,1,0,10)&&state.limits.back().group==-1,"unknown ring group remains minus1");
    }
    {
        CSOLMonsterSpeedState state;Vector output(9,9,9);
        Check(state.AddEffect(1,80,-5,5,0,Vector(3,4,99),10)&&state.effects.front().initialSpeed==5,"effect insert records XY initial speed only");
        Check(state.AddEffect(1,70,-4,1,2,Vector(30,40,0),10)&&state.effects.size()==1&&state.effects.front().until==11&&state.effects.front().initialSpeed==5,"same effect ID shortens deadline while preserving initial speed");
        Check(state.AddEffect(2,1,-100,5,2,Vector(0,0,0),10),"equal-priority later effect inserted");
        Check(state.ApplyVelocityEffects(Vector(100,0,7),200,10,output)&&Equal(output,Vector(70,0,3)),"vector pass skips later equal priority despite scalar pass tighter cap");
        Check(state.ApplyVelocityEffects(Vector(100,0,7),200,11,output)&&Equal(output,Vector(1,0,-93))&&state.effects.size()==1,"effect expires at exact equality");
        state.effects.push_back({2,6,0,20,1,0});state.RemoveEffect(2);
        Check(state.effects.empty(),"remove ID clears every duplicate");
        Check(state.AddEffect(3,0,-9,-1,0,Vector(0,0,0),10),"effect list accepts negative duration");
        const Vector raw(-0.0f,2,7);
        Check(state.ApplyVelocityEffects(raw,200,10,output)&&std::bit_cast<unsigned>(output.x)==0x80000000&&Equal(output,raw),"scalar drains expired list before vector empty fast path");
        state.AddEffect(4,60,-2,10,0,Vector(0,0,0),10);state.AddEffect(5,90,-1,10,1,Vector(0,0,0),10);
        Check(state.ApplyVelocityEffects(Vector(100,0,7),200,10,output)&&Equal(output,Vector(60,0,5)),"higher priority does not restore earlier smaller cap or vertical add");
        Check(state.ApplyVelocityEffects(Vector(0,0,7),200,10,output)&&Equal(output,Vector(0,0,5)),"zero horizontal velocity retains vertical effect");
        output=Vector(9,9,9);
        Check(!state.ApplyVelocityEffects(Vector(NAN,0,0),200,10,output)&&Equal(output,Vector(9,9,9)),"nonfinite input fails without output commit");
    }
    {
        Fixture f;const auto& initial=f.monster->SpeedState();
        Check(initial.limits.empty()&&initial.effects.empty()&&initial.recovery==1&&initial.boostUntil==0&&initial.boostAdd==1,"actual entity Spawn initializes speed containers and defaults");
        CCSOLMonsterModels models;csol::scenario::ConfigError error;
        auto& type=f.types.front();type.appearance="zombi_host";type.speed=87;type.health=100;
        csol::scenario::MonsterTypeTable table;table.rows={type};table.byId[type.Key()]=0;
        g_engfuncs.pfnPrecacheSound=[](const char*){return 1;};
        g_engfuncs.pfnSetSize=[](edict_t* ed,const float* mins,const float* maxs){ed->v.mins=Vector(mins);ed->v.maxs=Vector(maxs);};
        Require(models.Precache(table,error),"real model cache for base-speed test");
        csol::scenario::MonsterAttributes attributes;csol::scenario::RoundMonsterDeltas delta;delta.speed=7;
        Check(models.Apply(*f.monster,delta,attributes,error)&&f.monster->SpeedState().baseSpeed==attributes.speed&&f.monster->pev->maxspeed==attributes.speed,"actual attribute application updates stored base speed");
        f.monster->SpeedState().boostUntil=30;f.monster->SpeedState().AddEffect(7,80,0,30,0,Vector(3,4,0),10);
        f.Reborrow(*f.monster);
        Check(f.monster->SpeedState().boostUntil==30&&f.monster->SpeedState().effects.size()==1,"pool acquisition does not silently clear independent entity effects or boost");
    }
}
}
