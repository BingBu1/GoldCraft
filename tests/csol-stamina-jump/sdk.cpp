// SPDX-License-Identifier: GPL-3.0-or-later
#define CSOL_PHASE_SDK_LIBRARY
#include "../csol-challenge-phase/sdk.cpp"
#undef CSOL_PHASE_SDK_LIBRARY
#include "pm_defs.h"
#include "pm_movevars.h"
#include "in_buttons.h"
namespace jump_sdk
{
using namespace phase_sdk;
playermove_t* pmove{};
CCSPlayer* pmoveplayer{};
std::function<void()> textureCallback;
unsigned textureCalls{},gravityCalls{};
void Texture(){++textureCalls;if(textureCallback)textureCallback();}
void PreventBunny(){}
int StepType(char){return 0;}
void StepSound(int,float){}
real_t JumpHeight(bool){return 270;}
void FixGravity(){++gravityCalls;}
#define PM_CatagorizeTextureType Texture
#define PM_PreventMegaBunnyJumping PreventBunny
#define PM_MapTextureTypeStepType StepType
#define PM_PlayStepSound StepSound
#define PM_JumpHeight JumpHeight
#define PM_FixupGravityVelocity FixGravity
#include "stamina_pm_jump.inc"
#undef PM_CatagorizeTextureType
#undef PM_PreventMegaBunnyJumping
#undef PM_MapTextureTypeStepType
#undef PM_PlayStepSound
#undef PM_JumpHeight
#undef PM_FixupGravityVelocity
void Run(std::string_view group)
{
    Settings(group=="noon");Case c(MusicResources);
    // The focused world fixture initializes CSOL services rather than the
    // entire GameDLLInit cvar list. Bind this actual native cvar explicitly.
    Engine::RegisterCvar(&freezetime_jump);
    if(group=="noon"){NativeActor(c);Restart(c);}
    auto* rules=static_cast<CHalfLifeMultiplay*>(g_pGameRules);rules->m_bFreezePeriod=FALSE;
    pmoveplayer=c.player.CSPlayer();
    playermove_t move{};move.server=1;move.player_index=c.player.entindex()-1;move.onground=0;
    move.movetype=MOVETYPE_WALK;move.cmd.buttons=IN_JUMP;move.maxspeed=250;move.clientmaxspeed=250;
    move.PM_Info_ValueForKey=[](const char*,const char*)->const char*{return "0";};pmove=&move;
    // Native swimming returns through its original random/sound callbacks.
    // These engine boundaries are observations, not a stamina success path.
    move.RandomLong=[](int32 low,int32)->int32{return low;};
    move.PM_PlaySound=[](int,const char*,float,float,int,int){};
    csol::scenario::ConfigError error;
    Require(CSOL_TickActionStamina(c.f.runtime,c.player,100,error),"actual initialized stamina before command");
    if(group=="new-life")
    {
        const auto previous=CSOL_PrepareActionStaminaJump(move);
        Require(previous.owner&&previous.request,"old life captures native jump token");
        CSOL_ScenarioPlayerSpawnStarted(c.player);
        ++c.player.m_iNumSpawns; // Native Spawn counter input; callback above owns the real profile serial.
        Require(CSOL_TickActionStamina(c.f.runtime,c.player,100,error),"actual SpawnStarted profile admits a new stamina life");
        Expect(!CSOL_CommitActionStaminaJump(previous)&&CSOL_ActionStaminaView(c.f.runtime,c.player)->amount==100,
            "old native jump cannot debit new SpawnStarted life");
        const auto current=CSOL_PrepareActionStaminaJump(move);
        Require(current.owner!=previous.owner&&CSOL_CommitActionStaminaJump(current),"new life receives independent accepted-jump authority");
        Expect(CSOL_ActionStaminaView(c.f.runtime,c.player)->amount==85,"new life charges its own original15 only");
        return;
    }
    if(group=="low")
        for(std::uint64_t token=1;token<=9;++token)
            Require(CSOL_TryDebitKickStamina(c.f.runtime,c.player,token)==CSOLKickStaminaDebit::Applied,"actual prior kicks leave10 stamina");
    const float before=CSOL_ActionStaminaView(c.f.runtime,c.player)->amount;
    if(group=="client")move.server=0;
    if(group=="air")move.onground=-1;
    if(group=="held")move.oldbuttons=IN_JUMP;
    if(group=="water")move.waterlevel=2;
    if(group=="waterjump")move.waterjumptime=100;
    if(group=="dead")move.dead=1;
    if(group=="duck"){move.bInDuck=1;move.flags|=FL_DUCKING;}
    if(group=="prevent")move.iuser3|=PLAYER_PREVENT_JUMP;
    if(group=="freeze")
    {
        c.f.engine.Set("mp_freezetime_jump","0");rules->m_bFreezePeriod=TRUE;
        Require(freezetime_jump.value==0&&g_pGameRules->IsFreezePeriod(),"actual native freeze controls are active");
    }
    if(group=="retire")textureCallback=[&]{CSOL_RetireActionStamina(c.player);};
    if(group=="map-cancel")textureCallback=[&]{c.f.runtime->RequestClose();};
    ScenarioStaminaNativeJump();
    if(group=="retire"||group=="map-cancel")
    {Expect(!CSOL_ActionStaminaView(c.f.runtime,c.player),"old jump cannot debit a retired map/life");return;}
    const auto after=CSOL_ActionStaminaView(c.f.runtime,c.player);Require(bool(after),"actual stamina owner remains");
    const bool accepted=group=="normal"||group=="low"||group=="noon"||group=="duplicate";
    const bool charged=accepted&&group!="noon";
    Expect(after->amount==before-(charged?15:0),"only native accepted server jump debits original15");
    if(accepted)
    {
        Expect(move.onground==-1&&move.velocity[2]>0&&(move.oldbuttons&IN_JUMP),"actual native gates accepted and ran jump epilogue");
        ScenarioStaminaNativeJump();
        Expect(CSOL_ActionStaminaView(c.f.runtime,c.player)->amount==after->amount,"held/air repeat cannot debit another jump");
    }
    if(group=="low")
    {
        Expect(after->amount==-5,"original Jump permits low balance and defers clamp to PostThink");
        Require(CSOL_TickActionStamina(c.f.runtime,c.player,100,error),"actual same-frame PostThink clamp");
        const auto clamped=CSOL_ActionStaminaView(c.f.runtime,c.player);
        Expect(clamped&&clamped->amount==0&&!clamped->exhausted,"late clamp does not invent early exhausted recovery");
    }
    if(group=="duplicate")
    {
        move.onground=0;move.oldbuttons=0;
        const auto token=CSOL_PrepareActionStaminaJump(move);
        Require(CSOL_CommitActionStaminaJump(token),"valid internal accepted-call token debits once");
        const float amount=CSOL_ActionStaminaView(c.f.runtime,c.player)->amount;
        Expect(!CSOL_CommitActionStaminaJump(token)&&CSOL_ActionStaminaView(c.f.runtime,c.player)->amount==amount,"same token cannot debit twice");
        auto bad=token;bad.slot=33;Expect(!CSOL_CommitActionStaminaJump(bad),"out-of-range token cannot address another slot");
    }
}
}
int main(int argc,char** argv)
{
    try{sdk_test::Require(argc==2,"one native jump group");jump_sdk::Run(argv[1]);}
    catch(const std::exception& error){++sdk_test::failures;std::cerr<<"ERROR "<<error.what()<<'\n';}
    std::cout<<"behaviorChecks="<<kick_test::behaviorChecks<<" checks="<<sdk_test::checks<<" failures="<<sdk_test::failures<<'\n';
    return sdk_test::failures?1:0;
}
