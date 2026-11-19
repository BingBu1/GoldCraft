// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "native/csol/server/weapons/guest_cof_shotgun_owner.hpp"
#include "shared-owner-runtime-sdk.inc"
#include <new>

namespace { unsigned allocations{}; }
void* operator new(std::size_t size) { ++allocations; if(auto* p=std::malloc(size?size:1))return p;throw std::bad_alloc(); }
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}

namespace guest::cof
{
BallisticsResult FireShotgunBallistics(CBaseEntity&, const PistolBallistics&, const BallisticsEffects&, const BallisticsCall&) noexcept
{ std::abort(); }
}
namespace
{
using namespace guest::cof::server;
struct Stage final:goldcraft::IPlayerThinkStage
{
    void* context{};goldcraft::PlayerThinkStageCallback callback{};std::uint64_t token{},serial{};
    std::uint64_t Register(void* c,goldcraft::PlayerThinkStageCallback f) noexcept override
    {if(token)return 0;context=c;callback=f;return token=++serial;}
    bool Current(std::uint64_t t) const noexcept override {return t&&t==token;}
    bool Release(std::uint64_t t) noexcept override {if(!Current(t))return false;token=0;callback=nullptr;return true;}
    void Fire(edict_t& actor,double now) {if(callback)callback(context,&actor,0,now);}
};
Stage stage;
PlayerActionRuntime actionRuntime;
PlayerActionBinding actions;
ShotgunOwnerView shotgun;
ShotgunOwnerBackendView view;
struct SharedLedger { CBasePlayer* player{}; float amount{73}; bool readable{true}, infinite{}; };
SharedLedger ledger;
bool ActionCurrent(void* context,CBasePlayer& actor,std::uint64_t map,std::uint64_t life) noexcept
{return runtimeAuth.active&&static_cast<SharedLedger*>(context)->player==&actor&&map==runtimeAuth.map&&life==runtimeAuth.life;}
bool Dispatch(void* context,CBasePlayer&,PlayerThinkKind) noexcept
{static_cast<SharedLedger*>(context)->amount=11;return true;}
bool SharedStamina(void* context,float& amount,bool& infinite) noexcept
{
    auto& value=*static_cast<SharedLedger*>(context);if(!value.readable)return false;
    amount=value.amount;infinite=value.infinite;return true;
}
bool SetShared(float amount) noexcept {ledger.amount=amount;return true;}
SharedPlayerActionBackend ActionBackend(SharedLedger& value=ledger)
{return {&value,ActionCurrent,Dispatch,SharedStamina,PlayerThinkBit(PlayerThinkKind::Pushback)};}
void PrepareActions()
{
    actionRuntime.Reset();Check(actionRuntime.Install(stage,runtimeAuth.map));
    ledger={&player,73,true,false};
    actions=actionRuntime.PrepareShared(19,player,runtimeAuth.life,ActionBackend());Check(bool(actions));
}
void ReadModel(const char* path)
{
    std::ifstream file(path,std::ios::binary);Check(file.good());
    std::vector<char> bytes{std::istreambuf_iterator<char>(file),{}};
    model.resize(bytes.size());std::memcpy(model.data(),bytes.data(),bytes.size());
}
bool Prepare(NativeOwnerRules rules={true,65,0,15},PistolAnimationRig rig=PistolAnimationRig::Stock)
{std::string error;return shotgun.Prepare(player,runtimeView,rules,actions,{},rig,model,_stricmp,view,error);}
ShotgunOwnerFrame Read()
{ShotgunOwnerFrame value;Check(shotgun.Read(view.binding,value));return value;}
void InitializeShotgun(const char* modelPath,PistolAnimationRig rig=PistolAnimationRig::Stock)
{
    actionRuntime.Reset();shotgun.Invalidate();ReadModel(modelPath);
    if(rig==PistolAnimationRig::Stock)RuntimeInitialize();
    else
    {
        runtime.EndMap();Initialize();globals.maxEntities=1024;runtimeAuth={};
        Check(runtime.BeginMap(runtimeAuth.map));auto request=Request();request.rig=rig;
        std::string error;Check(runtime.Prepare(player,request,runtimeView,error));
    }
    PrepareActions();Check(Prepare({true,65,0,15},rig));
}
void ReloadWindows()
{
    const float previousTime = globals.time;
    const int previousSequence = player.pev->sequence, previousGait = player.pev->gaitsequence;
    const auto previousActivity = player.m_Activity, previousIdeal = player.m_IdealActivity;
    auto* previousItem = player.m_pActiveItem;
    ShotgunWeapon held; edict_t heldEdict{}; heldEdict.serialnumber = 61; heldEdict.pvPrivateData = &held;
    heldEdict.v.pContainingEntity = &heldEdict; held.pev = &heldEdict.v; held.m_pPlayer = &player; held.m_iId = ShotgunHostId;
    player.m_pActiveItem = &held; player.pev->gaitsequence = 777;
    for (bool crouching : {false,true})
    {
        if (crouching) player.pev->flags |= FL_DUCKING; else player.pev->flags &= ~FL_DUCKING;
        const auto track = view.animation->Select(view.animationGeneration,LongGun::Shotgun,PlayerAction::Reload,crouching);
        Check(track.has_value()); player.pev->sequence = track->sequence;
        player.m_Activity = player.m_IdealActivity = ACT_RELOAD; player.m_fSequenceFinished = TRUE;
        for (int phase = 0; phase != 5; ++phase)
        {
            globals.time = 20; ShotgunEvent event{ShotgunEffect::ReloadStage}; event.value = phase;
            event.a = phase < 2 ? 21 : 0;
            Check(shotgun.ReloadStage(view.binding,event));
            ShotgunOwnerState saved{}; Check(shotgun.PersistentState(view.binding,saved));
            const float expected = phase < 2 ? 21 : float(double(20)+double(track->sdkDuration));
            Check(saved.reloadUntil == expected);
            Check(shotgun.ReloadSequence(view.binding,PLAYER_IDLE) == track->sequence);
            Check(shotgun.ReloadSequence(view.binding,PLAYER_WALK) == track->sequence);
            const auto oldAllocations = allocations, oldReads = modelReads;
            for (int n=0;n!=1000;++n) Check(shotgun.ReloadSequence(view.binding,PLAYER_WALK) == track->sequence);
            Check(allocations==oldAllocations && modelReads==oldReads && player.pev->gaitsequence==777);
            globals.time = expected; Check(!shotgun.ReloadSequence(view.binding,PLAYER_WALK));
        }
        globals.time = 30; ShotgunEvent event{ShotgunEffect::ReloadStage}; event.a = 31;
        Check(shotgun.ReloadStage(view.binding,event)); ShotgunOwnerState saved{};
        Check(shotgun.PersistentState(view.binding,saved) && saved.reloadUntil==31);
        for (int bad=0;bad!=5;++bad)
        {
            auto invalid=event;
            if(bad==0)invalid.kind=ShotgunEffect::InventoryChanged;
            else if(bad==1)invalid.value=-1;
            else if(bad==2)invalid.value=5;
            else if(bad==3)invalid.a=32;
            else invalid.a=NAN;
            Check(!shotgun.ReloadStage(view.binding,invalid));
            ShotgunOwnerState after{}; Check(shotgun.PersistentState(view.binding,after)&&after.reloadUntil==31);
        }
        for(int action:{PLAYER_ATTACK1,PLAYER_ATTACK2,PLAYER_JUMP,PLAYER_SUPERJUMP,PLAYER_DIE,PLAYER_FLINCH,PLAYER_LARGE_FLINCH})
            Check(!shotgun.ReloadSequence(view.binding,action));
        for(auto activity:{ACT_SWIM,ACT_DIESIMPLE,ACT_HOVER,ACT_HOP,ACT_LEAP,ACT_FLINCH,ACT_LARGE_FLINCH})
        {player.m_Activity=activity;Check(!shotgun.ReloadSequence(view.binding,PLAYER_WALK));}
        player.m_Activity=ACT_RELOAD; player.pev->flags|=FL_FROZEN;
        Check(!shotgun.ReloadSequence(view.binding,PLAYER_WALK)); player.pev->flags&=~FL_FROZEN;
        player.m_flFlinchTime=31; Check(!shotgun.ReloadSequence(view.binding,PLAYER_WALK)); player.m_flFlinchTime=0;
        ++player.pev->sequence; Check(!shotgun.ReloadSequence(view.binding,PLAYER_WALK)); player.pev->sequence=track->sequence;
        player.m_pActiveItem=previousItem; Check(!shotgun.ReloadSequence(view.binding,PLAYER_WALK)); player.m_pActiveItem=&held;
        const auto before = view;
        std::string error; ShotgunOwnerBackendView untouched = view;
        Check(!shotgun.Prepare(player,runtimeView,{true,65,0,15},actions,{NAN},PistolAnimationRig::Stock,model,_stricmp,untouched,error));
        Check(shotgun.Current(before.binding)&&view.binding.generation==before.binding.generation);
        Check(shotgun.Prepare(player,runtimeView,{true,65,0,15},actions,saved,PistolAnimationRig::Stock,model,_stricmp,view,error));
        Check(!shotgun.Current(before.binding)&&shotgun.ReloadSequence(view.binding,PLAYER_WALK)==track->sequence);
    }
    player.pev->flags &= ~FL_DUCKING; player.m_pActiveItem=previousItem;
    player.pev->sequence=previousSequence; player.pev->gaitsequence=previousGait;
    player.m_Activity=previousActivity; player.m_IdealActivity=previousIdeal; globals.time=previousTime;
}

}
int main(int argc,char** argv)
{
    if(argc!=4)return 1;
    InitializeShotgun(argv[1]);ReloadWindows();auto value=Read();const auto common=RuntimeRead(runtimeView.binding);
    Check(value.stamina==73&&value.bulletDamage==65&&value.reserveLimit==15&&value.toggleAim);
    Check(value.pressed==common.pressed&&value.released==common.released&&value.randomSeed==common.randomSeed&&
        value.condition3AA==common.condition3AA&&value.sourceMode==common.sourceMode&&value.originalFlags==common.originalFlags);
    Check(value.activityBlock==0&&!value.pendingGate&&!value.infiniteAmmo&&!value.quietAudio&&value.eventOwner2814==0);
    edict_t ground;player.pev->groundentity=&ground;player.pev->button=IN_JUMP|IN_ATTACK2;
    Check(runtime.BeginPreThink(19,player)&&runtime.BeginInput(19,player)&&runtime.AcceptedJump(19,player));
    Check(Read().condition3AA&&Read().pressed==(IN_JUMP|IN_ATTACK2));
    Check(runtime.AfterMovement(19,player)&&runtime.EndPostThink(19,player));
    Check(view.playerThink==actions.Think());
    Check(actions.PreparedFor(player,31,9)&&!actions.PreparedFor(player,32,9)&&!actions.PreparedFor(player,31,10));
    for(unsigned a=0;a<PlayerActionCount;++a)for(bool crouch:{false,true})
        Check(view.animation->Select(view.animationGeneration,LongGun::Shotgun,PlayerAction(a),crouch).has_value());
    Check(!view.animation->Select(view.animationGeneration,Pistol::Glock,PlayerAction::Fire,false));
    Check(SetShared(-7)&&!Read().stamina&&shotgun.Current(view.binding));Check(SetShared(21));
    player.pev->flags|=FL_IMMUNE_SLIME;Check(!(Read().originalFlags&0x40000U));
    player.pev->flags&=~FL_IMMUNE_SLIME;

    // Invalid cold requests retain a still-valid current view and do not touch
    // the player's stamina or independently scheduled task.
    const auto previous=view;Check(actions.Think()->Schedule(player,PlayerThinkKind::Pushback,.23F,31,9));
    const auto due=actions.Think()->Deadline();
    for(auto rules:{NativeOwnerRules{true,-1,0,15},NativeOwnerRules{true,1,0,0},NativeOwnerRules{true,1,0,90}})
    {Check(!Prepare(rules)&&shotgun.Current(previous.binding)&&Read().stamina==21&&actions.Think()->Deadline()==due);}
    std::string error;ShotgunOwnerBackendView untouched=previous;
    Check(!shotgun.Prepare(player,runtimeView,{true,1,0,15},{},{},PistolAnimationRig::Stock,model,_stricmp,untouched,error));
    Check(untouched.binding.generation==previous.binding.generation&&shotgun.Current(previous.binding));
    Check(!shotgun.Prepare(player,runtimeView,{true,1,0,15},actions,{},PistolAnimationRig::Stock,{},_stricmp,untouched,error));
    Check(shotgun.Current(previous.binding)&&actions.Think()->Deadline()==due);
    const auto oldModelFn=g_engfuncs.pfnGetModelPtr;
    g_engfuncs.pfnGetModelPtr=[](edict_t*)->void*{return nullptr;};
    Check(!Prepare()&&shotgun.Current(previous.binding));g_engfuncs.pfnGetModelPtr=oldModelFn;

    // Canonical model bindings may retire independently of the action life.
    ++runtimeAuth.modelGeneration;Check(runtime.Prepare(player,Request(),runtimeView,error));
    Check(!shotgun.Current(previous.binding)&&actions.Current());
    Check(Prepare({false,91,0,50})&&Read().reserveLimit==50&&!Read().toggleAim&&Read().stamina==21);
    Check(actions.Think()->Deadline()==due&&view.playerThink==previous.playerThink);
    Check(actions.BeginCommand());stage.Fire(ownerEdict,due);Check(Read().stamina==11);

    // A temporarily missing CSOL ledger pauses pushback only; canonical
    // gunfire/reload inputs remain readable and no second cycle is created.
    ledger.readable=false;value=Read();Check(!value.stamina&&value.bulletDamage==91&&shotgun.Current(view.binding));
    ledger.readable=true;Check(Read().stamina==11);
    StaminaCycleState untouchedState;untouchedState.amount=999;StaminaCycleOwner untouchedOwner;
    Check(!actions.Read(untouchedState,untouchedOwner)&&untouchedState.amount==999&&!actions.SetStamina(999));
    Check(actions.Spawn()==PlayerCycleResult::MissingBackend&&ledger.amount==11);
    const auto oldReads=modelReads,oldAllocations=allocations;
    for(unsigned n=0;n<10000;++n)Check(shotgun.Read(view.binding,value)&&value.stamina==11);
    Check(modelReads==oldReads&&allocations==oldAllocations);
    Check(!ShotgunOwnerView::BackendCurrent(nullptr)&&!ShotgunOwnerView::BackendFrame(nullptr,value));
    Check(ShotgunOwnerView::BackendCurrent(&view.binding)&&ShotgunOwnerView::BackendFrame(&view.binding,value));

    // Cross-player and cross-life combinations must fail even when both lives
    // are independently valid and use matching numeric map/life tokens.
    CBasePlayer second;edict_t secondEdict{};secondEdict.serialnumber=98;
    second.pev=&secondEdict.v;secondEdict.pvPrivateData=&second;secondEdict.v.pContainingEntity=&secondEdict;
    second.pev->health=100;second.pev->modelindex=1;second.m_iNumSpawns=0;second.has_disconnected=false;
    for(auto& item:second.m_rgpPlayerItems)item=nullptr;
    second.m_afButtonLast=second.m_afButtonPressed=second.m_afButtonReleased=0;second.random_seed=71;
    g_engfuncs.pfnIndexOfEdict=[](const edict_t* e){return e==&ownerEdict?19:20;};
    OwnerBackendView secondInput;Check(runtime.Prepare(second,Request(20),secondInput,error));
    SharedLedger secondLedger{&second};auto secondActions=actionRuntime.PrepareShared(20,second,9,ActionBackend(secondLedger));Check(bool(secondActions));
    Check(!secondActions.PreparedFor(player,31,9)&&!actions.PreparedFor(second,31,9));
    Check(!shotgun.Prepare(player,runtimeView,{true,1,0,15},secondActions,{},PistolAnimationRig::Stock,model,_stricmp,untouched,error));
    ShotgunOwnerView other;ShotgunOwnerBackendView otherView;
    Check(other.Prepare(second,secondInput,{false,17,0,50},secondActions,{},PistolAnimationRig::Stock,model,_stricmp,otherView,error));
    Check(other.Read(otherView.binding,value)&&value.stamina==73&&value.randomSeed==71&&Read().stamina==11);
    auto stale=view.binding;
    actions.Retire();
    Check(shotgun.Current(stale)&&other.Current(otherView.binding)&&!Read().stamina);
    ++runtimeAuth.life;Check(runtime.Prepare(player,Request(),runtimeView,error));
    Check(!Prepare());ledger.amount=73;actions=actionRuntime.PrepareShared(19,player,runtimeAuth.life,ActionBackend());
    Check(bool(actions)&&Prepare()&&!shotgun.Current(stale)&&Read().stamina==73);
    value.bulletDamage=987;Check(!shotgun.Read(stale,value)&&value.bulletDamage==987);
    actionRuntime.Reset();Check(!shotgun.Current(view.binding));

    // Real stock and CSO animation models, including all six actions and both
    // stances. Model retirement never manufactures a fresh stamina seed.
    for(int i=1;i<=3;++i)
    {
        const auto rig=i==1?PistolAnimationRig::Stock:PistolAnimationRig::CSO;InitializeShotgun(argv[i],rig);
        Check(Prepare({true,65,0,15},rig));
        for(unsigned a=0;a<PlayerActionCount;++a)for(bool crouch:{false,true})
            Check(view.animation->Select(view.animationGeneration,LongGun::Shotgun,PlayerAction(a),crouch).has_value());
        ReloadWindows();Check(SetShared(37));shotgun.Invalidate();Check(actions.Current()&&Prepare({true,65,0,15},rig)&&Read().stamina==37);
        runtime.EndMap();Check(!shotgun.Current(view.binding));actionRuntime.Reset();
    }
    std::cout<<"Shotgun canonical input and independent action life: "<<checks<<" SDK checks; no deployment/gameplay claim\n";
}
