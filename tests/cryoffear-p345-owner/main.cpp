// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "native/csol/server/weapons/guest_cof_p345_owner.hpp"
#include "shared-owner-runtime-sdk.inc"

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
}
int main(int argc,char** argv)
{
    if(argc!=4)return 1;
    ReadModel(argv[1]);RuntimeInitialize();
    P345OwnerView p345;P345OwnerBackendView backend{};std::string error;
    const auto prepare=[&](NativeOwnerRules rules=NativeOwnerRules{false,61,17,24}) {
        return p345.Prepare(player,runtimeView,rules,PistolAnimationRig::Stock,model,_stricmp,backend,error);
    };
    Check(prepare()&&p345.Current(backend.binding));
    Check(backend.animation->Select(backend.animationGeneration,Pistol::P345,PlayerAction::WeaponMelee,true).has_value());
    Check(!backend.animation->Select(backend.animationGeneration,Pistol::Glock,PlayerAction::Fire,false).has_value());
    P345OwnerFrame read{};Check(p345.Read(backend.binding,read));
    Check(!read.toggleAim&&read.bulletDamage==61&&read.meleeDamage==17&&read.reserveLimit==24);
    const auto canonical=RuntimeRead(runtimeView.binding);
    Check(read.pressed==canonical.pressed&&read.released==canonical.released&&read.randomSeed==canonical.randomSeed&&
        read.originalFlags==canonical.originalFlags&&read.condition3AA==canonical.condition3AA&&read.sourceMode==canonical.sourceMode);
    Check(!read.quietAudio&&!read.infiniteAmmo&&!read.pendingGate&&read.activityBlock==0&&read.pendingGateTime==0);

    const auto coldModelReads=modelReads;
    edict_t ground{};player.pev->groundentity=&ground;player.pev->button=IN_JUMP|IN_ATTACK2;
    Check(runtime.BeginPreThink(19,player)&&runtime.BeginInput(19,player)&&runtime.AcceptedJump(19,player));
    Check(p345.Read(backend.binding,read)&&read.condition3AA&&read.pressed==(IN_JUMP|IN_ATTACK2));
    Check(runtime.AfterMovement(19,player)&&runtime.EndPostThink(19,player));
    Check(modelReads==coldModelReads);
    const auto old=backend.binding;Check(prepare({true,83,23,80}));
    Check(!p345.Current(old)&&p345.Read(backend.binding,read)&&read.toggleAim&&read.bulletDamage==83&&read.reserveLimit==80);
    P345OwnerFrame untouched{};untouched.bulletDamage=987;
    Check(!p345.Read(old,untouched)&&untouched.bulletDamage==987);
    Check(!P345OwnerView::BackendCurrent(nullptr)&&!P345OwnerView::BackendFrame(nullptr,untouched));
    Check(P345OwnerView::BackendCurrent(&backend.binding)&&P345OwnerView::BackendFrame(&backend.binding,read));
    for(auto rules:{NativeOwnerRules{true,-1,1,24},NativeOwnerRules{true,1,-1,24},NativeOwnerRules{true,1,1,45}})
        Check(!prepare(rules));
    Check(prepare());++player.m_iNumSpawns;
    Check(!p345.Read(backend.binding,untouched)&&untouched.bulletDamage==987);--player.m_iNumSpawns;
    Check(prepare());runtimeAuth.revoke=true;
    Check(!p345.Read(backend.binding,untouched)&&untouched.bulletDamage==987);runtimeAuth.revoke=false;
    RuntimeInitialize();Check(prepare());
    const auto oldCanonical=backend.binding;
    ++runtimeAuth.life;++runtimeAuth.modelGeneration;
    Check(runtime.Prepare(player,Request(),runtimeView,error));
    Check(!p345.Current(oldCanonical));
    Check(prepare());p345.Invalidate();Check(!p345.Current(backend.binding));

    // One canonical inventory pass updates any guest capability exactly once,
    // while native item lists and owner retirement remain authoritative.
    RuntimeInitialize();FrameProbe probe;edict_t probeEdict{};
    probe.pev=&probeEdict.v;probeEdict.v.pContainingEntity=&probeEdict;probeEdict.pvPrivateData=&probe;
    probeEdict.serialnumber=39;probe.m_pPlayer=&player;probe.m_pNext=&weapon;
    player.m_rgpPlayerItems[PISTOL_SLOT]=&probe;
    Check(runtime.BeginPreThink(19,player)&&probe.advances==1);
    Check(!runtime.BeginPreThink(19,player)&&probe.advances==1);
    runtime.Invalidate(19,&player);Check(probe.retirements==1);
    player.m_rgpPlayerItems[PISTOL_SLOT]=nullptr;
    for(int i=2;i<=3;++i)
    {
        runtime.EndMap();ReadModel(argv[i]);Initialize();globals.maxEntities=1024;runtimeAuth={};
        Check(runtime.BeginMap(runtimeAuth.map));
        auto request=Request();request.rig=PistolAnimationRig::CSO;
        Check(runtime.Prepare(player,request,runtimeView,error));
        Check(p345.Prepare(player,runtimeView,{true,61,17,24},PistolAnimationRig::CSO,model,_stricmp,backend,error));
        Check(backend.animation->Select(backend.animationGeneration,Pistol::P345,PlayerAction::Fire,false).has_value());
        runtime.EndMap();Check(!p345.Current(backend.binding));
    }
    std::cout<<"P345 canonical owner/frame adapter: "<<checks<<" SDK checks, zero failures\n";
}
