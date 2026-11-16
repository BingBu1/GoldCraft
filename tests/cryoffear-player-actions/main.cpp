// SPDX-License-Identifier: GPL-3.0-or-later
#define main M16CompositionMain
#include "../cryoffear-m16-weapon/main.cpp"
#undef main
#include "native/guest/cof/server/player_action_runtime.hpp"
#include <new>

namespace { unsigned allocations{}; }
void* operator new(std::size_t size) { ++allocations; if (auto* p=std::malloc(size?size:1)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }

namespace
{
struct Stage final:goldcraft::IPlayerThinkStage
{
    void* context{};goldcraft::PlayerThinkStageCallback callback{};std::uint64_t token{},serial{};
    bool allow=true;
    mutable void (*duringCurrent)() noexcept {};
    std::uint64_t Register(void* c,goldcraft::PlayerThinkStageCallback f) noexcept override
    { if(!allow||token)return 0;context=c;callback=f;return token=++serial; }
    bool Current(std::uint64_t t) const noexcept override
    {if(auto call=duringCurrent){duringCurrent=nullptr;call();}return t&&t==token;}
    bool Release(std::uint64_t t) noexcept override
    {if(!Current(t))return false;token=0;callback=nullptr;return true;}
    void Fire(edict_t* e,float dt=0,double time=20) noexcept {if(callback)callback(context,e,dt,time);}
};
Stage stage;
PlayerActionRuntime runtime;
PlayerActionBinding action;
bool actionAuthority=true,staminaAuthority=true,failHud{},rebindHud{},retireHud{},mutateHud{},reenterHud{};
bool rescheduleAction{},rejectAction{};
bool captureHud{};
StaminaCycleState capturedHud,beforeHud;
unsigned actions{},huds{},voices{};
std::uint64_t expectedLife=1;
StaminaCycleState seed;
StaminaCycleOwner originalOwner;
PlayerActionBackend actionBackend;
bool ActionCurrent(void*,CBasePlayer&,std::uint64_t map,std::uint64_t life) noexcept
{return actionAuthority&&map==9&&life==expectedLife;}
bool ActionDispatch(void*,CBasePlayer&,PlayerThinkKind kind) noexcept
{
    ++actions;Check(kind==PlayerThinkKind::Pushback||kind==PlayerThinkKind::WeaponMelee);
    Check(action.PostThink()==PlayerCycleResult::NotReady);
    StaminaCycleState s;StaminaCycleOwner o;Check(action.Read(s,o));
    if(rescheduleAction&&actions==1)
        Check(action.Think()->Schedule(player,PlayerThinkKind::WeaponMelee,.25F,9,expectedLife));
    return action.SetStamina(s.amount-15)&&!rejectAction;
}
bool StaminaCurrent(void*) noexcept {return staminaAuthority;}
float Clock(void*) noexcept {return globals.time;}
bool Multiplayer(void*,bool& out) noexcept {out=true;return true;}
bool Random(void*,int lo,int hi,int& out) noexcept {Check(lo==0&&hi==1);out=1;return true;}
bool Voice(void*,const StaminaVoiceEvent&) noexcept {++voices;return true;}
bool Hud(void*,const StaminaHudEvent& e) noexcept
{
    ++huds;
    if(reenterHud)
    {Check(action.Spawn()==PlayerCycleResult::NotReady);Check(!action.BeginCommand());}
    if(e.kind==StaminaHudKind::Begin)
    {
        if(mutateHud)Check(action.SetStamina(37));
        if(captureHud)
        {
            StaminaCycleOwner owner;Check(action.Read(beforeHud,owner));
            player.pev->health=0;
            Check(action.CaptureAndRetire(capturedHud));
        }
        if(retireHud)action.Retire();
        if(rebindHud)
        {
            rebindHud=false;++expectedLife;
            auto changed=seed;changed.amount=73;
            action=runtime.Prepare(1,player,expectedLife,changed,originalOwner,actionBackend);
            Check(bool(action));
        }
    }
    if(e.kind==StaminaHudKind::Byte&&mutateHud)Check(e.integer==37);
    return !failHud;
}
void InitializeActions()
{
    runtime.Reset();Initialize();
    g_engfuncs.pfnIndexOfEdict=[](const edict_t*){return 1;};
    stage.allow=true;actionAuthority=staminaAuthority=true;failHud=rebindHud=retireHud=mutateHud=reenterHud=false;
    stage.duringCurrent=nullptr;captureHud=false;capturedHud=beforeHud={};
    rescheduleAction=rejectAction=false;
    actions=huds=voices=0;expectedLife=1;
    seed={};seed.amount=seed.lastSent=100;
    originalOwner={};originalOwner.rulesPresent=true;
    actionBackend={nullptr,ActionCurrent,ActionDispatch,{nullptr,StaminaCurrent,Clock,Multiplayer,Random,Voice,Hud},3};
    Check(runtime.Install(stage,9));
    action=runtime.Prepare(1,player,1,seed,originalOwner,actionBackend);Check(bool(action));
}
StaminaCycleState ReadState()
{StaminaCycleState s;StaminaCycleOwner o;Check(action.Read(s,o));return s;}
void Command(float time)
{globals.time=time;Check(action.BeginCommand());stage.Fire(&ownerEdict,0,time);}
}
int main(int argc,char** argv)
{
    M16CompositionMain(argc,argv);
    InitializeActions();
    Check(action.PostThink()==PlayerCycleResult::NotReady);

    InitializeActions();rescheduleAction=true;Check(action.BeginCommand());
    Check(action.Think()->Schedule(player,PlayerThinkKind::Pushback,.1F,9,1));
    stage.Fire(&ownerEdict);Check(actions==1&&action.Think()->Deadline()==20.25F);
    stage.Fire(&ownerEdict,0,30);Check(actions==1); // Same command cannot run twice.
    Command(21);Check(actions==2&&ReadState().amount==70);
    InitializeActions();rejectAction=true;Command(20);
    Check(action.Think()->Schedule(player,PlayerThinkKind::Pushback,.1F,9,1));
    Command(21);Check(action.LastThink()==PlayerThinkResult::Rejected&&ReadState().amount==85);
    rejectAction=false;Command(22);Check(action.PostThink()==PlayerCycleResult::Applied&&ReadState().amount==86);

    InitializeActions();Check(!runtime.Prepare(0,player,2,seed,originalOwner,actionBackend));
    Check(!runtime.Prepare(33,player,2,seed,originalOwner,actionBackend));
    auto incomplete=actionBackend;incomplete.stamina.hud=nullptr;
    Check(!runtime.Prepare(1,player,2,seed,originalOwner,incomplete)&&action.Current());
    incomplete=actionBackend;incomplete.dispatch=nullptr;
    Check(!runtime.Prepare(1,player,2,seed,originalOwner,incomplete));
    auto badSeed=seed;badSeed.hudPulse=NAN;
    Check(!runtime.Prepare(1,player,2,badSeed,originalOwner,actionBackend));
    StaminaCycleOwner badOwner=originalOwner;badOwner.horizontalSpeed=INFINITY;
    Check(!action.SetOwner(badOwner)&&ReadState().amount==100);
    stage.Fire(&ownerEdict);Check(action.PostThink()==PlayerCycleResult::NotReady);
    Check(action.Spawn()==PlayerCycleResult::Applied&&ReadState().amount==100&&huds==4);
    auto* task=action.Think();Check(task);
    Check(task->Schedule(player,PlayerThinkKind::Pushback,.23F,9,1));
    const float due=task->Deadline();
    player.pev->modelindex=17;weapon.OwnerModelChanging();weapon.Invalidate();
    Check(runtime.Prepare(1,player,1,{},originalOwner,actionBackend).Think()==task);
    Check(task->Deadline()==due&&ReadState().amount==100);
    Check(action.BeginCommand());stage.Fire(&ownerEdict,0,due);
    Check(actions==1&&ReadState().amount==85&&action.LastThink()==PlayerThinkResult::Dispatched);
    Check(action.PostThink()==PlayerCycleResult::Applied&&ReadState().amount==86);
    Check(action.PostThink()==PlayerCycleResult::NotReady);

    // Actual M16 SecondaryAttack installs into the life slot; removing its weapon
    // and model binding leaves the original player-owned task pending.
    InitializeActions();backends.playerThink=action.Think();backends.ownerLife=1;
    std::string error;const auto plan=RecommendedLongGunAnimationPlan(LongGun::M16,PistolAnimationRig::Stock);
    Check(animations.Prepare(player,model,{9,1},LongGun::M16,*plan,_stricmp,error));
    backends.playerModelGeneration={9,1};
    Check(weapon.Bind(player,prepared,backends)==M16Status::Ready);Check(weapon.Deploy());Ready();
    weapon.SecondaryAttack();Check(action.Think()->Deadline()==globals.time+.23F);
    const auto pending=action.Think()->Deadline();weapon.Invalidate();player.pev->modelindex=2;
    Check(action.BeginCommand());stage.Fire(&ownerEdict,0,pending);
    Check(actions==1&&ReadState().amount==85);

    // Invalid commands do not grant the post-movement phase; each valid command
    // can consume it once. All native player flags remain untouched.
    InitializeActions();Check(action.SetStamina(4));
    Check(action.BeginCommand());stage.Fire(&ownerEdict,-1,20);
    Check(action.PostThink()==PlayerCycleResult::NotReady);
    stage.Fire(&ownerEdict,0,NAN);Check(action.PostThink()==PlayerCycleResult::NotReady);
    Command(20);Check(ReadState().amount==4);
    Check(action.PostThink()==PlayerCycleResult::Applied&&ReadState().amount==5);
    Check(player.pev->flags==0);
    Check(!action.SetStamina(NAN));Check(action.SetStamina(-14));
    Command(21);Check(action.PostThink()==PlayerCycleResult::Applied&&ReadState().amount==0);
    Command(22);Check(action.PostThink()==PlayerCycleResult::Applied&&ReadState().amount==.1F&&voices==1);

    // Callback writes are visible at original HUD boundaries. A synchronous
    // new life cannot receive writes from the old call's stack-local state.
    InitializeActions();mutateHud=reenterHud=true;
    Check(action.Spawn()==PlayerCycleResult::Applied&&ReadState().amount==37);
    mutateHud=reenterHud=false;auto stale=action;rebindHud=true;
    Check(stale.Spawn()==PlayerCycleResult::Cancelled&&ReadState().amount==73);
    Check(!stale.Current()&&!stale.SetStamina(6)&&!stale.Think());
    Check(stale.PostThink()==PlayerCycleResult::Cancelled&&ReadState().amount==73);
    Check(!runtime.Prepare(1,player,1,seed,originalOwner,actionBackend));
    failHud=true;Check(action.Spawn()==PlayerCycleResult::MissingBackend&&ReadState().amount==100);
    failHud=false;staminaAuthority=false;Check(action.Spawn()==PlayerCycleResult::Cancelled&&ReadState().amount==100);
    staminaAuthority=true;Check(action.Spawn()==PlayerCycleResult::Applied);
    failHud=false;retireHud=true;Check(action.Spawn()==PlayerCycleResult::Cancelled&&!action);

    // Killed can start with zero health. Capture before asking the living
    // capability to validate, and retain all eight original scalar fields.
    InitializeActions();action.Retire();++expectedLife;
    seed={12,13,14,15,16,17,18,19};
    action=runtime.Prepare(1,player,expectedLife,seed,originalOwner,actionBackend);
    Check(bool(action));auto dead=action;
    Check(action.Think()->Schedule(player,PlayerThinkKind::Pushback,.23F,9,expectedLife));
    player.pev->health=0;player.pev->deadflag=DEAD_DYING;
    StaminaCycleState captured;captured.amount=999;
    Check(dead.CaptureAndRetire(captured));
    Check(captured.amount==12&&captured.lastSent==13&&captured.nextCycle==14&&captured.auxiliaryTime==15&&
        captured.exhaustionActionDeadline==16&&captured.voiceDeadline==17&&
        captured.exhaustionAudioDeadline==18&&captured.hudPulse==19);
    Check(!dead.Current()&&!dead.SetStamina(1)&&!dead.Think());
    StaminaCycleState untouched;untouched.amount=999;
    Check(!dead.CaptureAndRetire(untouched)&&untouched.amount==999);
    player.pev->health=100;player.pev->deadflag=DEAD_NO;++player.m_iNumSpawns;++expectedLife;
    action=runtime.Prepare(1,player,expectedLife,captured,originalOwner,actionBackend);
    Check(bool(action)&&ReadState().voiceDeadline==17);
    globals.time=55;Check(action.Spawn()==PlayerCycleResult::Applied);
    captured=ReadState();
    Check(captured.amount==100&&captured.lastSent==13&&captured.nextCycle==55&&captured.auxiliaryTime==55&&
        captured.exhaustionActionDeadline==16&&captured.voiceDeadline==17&&
        captured.exhaustionAudioDeadline==18&&captured.hudPulse==19);
    Command(60);Check(actions==0); // The previous life never transfers its task.

    // Retirement in a synchronous HUD callback must capture the live staged
    // copy, including writes made just before death, then stop the old cycle.
    InitializeActions();captureHud=mutateHud=true;
    Check(action.Spawn()==PlayerCycleResult::Cancelled&&!action);
    Check(capturedHud.amount==37&&capturedHud.lastSent==beforeHud.lastSent&&
        capturedHud.nextCycle==beforeHud.nextCycle&&capturedHud.auxiliaryTime==beforeHud.auxiliaryTime&&
        capturedHud.exhaustionActionDeadline==beforeHud.exhaustionActionDeadline&&
        capturedHud.voiceDeadline==beforeHud.voiceDeadline&&
        capturedHud.exhaustionAudioDeadline==beforeHud.exhaustionAudioDeadline&&capturedHud.hudPulse==beforeHud.hudPulse);

    // A stage callback can replace a life during identity checking. The old
    // capture must neither publish its state nor retire the newer capability.
    InitializeActions();auto replacing=action;
    stage.duringCurrent=[]() noexcept {
        ++expectedLife;auto changed=seed;changed.amount=73;
        action=runtime.Prepare(1,player,expectedLife,changed,originalOwner,actionBackend);
        Check(bool(action));
    };
    Check(!replacing.CaptureAndRetire(untouched)&&untouched.amount==999);
    Check(action.Current()&&ReadState().amount==73);

    for(unsigned failure=0;failure<6;++failure)
    {
        InitializeActions();auto staleCapture=action;
        switch(failure)
        {
        case 0:player.has_disconnected=true;break;
        case 1:++ownerEdict.serialnumber;break;
        case 2:++player.m_iNumSpawns;break;
        case 3:ownerEdict.free=true;break;
        case 4:ownerEdict.pvPrivateData=nullptr;break;
        case 5:runtime.Reset();break;
        }
        Check(!staleCapture.CaptureAndRetire(untouched)&&untouched.amount==999);
        Check(!staleCapture.Current());
    }

    // Once observed, retirement is permanent for a life token, even if a caller
    // restores the old health/serial/authority fields afterwards.
    for(unsigned failure=0;failure<7;++failure)
    {
        InitializeActions();auto old=action;
        Check(action.Think()->Schedule(player,PlayerThinkKind::Pushback,.1F,9,1));
        switch(failure)
        {
        case 0:player.pev->health=0;break;
        case 1:player.pev->deadflag=DEAD_DEAD;break;
        case 2:player.has_disconnected=true;break;
        case 3:++ownerEdict.serialnumber;break;
        case 4:++player.m_iNumSpawns;break;
        case 5:ownerEdict.free=true;break;
        case 6:actionAuthority=false;break;
        }
        Check(!old.Current());player.pev->health=100;player.pev->deadflag=DEAD_NO;
        player.has_disconnected=false;ownerEdict.serialnumber=2;player.m_iNumSpawns=0;
        ownerEdict.free=false;actionAuthority=true;
        Check(!old.Current()&&!runtime.Prepare(1,player,1,seed,originalOwner,actionBackend));
        ++expectedLife;action=runtime.Prepare(1,player,2,seed,originalOwner,actionBackend);
        Check(bool(action)&&!old.SetStamina(1)&&ReadState().amount==100);
        Command(30);Check(actions==0);
    }
    InitializeActions();auto old=action;runtime.Reset();Check(!old.Current());
    Check(!runtime.Current()&&stage.token==0);
    Check(runtime.Install(stage,9));action=runtime.Prepare(1,player,1,seed,originalOwner,actionBackend);
    Check(bool(action)&&!old.Current());
    stage.allow=false;Check(!runtime.Install(stage,10)&&!runtime.Current()&&!action.Current());

    // All 32 slots use actual SDK players/edicts. No weapon/model identity leaks
    // into the player action state, and hot dispatch does not allocate.
    InitializeActions();
    std::array<edict_t,32> entities{};std::array<Player,32> people;
    std::array<PlayerActionBinding,32> bindings;
    g_engfuncs.pfnIndexOfEdict=[](const edict_t* e){return e->v.iuser1;};
    expectedLife=2;
    for(int i=0;i<32;++i)
    {
        auto& e=entities[i];auto& p=people[i];e.serialnumber=i+1;e.v.iuser1=i+1;
        e.pvPrivateData=&p;e.v.pContainingEntity=&e;p.pev=&e.v;p.pev->health=100;
        p.has_disconnected=false;p.m_iNumSpawns=0;
        bindings[i]=runtime.Prepare(i+1,p,2,seed,originalOwner,actionBackend);
        Check(bool(bindings[i]));
        Check(bindings[i].SetStamina(float(i+1)));
    }
    for(int i=0;i<32;++i)
    {StaminaCycleState s;StaminaCycleOwner o;Check(bindings[i].Read(s,o)&&s.amount==float(i+1));}
    globals.time=0;const auto before=allocations;
    for(unsigned n=0;n<10000;++n)
    {
        const unsigned slot=n%32;auto b=bindings[slot];
        Check(b.BeginCommand());stage.Fire(&entities[slot],0,0);
        Check(b.PostThink()==PlayerCycleResult::Applied);
    }
    Check(before==allocations);runtime.Reset();
    std::cout<<checks<<" SDK checks; 32 life slots; 10000 hot commands without allocations; no deployment/gameplay claim\n";
}
