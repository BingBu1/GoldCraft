// SPDX-License-Identifier: GPL-3.0-or-later
#define CSOL_PHASE_SDK_LIBRARY
#include "../csol-challenge-phase/sdk.cpp"
#undef CSOL_PHASE_SDK_LIBRARY
#include "pm_defs.h"
#include "pm_movevars.h"
#include "in_buttons.h"
#include "../../native/csol/scenario/custom/jump_prediction.hpp"
#include "../../native/csol/player_collision.hpp"
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
}
namespace csol::client
{
unsigned clientMoves{},clipChecks{};
bool clipAllowed{true};
bool InjectPlayerClips(playermove_t&){++clipChecks;return clipAllowed;}
struct Downstream
{
    void (*HUD_PlayerMove)(playermove_t*,int)=[](playermove_t* move,int server)
    {
        sdk_test::Require(server==0,"actual plugin forwards native client prediction");
        ++clientMoves;jump_sdk::pmove=move;jump_sdk::ScenarioStaminaNativeJump();
    };
} downstream;
#include "stamina_client_move.inc"
}
namespace jump_sdk
{
void Snapshot(Case& c,bool denied)
{
    g_engfuncs.pfnGetPhysicsInfoString=[](const edict_t*)->const char*{return "\\ordinary\\retained";};
    clientdata_t snapshot{};
    c.f.engine.api.pfnUpdateClientData(c.player.edict(),0,&snapshot);
    const std::string_view info(snapshot.physinfo);
    Expect(info.starts_with("\\ordinary\\retained"),"actual outgoing clientdata retains native physics info");
    Expect((info.find("\\cso_jdeny\\1")!=info.npos)==denied,"actual clientdata derives jump denial from same native life");
    const auto buffer=std::make_unique<playermove_t>();auto& prediction=*buffer;
    prediction.server=0;prediction.movetype=MOVETYPE_WALK;
    prediction.onground=0;prediction.cmd.buttons=IN_JUMP;prediction.maxspeed=250;
    std::strcpy(prediction.physinfo,snapshot.physinfo);
    prediction.PM_Info_ValueForKey=[](const char* text,const char* key)->const char*
    {
        if(std::strcmp(key,custom::JumpDeniedKey)==0)
            return std::strstr(text,"\\cso_jdeny\\1")?"1":"";
        return "";
    };
    csol::client::PlayerMove(&prediction,0);
    Expect(csol::client::clientMoves==1&&csol::client::clipChecks==1,
        "verbatim actual plugin PlayerMove delegates exactly once after player clips");
    Expect((prediction.onground==0&&prediction.velocity[2]==0)==denied,
        "actual outgoing denial and actual client PM adapter agree on physical departure");
    const auto saved=std::make_unique<playermove_t>(prediction);
    csol::client::clipAllowed=false;csol::client::PlayerMove(&prediction,0);
    Expect(csol::client::clientMoves==1&&std::memcmp(&prediction,saved.get(),sizeof(prediction))==0,
        "failed native clip preparation cannot partially modify prediction");
}
void Run(std::string_view group)
{
    Settings(group=="noon");Case c(MusicResources);
    // The focused world fixture initializes CSOL services rather than the
    // entire GameDLLInit cvar list. Bind this actual native cvar explicitly.
    Engine::RegisterCvar(&freezetime_jump);
    if(group=="noon"){NativeActor(c);Restart(c);}
    auto* rules=static_cast<CHalfLifeMultiplay*>(g_pGameRules);rules->m_bFreezePeriod=FALSE;
    pmoveplayer=c.player.CSPlayer();
    const auto buffer=std::make_unique<playermove_t>();auto& move=*buffer;
    move.server=1;move.player_index=c.player.entindex()-1;move.onground=0;
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
    if(group=="low"||group=="zero"||group=="snapshot-low"||group=="snapshot-zero")
        for(std::uint64_t token=1;token<=((group=="zero"||group=="snapshot-zero")?10:9);++token)
            Require(CSOL_TryDebitKickStamina(c.f.runtime,c.player,token)==CSOLKickStaminaDebit::Applied,"actual prior kicks leave10 stamina");
    if(group.starts_with("snapshot-"))
    {Snapshot(c,group!="snapshot-normal");return;}
    if(group=="exact")
    {
        for(int i=0;i<5;++i)
            Require(CSOL_CommitActionStaminaJump(CSOL_PrepareActionStaminaJump(move)),"actual accepted jumps debit75");
        Require(CSOL_TryDebitKickStamina(c.f.runtime,c.player,1)==CSOLKickStaminaDebit::Applied,"actual accepted kick leaves exact15");
    }
    const float before=CSOL_ActionStaminaView(c.f.runtime,c.player)->amount;
    if(group=="client")move.server=0;
    if(group=="fly")move.movetype=MOVETYPE_FLY;
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
    if(group=="callback-debit")textureCallback=[&]
    {
        for(std::uint64_t token=1;token<=9;++token)
            Require(CSOL_TryDebitKickStamina(c.f.runtime,c.player,token)==CSOLKickStaminaDebit::Applied,"engine observer debits actual current owner");
    };
    ScenarioStaminaNativeJump();
    if(group=="retire"||group=="map-cancel")
    {Expect(!CSOL_ActionStaminaView(c.f.runtime,c.player),"old jump cannot debit a retired map/life");return;}
    const auto after=CSOL_ActionStaminaView(c.f.runtime,c.player);Require(bool(after),"actual stamina owner remains");
    const bool accepted=group=="normal"||group=="noon"||group=="duplicate"||group=="exact"||group=="fly";
    const bool charged=accepted&&group!="noon"&&group!="fly";
    Expect(after->amount==before-(charged?15:0)-(group=="callback-debit"?90:0),"only native accepted server jump debits user15");
    if(accepted)
    {
        Expect(move.onground==-1&&move.velocity[2]>0&&(move.oldbuttons&IN_JUMP),"actual native gates accepted and ran jump epilogue");
        ScenarioStaminaNativeJump();
        Expect(CSOL_ActionStaminaView(c.f.runtime,c.player)->amount==after->amount,"held/air repeat cannot debit another jump");
    }
    if(group=="low"||group=="zero"||group=="callback-debit")
    {
        const float balance=group=="zero"?0:10;
        Expect(after->amount==balance&&move.onground==0&&move.velocity[2]==0&&textureCalls==(group=="callback-debit"?1:0),
            "human insufficient-fee rule refuses physical departure, sound and debit");
        Require(CSOL_TickActionStamina(c.f.runtime,c.player,100,error),"actual same-frame PostThink clamp");
        const auto clamped=CSOL_ActionStaminaView(c.f.runtime,c.player);
        Expect(clamped&&clamped->amount==balance&&clamped->exhausted==(balance==0),"rejected jump cannot invent depletion or recovery");
        const auto predicted=std::make_unique<playermove_t>(move);auto& prediction=*predicted;
        prediction.oldbuttons=0;prediction.server=0;
        Require(custom::ApplyJumpPredictionGate(prediction,true),"native prediction accepts authoritative denial bit");
        Expect((move.oldbuttons&IN_JUMP)==(prediction.oldbuttons&IN_JUMP),"denied server and client retain the same native held-key latch");
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
