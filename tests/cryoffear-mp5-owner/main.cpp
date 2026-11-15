// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "native/csol/server/weapons/guest_cof_mp5_owner.hpp"
#include "shared-owner-runtime-sdk.inc"
#include <type_traits>

template<class T> concept HasFirstDraw = requires(T& owner, typename T::Binding key) { owner.FirstDraw(key); };
static_assert(!HasFirstDraw<csol::server::cof::MP5OwnerView>);
static_assert(HasFirstDraw<csol::server::cof::TMPOwnerView>);
static_assert(!std::is_convertible_v<csol::server::cof::MP5OwnerBinding, csol::server::cof::TMPOwnerBinding>);

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
    MP5OwnerView mp5;MP5OwnerBackendView backend{};std::string error;
    const auto prepare=[&](NativeOwnerRules rules=NativeOwnerRules{false,75,19,90}) {
        return mp5.Prepare(player,runtimeView,rules,true,PistolAnimationRig::Stock,model,_stricmp,backend,error);
    };
    Check(prepare()&&mp5.Current(backend.binding));
    TMPOwnerFrame read{};Check(mp5.Read(backend.binding,read));
    Check(!read.firstDrawn&&!read.toggleAim&&read.bulletDamage==75&&read.meleeDamage==19&&read.reserveLimit==90);
    const auto canonical=RuntimeRead(runtimeView.binding);
    Check(canonical.firstDrawn&&!read.firstDrawn);
    Check(read.pressed==canonical.pressed&&read.released==canonical.released&&read.randomSeed==canonical.randomSeed&&
        read.originalFlags==canonical.originalFlags&&read.condition3AA==canonical.condition3AA&&
        read.eventOwner2814==canonical.eventOwner2814&&read.sourceMode==canonical.sourceMode);
    Check(!read.quietAudio&&!read.infiniteAmmo&&!read.pendingGate&&read.activityBlock==0&&read.pendingGateTime==0);
    Check(backend.animation->Select(backend.animationGeneration,LongGun::MP5,PlayerAction::Fire,false).has_value());
    Check(!backend.animation->Select(backend.animationGeneration,LongGun::TMP,PlayerAction::Fire,false).has_value());
    TMPOwnerView tmp;TMPOwnerBackendView tmpBackend{};
    Check(tmp.Prepare(player,runtimeView,{true,91,23,300},false,PistolAnimationRig::Stock,model,_stricmp,tmpBackend,error));
    Check(tmp.FirstDraw(tmpBackend.binding)&&mp5.Read(backend.binding,read)&&!read.firstDrawn);

    const auto coldReads=modelReads;
    for(int i=0;i<10000;++i)Check(mp5.Read(backend.binding,read)&&!read.firstDrawn);
    Check(modelReads==coldReads);
    edict_t ground{};player.pev->groundentity=&ground;player.pev->button=IN_JUMP|IN_ATTACK2;
    Check(runtime.BeginPreThink(19,player)&&runtime.BeginInput(19,player)&&runtime.AcceptedJump(19,player));
    Check(mp5.Read(backend.binding,read)&&read.condition3AA&&read.pressed==(IN_JUMP|IN_ATTACK2));
    Check(runtime.AfterMovement(19,player)&&runtime.EndPostThink(19,player)&&modelReads==coldReads);
    const auto old=backend.binding;
    Check(prepare({true,83,23,300}));
    Check(!mp5.Current(old)&&mp5.Read(backend.binding,read)&&!read.firstDrawn&&read.toggleAim&&read.reserveLimit==300);
    TMPOwnerFrame untouched{};untouched.bulletDamage=987;
    Check(!mp5.Read(old,untouched)&&untouched.bulletDamage==987);
    Check(!MP5OwnerView::BackendCurrent(nullptr)&&!MP5OwnerView::BackendFrame(nullptr,untouched));
    Check(MP5OwnerView::BackendCurrent(&backend.binding)&&MP5OwnerView::BackendFrame(&backend.binding,read));
    for(auto rules:{NativeOwnerRules{true,-1,1,90},NativeOwnerRules{true,1,-1,300},NativeOwnerRules{true,1,1,24}})
        Check(!prepare(rules));
    Check(prepare());++player.m_iNumSpawns;
    Check(!mp5.Read(backend.binding,untouched)&&untouched.bulletDamage==987);--player.m_iNumSpawns;
    Check(prepare());runtimeAuth.revoke=true;
    Check(!mp5.Read(backend.binding,untouched)&&untouched.bulletDamage==987);runtimeAuth.revoke=false;
    RuntimeInitialize();Check(prepare());
    const auto oldCanonical=backend.binding;++runtimeAuth.life;++runtimeAuth.modelGeneration;
    Check(runtime.Prepare(player,Request(),runtimeView,error));
    Check(!mp5.Current(oldCanonical));Check(prepare());mp5.Invalidate();Check(!mp5.Current(backend.binding));

    RuntimeInitialize();Check(prepare());
    CBasePlayer second;edict_t secondEdict{};second.pev=&secondEdict.v;
    secondEdict.v.pContainingEntity=&secondEdict;secondEdict.pvPrivateData=&second;secondEdict.serialnumber=99;
    second.pev->health=100;second.pev->modelindex=1;
    for(auto& item:second.m_rgpPlayerItems)item=nullptr;
    second.m_afButtonLast=second.m_afButtonPressed=second.m_afButtonReleased=0;second.random_seed=71;
    g_engfuncs.pfnIndexOfEdict=[](const edict_t* e){return e==&ownerEdict?19:20;};
    OwnerBackendView otherCanonical{};Check(runtime.Prepare(second,Request(20),otherCanonical,error));
    MP5OwnerView other;MP5OwnerBackendView otherBackend{};
    Check(other.Prepare(second,otherCanonical,{true,91,13,300},false,PistolAnimationRig::Stock,
        model,_stricmp,otherBackend,error));
    Check(other.Read(otherBackend.binding,read)&&!read.firstDrawn&&read.randomSeed==71&&mp5.Current(backend.binding));
    runtime.Invalidate(20,&second);Check(!other.Current(otherBackend.binding)&&mp5.Current(backend.binding));
    player.m_rgpPlayerItems[PISTOL_SLOT]=nullptr;
    for(int i=2;i<=3;++i)
    {
        runtime.EndMap();ReadModel(argv[i]);Initialize();globals.maxEntities=1024;runtimeAuth={};
        Check(runtime.BeginMap(runtimeAuth.map));
        auto request=Request();request.rig=PistolAnimationRig::CSO;
        Check(runtime.Prepare(player,request,runtimeView,error));
        Check(mp5.Prepare(player,runtimeView,{true,75,19,90},false,PistolAnimationRig::CSO,
            model,_stricmp,backend,error));
        for(unsigned a=0;a<PlayerActionCount;++a)for(bool crouch:{false,true})
            Check(backend.animation->Select(backend.animationGeneration,LongGun::MP5,PlayerAction(a),crouch).has_value());
        Check(mp5.Read(backend.binding,read)&&!read.firstDrawn);
        runtime.EndMap();Check(!mp5.Current(backend.binding));
    }
    std::cout<<"MP5 canonical owner/frame adapter: "<<checks<<" SDK checks, zero failures\n";
}
