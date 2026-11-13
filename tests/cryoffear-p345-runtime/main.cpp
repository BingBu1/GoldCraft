// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "native/csol/server/weapons/guest_cof_runtime.hpp"
#include "native/csol/server/weapons/guest_cof_p345_factory.hpp"
#include "native/csol/server/weapons/guest_cof_p345_native_effects.hpp"
#include "native/csol/server/weapons/guest_cof_native_effects.hpp"
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
    std::vector<P345Effect> order;
    std::vector<std::byte> strings;
    std::map<std::string,std::vector<byte>> files;
    std::map<std::string,int> stringIds,models,soundIds,generics,events;
} context;

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
GlockRuntimeHost Callbacks(bool p345)
{
    GlockRuntimeHost host{nullptr,CurrentMap,CurrentOwner,PrepareGlock,GlockEffectBoundary,
        GlockRequiredEffects&~GlockNativeSoundEffects,false};
    if(p345)host.p345={PrepareP345,P345EffectBoundary,P345RequiredEffects&~P345NativeSoundEffects};
    return host;
}
GlockPlayerPreparation PlayerPreparation(bool p345)
{
    GlockPlayerPreparation request{{OwnerMovement::NativeCounterStrike,true,true,true,true,true},
        {true,37,11,45},{true,false},PistolAnimationRig::Stock,model,_stricmp};
    if(p345)request.p345Rules=NativeOwnerRules{true,61,11,24};
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
    for(auto& item:player.m_rgpPlayerItems)item=nullptr;
}
void DestroyP345()
{
    for(auto* memory:privateAllocations)
    {
        auto* p=static_cast<P345Weapon*>(memory);delete p->m_pEntity;p->~P345Weapon();std::free(memory);
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
    std::cout<<"P345 actual map/player/resource/factory runtime: "<<checks<<" SDK checks, zero failures; external visual/message consumers remain controlled boundaries\n";
}
