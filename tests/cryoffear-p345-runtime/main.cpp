// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "native/csol/server/weapons/guest_cof_runtime.hpp"
#include "native/csol/server/weapons/guest_cof_p345_factory.hpp"
#include "native/csol/server/weapons/guest_cof_p345_native_effects.hpp"
#include "native/csol/server/weapons/guest_cof_native_effects.hpp"
#include "native/csol/server/weapons/guest_cof_revolver_factory.hpp"
#include "native/csol/server/weapons/guest_cof_revolver_native_effects.hpp"
#include "shared-factory-sdk.inc"
#include <filesystem>
#include <map>

namespace runtime_fixture
{
using namespace csol::server::cof;
namespace fs=std::filesystem;
struct Context
{
    GlockRuntime* runtime{};
    std::uint64_t epoch{};
    bool active{true},retireOnRecoil{},releaseWhileBorrowed{};
    unsigned loads{},frees{},sounds{},p345Effects{},glockEffects{};
    unsigned revolverEffects{};
    std::vector<RevolverEffect> revolverOrder;
    bool retireRevolver{};
    std::vector<P345Effect> order;
    std::vector<std::byte> strings;
    std::map<std::string,std::vector<byte>> files;
    std::map<std::string,int> stringIds,models,soundIds,generics,events;
} context;

struct ModelRetireProbe final:CBasePlayerWeapon,OwnerFrameWeapon
{
    unsigned retirements{};
    void AdvanceOwnerFrame() noexcept override {}
    void Invalidate() noexcept override { ++retirements; }
    void OwnerModelChanging() noexcept override
    {
        context.runtime->EndMap();
        context.releaseWhileBorrowed=context.runtime->EngineEntitiesReleased(context.epoch);
    }
};

std::vector<byte> Read(const fs::path& path)
{
    std::ifstream input(path,std::ios::binary);Check(input.good());
    return {std::istreambuf_iterator<char>(input),{}};
}
void Resources(const fs::path& candidate,const fs::path& deployed)
{
    const auto put=[&](const char* path) {
        if(context.files.contains(path))return;
        const auto a=candidate/path,b=deployed/path;
        context.files.emplace(path,Read(fs::is_regular_file(a)?a:b));
    };
    const auto family=[&](const auto& models,const auto& sounds,const auto& waves,const auto& events,
        ResourcePath inventory,ResourcePath particle,ResourcePath clip) {
        for(const auto& p:models)put(p.host);
        for(const auto& p:sounds)put(("sound/"+std::string(p.host)).c_str());
        for(const auto& p:waves)put(p.host);
        for(const auto& p:events)put(p.host);
        put(inventory.host);put(particle.host);put(clip.host);
    };
    family(GlockModels,GlockSounds,GlockLocalWaves,GlockEvents,GlockInventory,GlockParticle,GlockClipTexture);
    family(P345Models,P345Sounds,P345LocalWaves,P345Events,P345Inventory,P345Particle,P345ClipTexture);
    family(RevolverModels,RevolverSounds,RevolverLocalWaves,RevolverEvents,RevolverInventory,RevolverParticle,RevolverClipTexture);
    for(const auto& p:ShotDecals)put(p.host);
}
int String(const char* text)
{
    auto [it,inserted]=context.stringIds.try_emplace(text,int(context.strings.size()));
    if(inserted)
    {
        const auto size=std::strlen(text)+1;Check(context.strings.size()+size<=context.strings.capacity());
        const auto* p=reinterpret_cast<const std::byte*>(text);
        context.strings.insert(context.strings.end(),p,p+size);globals.pStringBase=reinterpret_cast<const char*>(context.strings.data());
    }
    return it->second;
}
int Cache(const char* text,std::map<std::string,int>& entries,bool zero=false)
{return entries.try_emplace(text,int(entries.size())+(zero?0:1)).first->second;}
bool CurrentMap(void*,std::uint64_t epoch) noexcept{return context.active&&epoch==context.epoch;}
bool CurrentOwner(void*,CBasePlayer& actor,const NativeOwnerMode& mode) noexcept
{
    return context.active && &actor==&player && actor.IsAlive()&&mode.movement==OwnerMovement::NativeCounterStrike&&
        mode.freeActivity&&mode.phoneUnavailable&&mode.substanceFree&&mode.finiteAmmo&&mode.firstPerson;
}
bool PrepareGlock(void*,const GlockResources&,GlockPrepared& value) noexcept
{
    value.ballistics.customDecalMessage=77;value.melee.shakeMessage=78;value.melee.recoil=Recoil;
    value.world=MeleeWorld::Multiplayer;value.particleReady=value.deployPreludeReady=true;return true;
}
bool PrepareP345(void*,const P345Resources&,P345Prepared& value) noexcept
{
    value.ballistics.customDecalMessage=77;value.melee.shakeMessage=78;value.melee.recoil=Recoil;
    value.world=MeleeWorld::Multiplayer;value.particleReady=value.deployPreludeReady=true;return true;
}
bool PrepareRevolver(void*,const RevolverResources&,RevolverPrepared& value) noexcept
{
    value.ballistics.customDecalMessage=77;value.melee.shakeMessage=78;value.melee.recoil=Recoil;
    value.world=MeleeWorld::Multiplayer;value.particleReady=value.deployPreludeReady=true;
    value.cylinderReady=value.ejectEventReady=value.reloadStagesReady=value.ammoXReady=true;return true;
}
bool GlockEffectBoundary(void*,const GlockResources&,const OwnerBinding& binding,CBasePlayer& actor,
    CBasePlayerWeapon& item,const GlockEvent&) noexcept
{
    Check(binding.runtime&&binding.runtime->Current(binding) && &actor==&player && item.m_pPlayer==&player);
    ++context.glockEffects;return true;
}
bool P345EffectBoundary(void*,const P345Resources& resources,const OwnerBinding& binding,CBasePlayer& actor,
    CBasePlayerWeapon& item,const P345Event& event) noexcept
{
    Check(binding.runtime&&binding.runtime->Current(binding) && &actor==&player && item.m_pPlayer==&player&&item.m_iId==897);
    ++context.p345Effects;context.order.push_back(event.kind);
    if(event.kind==P345Effect::Gunsmoke)Check(event.enabled&&event.other==0&&event.modelString==resources.viewModelString);
    if(event.kind==P345Effect::FireEvent)Check(event.value==7&&event.other==1);
    if(event.kind==P345Effect::Recoil&&context.retireOnRecoil)
    {
        context.runtime->EndMap();
        context.releaseWhileBorrowed=context.runtime->EngineEntitiesReleased(context.epoch);
    }
    return true;
}
bool RevolverEffectBoundary(void*,const RevolverResources&,const OwnerBinding& binding,CBasePlayer& actor,
    CBasePlayerWeapon& item,const RevolverEvent& event) noexcept
{
    Check(binding.runtime&&binding.runtime->Current(binding)&&&actor==&player&&item.m_pPlayer==&player&&item.m_iId==898);
    ++context.revolverEffects;context.revolverOrder.push_back(event.kind);
    if(event.kind==RevolverEffect::FirstDraw||event.kind==RevolverEffect::Cylinder||event.kind==RevolverEffect::ReloadStage)
    {
        RevolverOwnerState value{};Check(context.runtime->PersistentState(actor,value));
        if(event.kind==RevolverEffect::FirstDraw)Check(value.firstDrawn);
        else if(event.kind==RevolverEffect::Cylinder)Check(value.cylinderAngle==event.a&&(!event.enabled||value.cylinderGate==event.b));
        else Check(value.reloadUntil.has_value()&&(event.value==2||value.reloadUntil==event.a));
    }
    if(event.kind==RevolverEffect::Recoil&&context.retireRevolver)
    {
        context.runtime->EndMap();
        context.releaseWhileBorrowed=context.runtime->EngineEntitiesReleased(context.epoch);
    }
    return true;
}
GlockRuntimeHost Callbacks(bool p345,bool revolver=false)
{
    GlockRuntimeHost host{nullptr,CurrentMap,CurrentOwner,PrepareGlock,GlockEffectBoundary,
        GlockRequiredEffects&~GlockNativeSoundEffects,false};
    if(p345)host.p345={PrepareP345,P345EffectBoundary,P345RequiredEffects&~P345NativeSoundEffects};
    if(revolver)host.revolver={PrepareRevolver,RevolverEffectBoundary,RevolverRequiredEffects&~RevolverNativeSoundEffects};
    return host;
}
GlockPlayerPreparation PlayerPreparation(bool p345,bool revolver=false)
{
    GlockPlayerPreparation request{{OwnerMovement::NativeCounterStrike,true,true,true,true,true},
        {true,37,11,45},{true,false},PistolAnimationRig::Stock,model,_stricmp};
    if(p345)request.p345Rules=NativeOwnerRules{true,61,11,24};
    if(revolver)request.revolver=GlockPlayerPreparation::RevolverPreparation{{true,89,13,15},{false,144,12.5F}};
    return request;
}
void Engine()
{
    g_engfuncs.pfnPvAllocEntPrivateData=[](edict_t* entity,int32 amount)->void* {
        Check(amount>=int(sizeof(P345Weapon)));
        auto* memory=std::calloc(1,std::size_t(amount));Check(memory!=nullptr);
        privateAllocations.push_back(memory);entity->pvPrivateData=memory;++allocated;return memory;
    };
    const int end=int(weapon.pev->classname)+int(std::strlen(STRING(weapon.pev->classname)))+1;
    context.strings.reserve(1024*1024);
    const auto* base=reinterpret_cast<const std::byte*>(globals.pStringBase);
    context.strings.assign(base,base+end);globals.pStringBase=reinterpret_cast<const char*>(context.strings.data());
    globals.maxEntities=1024;
    g_engfuncs.pfnIndexOfEdict=[](const edict_t* e){return e==player.edict()?19:40;};
    g_engfuncs.pfnGetFileSize=[](const char* name){auto it=context.files.find(name);return it==context.files.end()?-1:int(it->second.size());};
    g_engfuncs.pfnLoadFileForMe=[](const char* name,int* size)->byte* {
        auto it=context.files.find(name);if(it==context.files.end()){*size=0;return nullptr;}
        ++context.loads;*size=int(it->second.size());auto* p=static_cast<byte*>(std::malloc(it->second.size()));Check(p!=nullptr);
        std::memcpy(p,it->second.data(),it->second.size());return p;
    };
    g_engfuncs.pfnFreeFile=[](void* p){++context.frees;std::free(p);};
    g_engfuncs.pfnPrecacheModel=[](const char* p){return Cache(p,context.models);};
    g_engfuncs.pfnPrecacheSound=[](const char* p){return Cache(p,context.soundIds);};
    g_engfuncs.pfnPrecacheGeneric=[](const char* p){return Cache(p,context.generics,true);};
    g_engfuncs.pfnPrecacheEvent=[](int type,const char* p)->unsigned short{Check(type==1);return static_cast<unsigned short>(Cache(p,context.events));};
    g_engfuncs.pfnAllocString=String;
    g_engfuncs.pfnSetModel=[](edict_t* e,const char* path){e->v.modelindex=Cache(path,context.models);e->v.model=String(path);};
    g_engfuncs.pfnSetOrigin=[](edict_t*,const float*){};g_engfuncs.pfnSetSize=[](edict_t*,const float*,const float*){};
    g_engfuncs.pfnEmitSound=[](edict_t* e,int channel,const char* sample,float volume,float attenuation,int flags,int pitch){
        Check(e==player.edict()&&channel==CHAN_AUTO&&volume==1&&attenuation==.8F&&flags==0&&pitch==102);
        Check(!std::strcmp(sample,P345Sounds[unsigned(P345Sound::Sleeve3)].host));++context.sounds;
    };
    CBasePlayerItem::m_AmmoInfoArray[2].pszName="cof_p345";
    CBasePlayerItem::m_AmmoInfoArray[3].pszName="cof_revolver";
    for(auto& item:player.m_rgpPlayerItems)item=nullptr;
}
void DestroyP345()
{
    for(auto* memory:privateAllocations)
    {
        auto* p=static_cast<CBasePlayerWeapon*>(memory);delete p->m_pEntity;
        if(auto* revolver=dynamic_cast<RevolverWeapon*>(p))revolver->~RevolverWeapon();
        else static_cast<P345Weapon*>(p)->~P345Weapon();
        std::free(memory);
    }
    privateAllocations.clear();
}
}
int main(int argc,char** argv)
{
    if(argc!=4)return 1;
    using namespace runtime_fixture;
    std::ifstream input(argv[1],std::ios::binary);Check(input.good());
    const std::vector<char> bytes{std::istreambuf_iterator<char>(input),{}};
    model.resize(bytes.size());std::memcpy(model.data(),bytes.data(),bytes.size());
    Configure();Engine();Resources(argv[2],argv[3]);
    GlockRuntime runtime{{2,64}};context.runtime=&runtime;context.epoch=1;
    Check(bool(runtime.BeginMap(Callbacks(false)))&&runtime.Resources()&&!runtime.PreparedP345Resources());
    std::string error;Check(runtime.PlayerSpawned(player,PlayerPreparation(false),error));
    GlockBackends ordinary{};P345Backends unavailable{};
    Check(runtime.Backend(player,ordinary)&&!runtime.Backend(player,unavailable));
    runtime.EndMap();Check(runtime.EngineEntitiesReleased(1));

    context.epoch=2;Check(bool(runtime.BeginMap(Callbacks(true)))&&runtime.PreparedP345Resources());
    Check(context.loads==context.frees);
    Check(runtime.PlayerSpawned(player,PlayerPreparation(true),error));
    P345Backends first{};Check(runtime.Backend(player,first));P345OwnerFrame snapshot{};
    Check(first.ownerFrame(first.context,snapshot)&&snapshot.reserveLimit==24&&snapshot.bulletDamage==61);
    edict_t entity{};InitEdict(entity);
    auto* pistol=CreateP345(entity.v);Check(pistol&&pistol->m_pEntity&&pistol->m_iClip==0);
    pistol->Spawn();Check(pistol->ResourcesReady()&&pistol->m_iId==897&&pistol->m_iDefaultAmmo==8);
    Check(pistol->AddToPlayer(&player)&&pistol->m_iClip==8&&pistol->m_iPrimaryAmmoType==2);
    pistol->AttachToPlayer(&player);player.m_rgpPlayerItems[PISTOL_SLOT]=pistol;player.m_pActiveItem=pistol;
    globals.time=20;Check(pistol->Deploy()&&context.sounds==1);
    globals.time=30;player.pev->button=IN_ATTACK;
    Check(runtime.BeginPreThink(player)&&runtime.BeginInput(player)&&runtime.AfterMovement(player));
    pistol->PrimaryAttack();Check(pistol->Status()==P345Status::Applied&&pistol->m_iClip==7);
    Check(runtime.EndPostThink(player));
    Check(std::find(context.order.begin(),context.order.end(),P345Effect::FireEvent)!=context.order.end());
    const auto eventsBefore=context.p345Effects;
    Check(runtime.PlayerModelChanged(player,PlayerPreparation(true),error));
    Check(!first.current(first.context)&&!pistol->CanDeploy()&&context.p345Effects==eventsBefore);
    Check(pistol->RestoreInventoryBinding(player)&&pistol->CanDeploy()&&pistol->m_iClip==7);
    P345Backends second{};Check(runtime.Backend(player,second)&&second.context!=first.context);

    globals.time=40;player.pev->button=0;
    Check(runtime.BeginPreThink(player)&&runtime.BeginInput(player)&&runtime.AfterMovement(player)&&runtime.EndPostThink(player));
    globals.time=45;player.pev->button=IN_ATTACK;context.retireOnRecoil=true;
    Check(runtime.BeginPreThink(player)&&runtime.BeginInput(player)&&runtime.AfterMovement(player));
    pistol->PrimaryAttack();
    Check(!context.releaseWhileBorrowed&&!second.current(second.context)&&!runtime.PreparedP345Resources());
    Check(pistol->Status()==P345Status::Cancelled);
    player.m_rgpPlayerItems[PISTOL_SLOT]=nullptr;player.m_pActiveItem=nullptr;
    DestroyP345();entity.pvPrivateData=nullptr;
    Check(runtime.EngineEntitiesReleased(2));
    Check(!runtime.EngineEntitiesReleased(2));

    context.retireOnRecoil=false;context.epoch=3;
    auto invalidHost=Callbacks(true,true);invalidHost.revolver.effect=nullptr;
    Check(!runtime.BeginMap(invalidHost)&&runtime.LastIssuedMapEpoch()==2);
    Check(bool(runtime.BeginMap(Callbacks(true,true)))&&runtime.PreparedRevolverResources()&&runtime.PreparedP345Resources());
    auto request=PlayerPreparation(true,true);Check(runtime.PlayerSpawned(player,request,error));
    RevolverBackends revolverBackend{};Check(runtime.Backend(player,revolverBackend));
    RevolverOwnerFrame revolverFrame{};Check(revolverBackend.ownerFrame(revolverBackend.context,revolverFrame));
    Check(revolverFrame.bulletDamage==89&&revolverFrame.reserveLimit==15&&!revolverFrame.firstDrawn&&
        revolverFrame.cylinderAngle==144&&revolverFrame.cylinderGate==12.5F);
    edict_t revolverEntity{};InitEdict(revolverEntity);auto* revolver=CreateRevolver(revolverEntity.v);Check(revolver);
    revolver->Spawn();Check(revolver->ResourcesReady()&&revolver->m_iId==898&&revolver->m_iDefaultAmmo==5);
    Check(revolver->AddToPlayer(&player)&&revolver->m_iClip==5&&revolver->m_iPrimaryAmmoType==3);
    revolver->AttachToPlayer(&player);player.m_rgpPlayerItems[PISTOL_SLOT]=revolver;player.m_pActiveItem=revolver;
    globals.time=60;Check(revolver->Deploy());
    RevolverOwnerState saved{};Check(runtime.PersistentState(player,saved)&&saved.firstDrawn&&saved.cylinderAngle==144);
    globals.time=70;player.pev->button=IN_ATTACK;
    Check(runtime.BeginPreThink(player)&&runtime.BeginInput(player)&&runtime.AfterMovement(player));
    revolver->PrimaryAttack();Check(revolver->Status()==RevolverStatus::Applied&&revolver->m_iClip==4);
    Check(runtime.EndPostThink(player)&&runtime.PersistentState(player,saved));
    Check(saved.cylinderAngle!=144&&saved.cylinderGate>70);
    const auto loadsBefore=context.loads,effectsBefore=context.revolverEffects;
    player.m_pActiveItem=&weapon;Check(runtime.PersistentState(player,saved));player.m_pActiveItem=revolver;
    request.revolver->state=saved;
    Check(revolverBackend.ownerLife!=0);
    runtime.PlayerModelChanging(player);
    Check(!revolverBackend.current(revolverBackend.context));
    Check(runtime.PlayerModelChanged(player,request,error)&&!revolverBackend.current(revolverBackend.context));
    Check(context.loads==loadsBefore&&context.revolverEffects==effectsBefore);
    RevolverBackends rebound{};Check(runtime.Backend(player,rebound)&&rebound.context!=revolverBackend.context);
    Check(revolver->RestoreInventoryBinding(player)&&revolver->CanDeploy());
    RevolverOwnerState preserved{};Check(runtime.PersistentState(player,preserved)&&preserved.firstDrawn==saved.firstDrawn&&
        preserved.cylinderAngle==saved.cylinderAngle&&preserved.cylinderGate==saved.cylinderGate);
    Check(context.loads==loadsBefore&&context.revolverEffects==effectsBefore+2&&
        context.revolverOrder[context.revolverOrder.size()-2]==RevolverEffect::Aim&&
        context.revolverOrder.back()==RevolverEffect::Body);
    const auto reloadTrack=rebound.playerAnimation->Select(rebound.playerModelGeneration,Pistol::Revolver,PlayerAction::Reload,false);
    Check(reloadTrack.has_value());
    player.pev->sequence=reloadTrack->sequence;player.pev->gaitsequence=888;player.pev->frame=123;
    player.m_Activity=player.m_IdealActivity=ACT_RELOAD;
    globals.time=76;RevolverEvent reloadStage{RevolverEffect::ReloadStage};reloadStage.a=78;
    Check(rebound.effect(rebound.context,player,*revolver,reloadStage));
    int desired=999;player.m_Activity=ACT_RUN;
    Check(runtime.PreserveRevolverReload(player,PLAYER_WALK,desired)&&desired==reloadTrack->sequence&&
        player.pev->gaitsequence==888&&player.pev->frame==123&&player.m_Activity==ACT_RELOAD);
    globals.time=78;desired=999;
    Check(!runtime.PreserveRevolverReload(player,PLAYER_WALK,desired)&&desired==999);
    Check(!runtime.PreserveRevolverReload(player,PLAYER_JUMP,desired)&&desired==999);
    globals.time=80;player.pev->button=0;
    Check(runtime.BeginPreThink(player)&&runtime.BeginInput(player)&&runtime.AfterMovement(player)&&runtime.EndPostThink(player));
    player.m_rgAmmo[revolver->m_iPrimaryAmmoType]=50;
    revolver->Reload();Check(revolver->Status()==RevolverStatus::Applied&&revolver->CylinderState().loading);
    const auto originalThink=revolver->pev->nextthink,originalAttack=revolver->m_flNextPrimaryAttack;
    Check(originalThink>globals.time&&runtime.PersistentState(player,saved));request.revolver->state=saved;
    const auto previousLife=rebound.ownerLife;
    runtime.PlayerModelChanging(player);Check(!rebound.current(rebound.context)&&revolver->m_pfnThink==nullptr);
    Check(runtime.PlayerModelChanged(player,request,error)&&runtime.Backend(player,rebound)&&rebound.ownerLife==previousLife);
    Check(revolver->RestoreInventoryBinding(player)&&revolver->pev->nextthink==originalThink&&
        revolver->m_flNextPrimaryAttack==originalAttack&&revolver->CylinderState().loading);
    for(int i=0;i<12&&revolver->m_pfnThink;++i)
    {
        globals.time=revolver->pev->nextthink;revolver->DispatchThink();
        if(revolver->CylinderState().pending)
        {
            globals.time=std::nextafter(revolver->CylinderState().commitDeadline,INFINITY);
            revolver->ItemPostFrame();
        }
    }
    Check(revolver->m_iClip==5&&!revolver->CylinderState().loading&&revolver->m_pfnThink==nullptr&&
        player.m_rgAmmo[revolver->m_iPrimaryAmmoType]==45);
    // Userinfo can renew the same live owner without a prior model callback.
    // Rebind after one committed round, with attack cancelling the reload.
    for(bool withdrawal:{false,true})
    {
        const auto beforeRenewalShot=revolver->m_iClip;Check(beforeRenewalShot>0);
        globals.time=std::max(90.0F,globals.time+3.0F);player.pev->button=IN_ATTACK;
        Check(runtime.BeginPreThink(player)&&runtime.BeginInput(player)&&runtime.AfterMovement(player));
        revolver->PrimaryAttack();Check(revolver->m_iClip==beforeRenewalShot-1&&runtime.EndPostThink(player));
        globals.time+=1;player.pev->button=0;
        Check(runtime.BeginPreThink(player)&&runtime.BeginInput(player)&&runtime.AfterMovement(player)&&runtime.EndPostThink(player));
        player.m_rgAmmo[revolver->m_iPrimaryAmmoType]=66;
        revolver->Reload();Check(revolver->CylinderState().loading);
        globals.time=revolver->pev->nextthink;revolver->DispatchThink();
        globals.time=revolver->pev->nextthink;revolver->DispatchThink();
        globals.time=std::nextafter(revolver->CylinderState().commitDeadline,INFINITY);
        revolver->ItemPostFrame();
        player.pev->button=IN_ATTACK;
        Check(runtime.BeginPreThink(player)&&runtime.BeginInput(player)&&runtime.AfterMovement(player));
        revolver->ItemPostFrame();Check(runtime.EndPostThink(player));
        Check(revolver->m_iClip==1&&player.m_rgAmmo[revolver->m_iPrimaryAmmoType]==65&&
            revolver->CylinderState().loading&&revolver->CylinderState().cancel);
        const auto renewalThink=revolver->pev->nextthink,renewalAttack=revolver->m_flNextPrimaryAttack;
        Check(renewalThink>globals.time&&runtime.PersistentState(player,saved));request.revolver->state=saved;
        const auto renewalLife=rebound.ownerLife;
        if(withdrawal)
        {
            // The host captures before publishing a withdrawn capability mask.
            runtime.PlayerModelChanging(player);runtime.PlayerDisconnected(player);
            Check(!runtime.Backend(player,rebound)&&revolver->m_pfnThink==nullptr);
        }
        Check(runtime.PlayerModelChanged(player,request,error)&&runtime.Backend(player,rebound)&&rebound.ownerLife==renewalLife);
        Check(revolver->RestoreInventoryBinding(player));
        if(!revolver->m_pfnThink)std::cerr<<"Renewal lost reload: clip="<<revolver->m_iClip
            <<" reserve="<<player.m_rgAmmo[revolver->m_iPrimaryAmmoType]
            <<" action="<<revolver->ActionState().action<<" loading="<<revolver->CylinderState().loading
            <<" cancel="<<revolver->CylinderState().cancel<<" status="<<int(revolver->Status())<<'\n';
        Check(revolver->m_pfnThink&&revolver->pev->nextthink==renewalThink&&
            revolver->m_flNextPrimaryAttack==renewalAttack&&revolver->CylinderState().loading);
        player.pev->button=0;globals.time=renewalThink;revolver->DispatchThink();
        Check(revolver->m_iClip==1&&!revolver->CylinderState().loading&&!revolver->CylinderState().cancel&&
            revolver->ActionState().action==0&&player.m_rgAmmo[revolver->m_iPrimaryAmmoType]==65);
        if(revolver->m_pfnThink){globals.time=revolver->pev->nextthink;revolver->DispatchThink();}
    }
    globals.time=revolver->m_flNextPrimaryAttack+1;player.pev->button=IN_ATTACK;context.retireRevolver=true;
    Check(runtime.BeginPreThink(player)&&runtime.BeginInput(player)&&runtime.AfterMovement(player));
    revolver->PrimaryAttack();Check(revolver->Status()==RevolverStatus::Cancelled&&!context.releaseWhileBorrowed);
    Check(!runtime.PreparedRevolverResources()&&!rebound.current(rebound.context));
    player.m_rgpPlayerItems[PISTOL_SLOT]=nullptr;player.m_pActiveItem=nullptr;
    DestroyP345();revolverEntity.pvPrivateData=nullptr;Check(runtime.EngineEntitiesReleased(3));
    const auto revolverInventory=context.files.at(RevolverInventory.host);
    context.epoch=4;context.files.erase(RevolverInventory.host);
    Check(!runtime.BeginMap(Callbacks(true,true))&&!runtime.PreparedRevolverResources());
    Check(runtime.EngineEntitiesReleased(4));
    // A model-boundary callback may retire the map while its inventory is
    // being traversed. The retained map must survive until that call returns.
    context.files[RevolverInventory.host]=revolverInventory;context.epoch=5;
    Check(bool(runtime.BeginMap(Callbacks(true,true))));
    Check(runtime.PlayerSpawned(player,PlayerPreparation(true,true),error));
    ModelRetireProbe probe;edict_t probeEntity{};InitEdict(probeEntity);
    probe.pev=&probeEntity.v;probeEntity.pvPrivateData=&probe;probe.m_pPlayer=&player;probe.m_pNext=nullptr;
    player.m_rgpPlayerItems[PISTOL_SLOT]=&probe;player.m_pActiveItem=&probe;
    runtime.PlayerModelChanging(player);
    Check(!runtime.Resources()&&!context.releaseWhileBorrowed&&probe.retirements==1);
    player.m_rgpPlayerItems[PISTOL_SLOT]=nullptr;player.m_pActiveItem=nullptr;
    Check(runtime.EngineEntitiesReleased(5));
    std::cout<<"P345/Revolver actual map/player/resource/factory runtime: "<<checks<<" SDK checks, zero failures; external visual/message consumers remain controlled boundaries\n";
}
