// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "native/csol/server/weapons/guest_cof_revolver_owner.hpp"
#include "shared-owner-runtime-sdk.inc"
#include <limits>

namespace
{
struct FrameProbe final:CBasePlayerWeapon,guest::cof::server::OwnerFrameWeapon
{
    unsigned advances{},retirements{};
    void AdvanceOwnerFrame() noexcept override{++advances;}
    void Invalidate() noexcept override{++retirements;}
};
void ReadModel(const char* path)
{
    std::ifstream input(path,std::ios::binary);Check(input.good());
    const std::vector<char> bytes{std::istreambuf_iterator<char>(input),{}};
    model.resize(bytes.size());std::memcpy(model.data(),bytes.data(),bytes.size());
}
void ReloadWindows(RevolverOwnerView& revolver,RevolverOwnerBackendView& backend)
{
    const float previousTime=globals.time;
    const int previousSequence=player.pev->sequence,previousGait=player.pev->gaitsequence;
    const auto previousActivity=player.m_Activity,previousIdeal=player.m_IdealActivity;
    auto* previousItem=player.m_pActiveItem;
    const auto track=backend.animation->Select(backend.animationGeneration,Pistol::Revolver,PlayerAction::Reload,false);
    Check(track.has_value());
    RevolverWeapon held;edict_t heldEdict{};heldEdict.serialnumber=61;heldEdict.pvPrivateData=&held;
    heldEdict.v.pContainingEntity=&heldEdict;held.pev=&heldEdict.v;held.m_pPlayer=&player;held.m_iId=898;
    player.m_pActiveItem=&held;player.pev->sequence=track->sequence;player.pev->gaitsequence=777;
    player.m_Activity=player.m_IdealActivity=ACT_RELOAD;
    player.m_fSequenceFinished=TRUE;
    Check(!revolver.ReloadSequence(backend.binding,PLAYER_WALK));
    globals.time=20;RevolverEvent stage{RevolverEffect::ReloadStage};stage.value=0;stage.a=22;
    Check(revolver.ReloadStage(backend.binding,stage));
    RevolverOwnerState saved{};Check(revolver.PersistentState(backend.binding,saved)&&saved.reloadUntil==22);
    Check(revolver.ReloadSequence(backend.binding,PLAYER_WALK)==track->sequence);
    Check(revolver.ReloadSequence(backend.binding,PLAYER_IDLE)==track->sequence);
    for(int action:{PLAYER_ATTACK1,PLAYER_ATTACK2,PLAYER_JUMP,PLAYER_SUPERJUMP,PLAYER_DIE,PLAYER_FLINCH,PLAYER_LARGE_FLINCH})
        Check(!revolver.ReloadSequence(backend.binding,action));
    const auto flags=player.pev->flags;
    player.pev->flags|=FL_FROZEN;Check(!revolver.ReloadSequence(backend.binding,PLAYER_WALK));player.pev->flags=flags;
    for(auto activity:{ACT_SWIM,ACT_DIESIMPLE,ACT_HOVER,ACT_HOP,ACT_LEAP,ACT_FLINCH,ACT_LARGE_FLINCH})
    {player.m_Activity=activity;Check(!revolver.ReloadSequence(backend.binding,PLAYER_WALK));}
    player.m_Activity=ACT_RELOAD;player.m_flFlinchTime=21;
    Check(!revolver.ReloadSequence(backend.binding,PLAYER_WALK));player.m_flFlinchTime=0;
    player.m_pActiveItem=&weapon;Check(!revolver.ReloadSequence(backend.binding,PLAYER_WALK));player.m_pActiveItem=&held;
    ++player.pev->sequence;Check(!revolver.ReloadSequence(backend.binding,PLAYER_WALK));player.pev->sequence=track->sequence;
    const auto readsBefore=modelReads;
    for(int i=0;i<1000;++i)Check(revolver.ReloadSequence(backend.binding,PLAYER_WALK)==track->sequence);
    Check(modelReads==readsBefore&&player.pev->gaitsequence==777);
    globals.time=21.999F;Check(revolver.ReloadSequence(backend.binding,PLAYER_WALK)==track->sequence);
    globals.time=22;Check(!revolver.ReloadSequence(backend.binding,PLAYER_WALK));
    globals.time=24;stage.value=1;stage.a=25;
    Check(revolver.ReloadStage(backend.binding,stage)&&revolver.PersistentState(backend.binding,saved)&&saved.reloadUntil==25);
    stage.a=26;Check(!revolver.ReloadStage(backend.binding,stage));
    Check(revolver.PersistentState(backend.binding,saved)&&saved.reloadUntil==25);
    stage.value=3;Check(!revolver.ReloadStage(backend.binding,stage));
    stage.value=0;stage.a=std::numeric_limits<float>::quiet_NaN();Check(!revolver.ReloadStage(backend.binding,stage));
    globals.time=30;stage.value=2;stage.a=0;
    Check(revolver.ReloadStage(backend.binding,stage)&&revolver.PersistentState(backend.binding,saved));
    const float until=static_cast<float>(30.0+double(track->sdkDuration));
    Check(saved.reloadUntil==until&&revolver.ReloadSequence(backend.binding,PLAYER_IDLE)==track->sequence);
    globals.time=until;Check(!revolver.ReloadSequence(backend.binding,PLAYER_IDLE));
    Check(player.pev->gaitsequence==777&&modelReads==readsBefore);
    globals.time=previousTime;player.pev->sequence=previousSequence;player.pev->gaitsequence=previousGait;
    player.m_Activity=previousActivity;player.m_IdealActivity=previousIdeal;player.m_pActiveItem=previousItem;
}
}
int main(int argc,char** argv)
{
    if(argc!=4)return 1;
    ReadModel(argv[1]);RuntimeInitialize();
    RevolverOwnerView revolver;RevolverOwnerBackendView backend{};std::string error;
    const auto prepare=[&](NativeOwnerRules rules=NativeOwnerRules{false,61,17,15}) {
        return revolver.Prepare(player,runtimeView,rules,{true,216,12.5F},PistolAnimationRig::Stock,model,_stricmp,backend,error);
    };
    Check(prepare()&&revolver.Current(backend.binding));
    Check(backend.animation->Select(backend.animationGeneration,Pistol::Revolver,PlayerAction::WeaponMelee,true).has_value());
    Check(!backend.animation->Select(backend.animationGeneration,Pistol::Glock,PlayerAction::Fire,false).has_value());
    RevolverOwnerFrame read{};Check(revolver.Read(backend.binding,read));
    Check(!read.toggleAim&&read.bulletDamage==61&&read.meleeDamage==17&&read.reserveLimit==15);
    const auto canonical=RuntimeRead(runtimeView.binding);
    Check(read.pressed==canonical.pressed&&read.released==canonical.released&&read.randomSeed==canonical.randomSeed&&
        read.originalFlags==canonical.originalFlags&&read.condition3AA==canonical.condition3AA&&read.sourceMode==canonical.sourceMode);
    Check(!read.quietAudio&&!read.infiniteAmmo&&!read.pendingGate&&read.activityBlock==0&&read.pendingGateTime==0);

    Check(read.firstDrawn&&read.cylinderAngle==216&&read.cylinderGate==12.5F);
    ReloadWindows(revolver,backend);
    Check(runtime.FirstDraw(runtimeView.binding,false)&&revolver.Read(backend.binding,read)&&read.firstDrawn);
    RevolverEvent cylinder{RevolverEffect::Cylinder};cylinder.a=72;cylinder.b=99;cylinder.enabled=false;
    Check(revolver.Cylinder(backend.binding,cylinder)&&revolver.Read(backend.binding,read)&&
        read.cylinderAngle==72&&read.cylinderGate==12.5F);
    cylinder.a=60;cylinder.enabled=true;
    Check(revolver.Cylinder(backend.binding,cylinder)&&revolver.Read(backend.binding,read)&&
        read.cylinderAngle==60&&read.cylinderGate==99);
    RevolverOwnerState saved{};Check(revolver.PersistentState(backend.binding,saved)&&saved.firstDrawn&&saved.cylinderAngle==60);
    cylinder.kind=RevolverEffect::Aim;Check(!revolver.Cylinder(backend.binding,cylinder));
    cylinder.kind=RevolverEffect::Cylinder;
    for(float angle:{-1.0F,361.0F,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()})
    {cylinder.a=angle;Check(!revolver.Cylinder(backend.binding,cylinder));}
    cylinder.a=72;cylinder.b=std::numeric_limits<float>::infinity();Check(!revolver.Cylinder(backend.binding,cylinder));
    Check(revolver.FirstDraw(backend.binding,false)&&revolver.Read(backend.binding,read)&&!read.firstDrawn);
    Check(revolver.PersistentState(backend.binding,saved)&&!saved.firstDrawn&&saved.cylinderAngle==60&&saved.cylinderGate==99);

    const auto coldModelReads=modelReads;
    edict_t ground{};player.pev->groundentity=&ground;player.pev->button=IN_JUMP|IN_ATTACK2;
    Check(runtime.BeginPreThink(19,player)&&runtime.BeginInput(19,player)&&runtime.AcceptedJump(19,player));
    Check(revolver.Read(backend.binding,read)&&read.condition3AA&&read.pressed==(IN_JUMP|IN_ATTACK2));
    Check(runtime.AfterMovement(19,player)&&runtime.EndPostThink(19,player));
    for(int i=0;i<10000;++i)Check(revolver.Read(backend.binding,read));
    Check(modelReads==coldModelReads);
    const auto old=backend.binding;Check(prepare({true,83,23,50}));
    Check(!revolver.Current(old)&&revolver.Read(backend.binding,read)&&read.toggleAim&&read.bulletDamage==83&&read.reserveLimit==50);
    RevolverOwnerFrame untouched{};untouched.bulletDamage=987;
    Check(!revolver.Read(old,untouched)&&untouched.bulletDamage==987);
    Check(!RevolverOwnerView::BackendCurrent(nullptr)&&!RevolverOwnerView::BackendFrame(nullptr,untouched));
    Check(RevolverOwnerView::BackendCurrent(&backend.binding)&&RevolverOwnerView::BackendFrame(&backend.binding,read));
    for(auto rules:{NativeOwnerRules{true,-1,1,15},NativeOwnerRules{true,1,-1,15},NativeOwnerRules{true,1,1,45}})
        Check(!prepare(rules));
    Check(prepare());++player.m_iNumSpawns;
    Check(!revolver.Read(backend.binding,untouched)&&untouched.bulletDamage==987);--player.m_iNumSpawns;
    Check(prepare());runtimeAuth.revoke=true;
    Check(!revolver.Read(backend.binding,untouched)&&untouched.bulletDamage==987);runtimeAuth.revoke=false;
    RuntimeInitialize();Check(prepare());
    const auto oldCanonical=backend.binding;
    ++runtimeAuth.life;++runtimeAuth.modelGeneration;
    Check(runtime.Prepare(player,Request(),runtimeView,error));
    Check(!revolver.Current(oldCanonical));
    Check(prepare());revolver.Invalidate();Check(!revolver.Current(backend.binding));
    RevolverOwnerState untouchedState{true,123,456};
    Check(!revolver.PersistentState(backend.binding,untouchedState)&&untouchedState.cylinderAngle==123);
    Check(!revolver.FirstDraw(backend.binding,true)&&!revolver.Cylinder(backend.binding,cylinder));

    // One canonical inventory pass updates any guest capability exactly once,
    // while native item lists and owner retirement remain authoritative.
    RuntimeInitialize();FrameProbe probe;edict_t probeEdict{};
    probe.pev=&probeEdict.v;probeEdict.v.pContainingEntity=&probeEdict;probeEdict.pvPrivateData=&probe;
    probeEdict.serialnumber=39;probe.m_pPlayer=&player;probe.m_pNext=&weapon;
    player.m_rgpPlayerItems[PISTOL_SLOT]=&probe;
    Check(runtime.BeginPreThink(19,player)&&probe.advances==1);
    Check(!runtime.BeginPreThink(19,player)&&probe.advances==1);
    runtime.ModelChanging(19,player);Check(probe.retirements==1&&!runtime.Current(runtimeView.binding));
    player.m_rgpPlayerItems[PISTOL_SLOT]=nullptr;
    for(int i=2;i<=3;++i)
    {
        runtime.EndMap();ReadModel(argv[i]);Initialize();globals.maxEntities=1024;runtimeAuth={};
        Check(runtime.BeginMap(runtimeAuth.map));
        auto request=Request();request.rig=PistolAnimationRig::CSO;
        Check(runtime.Prepare(player,request,runtimeView,error));
        Check(revolver.Prepare(player,runtimeView,{true,61,17,15},{false,0,0},PistolAnimationRig::CSO,model,_stricmp,backend,error));
        Check(backend.animation->Select(backend.animationGeneration,Pistol::Revolver,PlayerAction::Fire,false).has_value());
        ReloadWindows(revolver,backend);
        runtime.EndMap();Check(!revolver.Current(backend.binding));
    }
    std::cout<<"Revolver canonical owner/frame adapter: "<<checks<<" SDK checks, zero failures\n";
}
