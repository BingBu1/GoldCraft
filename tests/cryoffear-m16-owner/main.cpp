// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "native/csol/server/weapons/guest_cof_m16_owner.hpp"
#include "shared-owner-runtime-sdk.inc"
#include <new>

namespace { unsigned allocations{}; }
void* operator new(std::size_t size) { ++allocations; if(auto* p=std::malloc(size?size:1))return p;throw std::bad_alloc(); }
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}

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
M16OwnerView m16;
M16OwnerBackendView view;
StaminaCycleState seed;
StaminaCycleOwner originalOwner;
bool ActionCurrent(void*,CBasePlayer&,std::uint64_t map,std::uint64_t life) noexcept
{return runtimeAuth.active&&map==runtimeAuth.map&&life==runtimeAuth.life;}
bool Dispatch(void*,CBasePlayer&,PlayerThinkKind) noexcept {return actions.SetStamina(11);}
bool StaminaCurrent(void*) noexcept {return runtimeAuth.active;}
float Clock(void*) noexcept {return globals.time;}
bool Multiplayer(void*,bool& value) noexcept {value=true;return true;}
bool Random(void*,int,int,int& value) noexcept {value=0;return true;}
bool Voice(void*,const StaminaVoiceEvent&) noexcept {return true;}
bool Hud(void*,const StaminaHudEvent&) noexcept {return true;}
PlayerActionBackend ActionBackend()
{return {nullptr,ActionCurrent,Dispatch,{nullptr,StaminaCurrent,Clock,Multiplayer,Random,Voice,Hud},1};}
void PrepareActions()
{
    actionRuntime.Reset();Check(actionRuntime.Install(stage,runtimeAuth.map));
    seed={};seed.amount=73;seed.lastSent=73;originalOwner={};originalOwner.rulesPresent=true;
    actions=actionRuntime.Prepare(19,player,runtimeAuth.life,seed,originalOwner,ActionBackend());Check(bool(actions));
}
void ReadModel(const char* path)
{
    std::ifstream file(path,std::ios::binary);Check(file.good());
    std::vector<char> bytes{std::istreambuf_iterator<char>(file),{}};
    model.resize(bytes.size());std::memcpy(model.data(),bytes.data(),bytes.size());
}
bool Prepare(NativeOwnerRules rules={true,65,0,60},PistolAnimationRig rig=PistolAnimationRig::Stock)
{std::string error;return m16.Prepare(player,runtimeView,rules,actions,rig,model,_stricmp,view,error);}
M16OwnerFrame Read()
{M16OwnerFrame value;Check(m16.Read(view.binding,value));return value;}
void InitializeM16(const char* modelPath,PistolAnimationRig rig=PistolAnimationRig::Stock)
{
    actionRuntime.Reset();m16.Invalidate();ReadModel(modelPath);
    if(rig==PistolAnimationRig::Stock)RuntimeInitialize();
    else
    {
        runtime.EndMap();Initialize();globals.maxEntities=1024;runtimeAuth={};
        Check(runtime.BeginMap(runtimeAuth.map));auto request=Request();request.rig=rig;
        std::string error;Check(runtime.Prepare(player,request,runtimeView,error));
    }
    PrepareActions();Check(Prepare({true,65,0,60},rig));
}
}
int main(int argc,char** argv)
{
    if(argc!=4)return 1;
    InitializeM16(argv[1]);auto value=Read();const auto common=RuntimeRead(runtimeView.binding);
    Check(value.stamina==73&&value.bulletDamage==65&&value.reserveLimit==60&&value.toggleAim);
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
        Check(view.animation->Select(view.animationGeneration,LongGun::M16,PlayerAction(a),crouch).has_value());
    Check(!view.animation->Select(view.animationGeneration,Pistol::Glock,PlayerAction::Fire,false));
    Check(actions.SetStamina(-7)&&Read().stamina==-7);Check(actions.SetStamina(21));
    player.pev->flags|=FL_IMMUNE_SLIME;Check(!(Read().originalFlags&0x40000U));
    player.pev->flags&=~FL_IMMUNE_SLIME;

    // Invalid cold requests retain a still-valid current view and do not touch
    // the player's stamina or independently scheduled task.
    const auto previous=view;Check(actions.Think()->Schedule(player,PlayerThinkKind::Pushback,.23F,31,9));
    const auto due=actions.Think()->Deadline();
    for(auto rules:{NativeOwnerRules{true,-1,0,60},NativeOwnerRules{true,1,0,0},NativeOwnerRules{true,1,0,90}})
    {Check(!Prepare(rules)&&m16.Current(previous.binding)&&Read().stamina==21&&actions.Think()->Deadline()==due);}
    std::string error;M16OwnerBackendView untouched=previous;
    Check(!m16.Prepare(player,runtimeView,{true,1,0,60},{},PistolAnimationRig::Stock,model,_stricmp,untouched,error));
    Check(untouched.binding.generation==previous.binding.generation&&m16.Current(previous.binding));
    Check(!m16.Prepare(player,runtimeView,{true,1,0,60},actions,PistolAnimationRig::Stock,{},_stricmp,untouched,error));
    Check(m16.Current(previous.binding)&&actions.Think()->Deadline()==due);
    const auto oldModelFn=g_engfuncs.pfnGetModelPtr;
    g_engfuncs.pfnGetModelPtr=[](edict_t*)->void*{return nullptr;};
    Check(!Prepare()&&m16.Current(previous.binding));g_engfuncs.pfnGetModelPtr=oldModelFn;

    // Canonical model bindings may retire independently of the action life.
    ++runtimeAuth.modelGeneration;Check(runtime.Prepare(player,Request(),runtimeView,error));
    Check(!m16.Current(previous.binding)&&actions.Current());
    Check(Prepare({false,91,0,200})&&Read().reserveLimit==200&&!Read().toggleAim&&Read().stamina==21);
    Check(actions.Think()->Deadline()==due&&view.playerThink==previous.playerThink);
    Check(actions.BeginCommand());stage.Fire(ownerEdict,due);Check(Read().stamina==11);

    const auto oldReads=modelReads,oldAllocations=allocations;
    for(unsigned n=0;n<10000;++n)Check(m16.Read(view.binding,value)&&value.stamina==11);
    Check(modelReads==oldReads&&allocations==oldAllocations);
    Check(!M16OwnerView::BackendCurrent(nullptr)&&!M16OwnerView::BackendFrame(nullptr,value));
    Check(M16OwnerView::BackendCurrent(&view.binding)&&M16OwnerView::BackendFrame(&view.binding,value));

    // Cross-player and cross-life combinations must fail even when both lives
    // are independently valid and use matching numeric map/life tokens.
    CBasePlayer second;edict_t secondEdict{};secondEdict.serialnumber=98;
    second.pev=&secondEdict.v;secondEdict.pvPrivateData=&second;secondEdict.v.pContainingEntity=&secondEdict;
    second.pev->health=100;second.pev->modelindex=1;second.m_iNumSpawns=0;second.has_disconnected=false;
    for(auto& item:second.m_rgpPlayerItems)item=nullptr;
    second.m_afButtonLast=second.m_afButtonPressed=second.m_afButtonReleased=0;second.random_seed=71;
    g_engfuncs.pfnIndexOfEdict=[](const edict_t* e){return e==&ownerEdict?19:20;};
    OwnerBackendView secondInput;Check(runtime.Prepare(second,Request(20),secondInput,error));
    auto secondActions=actionRuntime.Prepare(20,second,9,seed,originalOwner,ActionBackend());Check(bool(secondActions));
    Check(!secondActions.PreparedFor(player,31,9)&&!actions.PreparedFor(second,31,9));
    Check(!m16.Prepare(player,runtimeView,{true,1,0,60},secondActions,PistolAnimationRig::Stock,model,_stricmp,untouched,error));
    M16OwnerView other;M16OwnerBackendView otherView;
    Check(other.Prepare(second,secondInput,{false,17,0,200},secondActions,PistolAnimationRig::Stock,model,_stricmp,otherView,error));
    Check(other.Read(otherView.binding,value)&&value.stamina==73&&value.randomSeed==71&&Read().stamina==11);
    auto stale=view.binding;actions.Retire();Check(!m16.Current(stale)&&other.Current(otherView.binding));
    ++runtimeAuth.life;Check(runtime.Prepare(player,Request(),runtimeView,error));
    Check(!Prepare());actions=actionRuntime.Prepare(19,player,runtimeAuth.life,seed,originalOwner,ActionBackend());
    Check(bool(actions)&&Prepare()&&!m16.Current(stale)&&Read().stamina==73);
    value.bulletDamage=987;Check(!m16.Read(stale,value)&&value.bulletDamage==987);
    actionRuntime.Reset();Check(!m16.Current(view.binding));

    // Real stock and CSO animation models, including all six actions and both
    // stances. Model retirement never manufactures a fresh stamina seed.
    for(int i=1;i<=3;++i)
    {
        const auto rig=i==1?PistolAnimationRig::Stock:PistolAnimationRig::CSO;InitializeM16(argv[i],rig);
        Check(Prepare({true,65,0,60},rig));
        for(unsigned a=0;a<PlayerActionCount;++a)for(bool crouch:{false,true})
            Check(view.animation->Select(view.animationGeneration,LongGun::M16,PlayerAction(a),crouch).has_value());
        Check(actions.SetStamina(37));m16.Invalidate();Check(actions.Current()&&Prepare({true,65,0,60},rig)&&Read().stamina==37);
        runtime.EndMap();Check(!m16.Current(view.binding));actionRuntime.Reset();
    }
    std::cout<<"M16 canonical input and independent action life: "<<checks<<" SDK checks; no deployment/gameplay claim\n";
}
