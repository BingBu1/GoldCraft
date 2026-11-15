// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "native/csol/server/weapons/guest_cof_tmp_owner.hpp"
#include "shared-owner-runtime-sdk.inc"

namespace
{
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
    TMPOwnerView tmp;TMPOwnerBackendView backend{};std::string error;
    const auto prepare=[&](NativeOwnerRules rules=NativeOwnerRules{false,75,19,90},bool firstDrawn=false) {
        return tmp.Prepare(player,runtimeView,rules,firstDrawn,PistolAnimationRig::Stock,model,_stricmp,backend,error);
    };
    Check(prepare()&&tmp.Current(backend.binding));
    TMPOwnerFrame read{};Check(tmp.Read(backend.binding,read));
    Check(!read.firstDrawn&&!read.toggleAim&&read.bulletDamage==75&&read.meleeDamage==19&&read.reserveLimit==90);
    const auto canonical=RuntimeRead(runtimeView.binding);
    Check(canonical.firstDrawn&&!read.firstDrawn);
    Check(read.pressed==canonical.pressed&&read.released==canonical.released&&read.randomSeed==canonical.randomSeed&&
        read.originalFlags==canonical.originalFlags&&read.condition3AA==canonical.condition3AA&&
        read.eventOwner2814==canonical.eventOwner2814&&read.sourceMode==canonical.sourceMode);
    Check(!read.quietAudio&&!read.infiniteAmmo&&!read.pendingGate&&read.activityBlock==0&&read.pendingGateTime==0);
    Check(backend.animation->Select(backend.animationGeneration,LongGun::TMP,PlayerAction::WeaponMelee,true).has_value());
    Check(!backend.animation->Select(backend.animationGeneration,Pistol::Glock,PlayerAction::Fire,false).has_value());
    bool saved=true;Check(tmp.PersistentFirstDraw(backend.binding,saved)&&!saved);
    Check(runtime.FirstDraw(runtimeView.binding,false)&&tmp.FirstDraw(backend.binding));
    Check(tmp.Read(backend.binding,read)&&read.firstDrawn&&!RuntimeRead(runtimeView.binding).firstDrawn);
    Check(tmp.PersistentFirstDraw(backend.binding,saved)&&saved);

    const auto coldReads=modelReads;
    for(int i=0;i<10000;++i)Check(tmp.Read(backend.binding,read)&&read.firstDrawn);
    Check(modelReads==coldReads);
    edict_t ground{};player.pev->groundentity=&ground;player.pev->button=IN_JUMP|IN_ATTACK2;
    Check(runtime.BeginPreThink(19,player)&&runtime.BeginInput(19,player)&&runtime.AcceptedJump(19,player));
    Check(tmp.Read(backend.binding,read)&&read.condition3AA&&read.pressed==(IN_JUMP|IN_ATTACK2));
    Check(runtime.AfterMovement(19,player)&&runtime.EndPostThink(19,player)&&modelReads==coldReads);
    const auto old=backend.binding;
    Check(prepare({true,83,23,300},true));
    Check(!tmp.Current(old)&&tmp.Read(backend.binding,read)&&read.firstDrawn&&read.toggleAim&&read.reserveLimit==300);
    TMPOwnerFrame untouched{};untouched.bulletDamage=987;saved=false;
    Check(!tmp.Read(old,untouched)&&untouched.bulletDamage==987);
    Check(!tmp.FirstDraw(old)&&!tmp.PersistentFirstDraw(old,saved)&&!saved);
    Check(!TMPOwnerView::BackendCurrent(nullptr)&&!TMPOwnerView::BackendFrame(nullptr,untouched));
    Check(TMPOwnerView::BackendCurrent(&backend.binding)&&TMPOwnerView::BackendFrame(&backend.binding,read));
    for(auto rules:{NativeOwnerRules{true,-1,1,90},NativeOwnerRules{true,1,-1,300},NativeOwnerRules{true,1,1,24}})
        Check(!prepare(rules));
    Check(prepare());++player.m_iNumSpawns;
    Check(!tmp.Read(backend.binding,untouched)&&untouched.bulletDamage==987);--player.m_iNumSpawns;
    Check(prepare());runtimeAuth.revoke=true;
    Check(!tmp.Read(backend.binding,untouched)&&untouched.bulletDamage==987);runtimeAuth.revoke=false;
    RuntimeInitialize();Check(prepare());
    const auto oldCanonical=backend.binding;
    ++runtimeAuth.life;++runtimeAuth.modelGeneration;
    Check(runtime.Prepare(player,Request(),runtimeView,error));
    Check(!tmp.Current(oldCanonical));Check(prepare());tmp.Invalidate();Check(!tmp.Current(backend.binding));

    RuntimeInitialize();Check(prepare());
    CBasePlayer second;edict_t secondEdict{};second.pev=&secondEdict.v;
    secondEdict.v.pContainingEntity=&secondEdict;secondEdict.pvPrivateData=&second;secondEdict.serialnumber=99;
    second.pev->health=100;second.pev->modelindex=1;
    for(auto& item:second.m_rgpPlayerItems)item=nullptr;
    second.m_afButtonLast=second.m_afButtonPressed=second.m_afButtonReleased=0;second.random_seed=71;
    g_engfuncs.pfnIndexOfEdict=[](const edict_t* e){return e==&ownerEdict?19:20;};
    OwnerBackendView otherCanonical{};Check(runtime.Prepare(second,Request(20),otherCanonical,error));
    TMPOwnerView other;TMPOwnerBackendView otherBackend{};
    Check(other.Prepare(second,otherCanonical,{true,91,13,300},false,PistolAnimationRig::Stock,
        model,_stricmp,otherBackend,error));
    Check(tmp.FirstDraw(backend.binding)&&other.Read(otherBackend.binding,read)&&!read.firstDrawn&&read.randomSeed==71);
    runtime.Invalidate(20,&second);Check(!other.Current(otherBackend.binding)&&tmp.Current(backend.binding));
    player.m_rgpPlayerItems[PISTOL_SLOT]=nullptr;
    for(int i=2;i<=3;++i)
    {
        runtime.EndMap();ReadModel(argv[i]);Initialize();globals.maxEntities=1024;runtimeAuth={};
        Check(runtime.BeginMap(runtimeAuth.map));
        auto request=Request();request.rig=PistolAnimationRig::CSO;
        Check(runtime.Prepare(player,request,runtimeView,error));
        Check(tmp.Prepare(player,runtimeView,{true,75,19,90},i==3,PistolAnimationRig::CSO,
            model,_stricmp,backend,error));
        for(unsigned a=0;a<PlayerActionCount;++a)for(bool crouch:{false,true})
            Check(backend.animation->Select(backend.animationGeneration,LongGun::TMP,PlayerAction(a),crouch).has_value());
        Check(tmp.Read(backend.binding,read)&&read.firstDrawn==(i==3));
        runtime.EndMap();Check(!tmp.Current(backend.binding));
    }
    std::cout<<"TMP canonical owner/frame adapter: "<<checks<<" SDK checks, zero failures\n";
}
