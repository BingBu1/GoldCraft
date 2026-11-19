// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "native/csol/server/weapons/guest_cof_g43_native_effects.hpp"
#include "native/guest/cof/presentation_protocol.hpp"
// Included after the real shared SDK fixture; only external host consumers are
// controlled. The map runtime, resources, owner, factory and G43 class are real.
// Stamina is an external CSOL ledger, never an original CoF cycle.
namespace runtime_fixture
{
struct G43Stage final:goldcraft::IPlayerThinkStage
{
    void* context{};goldcraft::PlayerThinkStageCallback callback{};std::uint64_t token{},serial{};
    std::uint64_t Register(void* c,goldcraft::PlayerThinkStageCallback f) noexcept override
    {if(token)return 0;context=c;callback=f;return token=++serial;}
    bool Current(std::uint64_t value) const noexcept override {return value&&value==token;}
    bool Release(std::uint64_t value) noexcept override
    {if(!Current(value))return false;token=0;callback=nullptr;return true;}
    void Fire(double now) {if(callback)callback(context,player.edict(),0,now);}
};
G43Stage g43Stage;
PlayerActionRuntime g43Actions;
PlayerActionBinding g43Life;
struct G43Ledger {float amount{100};bool readable{true},infinite{};} g43Ledger;
unsigned g43Fires{},g43Pushes{},g43Effects{};
bool failG43Actions{},retireG43Effect{},failG43Registration{},failDifferentG43Actions{};
bool DifferentG43ActionsFor(void*,CBasePlayer&,std::uint64_t,std::uint64_t,PlayerActionBinding&) noexcept;
std::array<edict_t,33> g43Listeners;
std::array<CBasePlayer,33> g43ListenerObjects;
unsigned g43Packets{},g43RegistrationCalls{},g43WireBytes{},g43WireCoords{};
int g43Recipient{},g43WireIndex{};
std::array<int,4> g43WireHeader{};
std::array<float,3> g43WireOrigin{};
bool g43MessageOpen{};
void G43MessageEngine()
{
    globals.maxClients=32;g43Packets=g43RegistrationCalls=0;
    for(unsigned i=1;i<g43Listeners.size();++i)
    {
        auto& entity=g43Listeners[i];entity={};entity.serialnumber=int(100+i);
        entity.v.pContainingEntity=&entity;entity.pvPrivateData=&g43ListenerObjects[i];
        g43ListenerObjects[i].pev=&entity.v;
    }
    g_engfuncs.pfnRegUserMsg=[](const char* name,int bytes)
    {Check(!std::strcmp(name,guest::cof::SoundMessage)&&bytes==-1);++g43RegistrationCalls;return failG43Registration?0:77;};
    g_engfuncs.pfnPEntityOfEntIndex=[](int i)->edict_t*
    {return i==19?player.edict():i>0&&i<33?&g43Listeners[unsigned(i)]:nullptr;};
    g_engfuncs.pfnIndexOfEdict=[](const edict_t* entity)
    {if(entity==player.edict())return 19;for(unsigned i=1;i<33;++i)if(entity==&g43Listeners[i])return int(i);return 40;};
    g_engfuncs.pfnMessageBegin=[](int destination,int message,const float* origin,edict_t* recipient)
    {
        Check(!g43MessageOpen&&destination==MSG_ONE&&message==77&&!origin&&recipient);
        g43MessageOpen=true;g43Recipient=g_engfuncs.pfnIndexOfEdict(recipient);g43WireBytes=g43WireCoords=0;
    };
    g_engfuncs.pfnWriteByte=[](int value){Check(g43MessageOpen&&g43WireBytes<4);g43WireHeader[g43WireBytes++]=value;};
    g_engfuncs.pfnWriteCoord=[](float value){Check(g43MessageOpen&&g43WireCoords<3);g43WireOrigin[g43WireCoords++]=value;};
    g_engfuncs.pfnWriteShort=[](int value){Check(g43MessageOpen);g43WireIndex=value;};
    g_engfuncs.pfnWriteString=[](const char* value){Check(g43MessageOpen&&!std::strcmp(value,G43LocalWaves[0].original));};
    g_engfuncs.pfnMessageEnd=[]()
    {
        Check(g43MessageOpen&&g43WireBytes==4&&g43WireCoords==3&&g43WireIndex==g43Recipient);
        Check((g43WireHeader==std::array<int,4>{guest::cof::SoundProtocol,0,1,g43Recipient==19?0:1}));
        Check((g43WireOrigin==std::array<float,3>{2,3,4}));
        ++g43Packets;if(g43Recipient==19)++g43Fires;g43MessageOpen=false;
    };
}

bool G43ActionCurrent(void*,CBasePlayer& actor,std::uint64_t map,std::uint64_t life) noexcept
{return context.active&&&actor==&player&&map==context.epoch&&life;}
bool G43Task(void*,CBasePlayer&,PlayerThinkKind kind) noexcept
{
    Check(kind==PlayerThinkKind::Pushback);++g43Pushes;
    // The actual host debit owns this balance; the weapon service cannot seed,
    // restore, recover, or publish a second copy.
    g43Ledger.amount-=15;return true;
}
bool G43SharedStamina(void*,float& amount,bool& infinite) noexcept
{
    if(!g43Ledger.readable)return false;
    amount=g43Ledger.amount;infinite=g43Ledger.infinite;return true;
}
bool G43ActionsBase(void*,CBasePlayer& actor,std::uint64_t map,std::uint64_t life,PlayerActionBinding& output) noexcept
{
    if(failG43Actions)return false;
    const SharedPlayerActionBackend backend{nullptr,G43ActionCurrent,G43Task,G43SharedStamina,
        PlayerThinkBit(PlayerThinkKind::Pushback)};
    g43Life=g43Actions.PrepareShared(19,actor,life,backend);
    if(!g43Life.PreparedFor(actor,map,life))return false;
    output=g43Life;return true;
}
bool G43ActionsFor(void* context,CBasePlayer& actor,std::uint64_t map,std::uint64_t life,PlayerActionBinding& output) noexcept
{
    return failDifferentG43Actions?DifferentG43ActionsFor(context,actor,map,life,output):
        G43ActionsBase(context,actor,map,life,output);
}
bool PrepareG43Boundary(void*,const G43Resources&,G43Prepared& value) noexcept
{
    value.ballistics.customDecalMessage=77;value.world=MeleeWorld::Multiplayer;
    value.pushbackReady=value.particleReady=value.deployPreludeReady=true;return true;
}
bool G43EffectBoundary(void*,const G43Resources& resources,const OwnerBinding& binding,CBasePlayer& actor,
    CBasePlayerWeapon& item,const G43Event& event) noexcept
{
    Check(binding.runtime&&binding.runtime->Current(binding)&&&actor==&player&&item.m_pPlayer==&player&&item.m_iId==G43HostId);
    ++g43Effects;
    Check(event.kind!=G43Effect::FireSound);
    if(event.kind==G43Effect::CameraZoom)Check(event.a==350&&event.value==0&&event.other==0&&!event.enabled&&event.first==Vector{});
    if(event.kind==G43Effect::Gunsmoke)Check(event.enabled&&event.other==1&&event.modelString==resources.viewModelString);
    if(event.kind==G43Effect::Recoil&&retireG43Effect)
    {
        context.runtime->EndMap();
        context.releaseWhileBorrowed=context.runtime->EngineEntitiesReleased(context.epoch);
    }
    return true;
}
GlockRuntimeHost G43Callbacks()
{
    auto host=Callbacks(false);
    host.g43={PrepareG43Boundary,G43EffectBoundary,G43ActionsFor,
        G43RequiredEffects & ~(G43NativeSoundEffects | (1u<<unsigned(G43Effect::FireSound))),guest::cof::PlaySfxRules::Cooperative};
    host.weaponCurrent=[](void*,CBasePlayer&,int id) noexcept
    {return id==G43HostId||id==guest::cof::Pistols[0].hostId;};
    return host;
}

// Both long guns must resolve the same player-owned task. Rejecting a different
// valid service preserves the current gun; it must never create two pushbacks.
G43Stage differentG43Stage;
PlayerActionRuntime differentG43Actions;
bool DifferentG43ActionsFor(void*,CBasePlayer& actor,std::uint64_t map,std::uint64_t life,PlayerActionBinding& output) noexcept
{
    const SharedPlayerActionBackend backend{nullptr,G43ActionCurrent,G43Task,G43SharedStamina,
        PlayerThinkBit(PlayerThinkKind::Pushback)};
    output=differentG43Actions.PrepareShared(19,actor,life,backend);return output.PreparedFor(actor,map,life);
}
void CheckG43SharedLongGuns(GlockRuntime& runtime)
{
    context.epoch=runtime.LastIssuedMapEpoch()+1;
    Check(g43Actions.Install(g43Stage,context.epoch));
    Check(differentG43Actions.Install(differentG43Stage,context.epoch));
    auto host=M16Callbacks();host.g43=G43Callbacks().g43;host.m16.actions=G43ActionsBase;
    host.weaponCurrent=[](void*,CBasePlayer&,int id) noexcept
    {return id==M16HostId||id==G43HostId||id==guest::cof::Pistols[0].hostId;};
    Check(bool(runtime.BeginMap(host))&&runtime.PreparedM16Resources()&&runtime.PreparedG43Resources());
    auto request=PlayerPreparation(false);request.m16Rules=NativeOwnerRules{true,65,0,60};
    request.g43Rules=NativeOwnerRules{true,65,0,30};std::string error;
    g43Ledger.amount=48;Check(runtime.PlayerSpawned(player,request,error));
    M16Backends m16;G43Backends g43;
    Check(runtime.Backend(player,m16)&&runtime.Backend(player,g43)&&m16.playerThink==g43.playerThink&&m16.ownerLife==g43.ownerLife);
    M16OwnerFrame m16Frame;G43OwnerFrame g43Frame;
    Check(m16.ownerFrame(m16.context,m16Frame)&&g43.ownerFrame(g43.context,g43Frame)&&m16Frame.stamina==48&&g43Frame.stamina==48);
    const auto currentLife=g43.ownerLife;
    edict_t entity;InitEdict(entity);auto* gun=CreateG43(entity.v);Check(gun);gun->Spawn();
    Check(gun->AddToPlayer(&player));gun->AttachToPlayer(&player);
    player.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]=gun;player.m_pActiveItem=gun;
    globals.time=600;player.pev->button=0;Check(gun->Deploy());
    // BeginMap always ends the old map, so inject the alternative through a
    // controlled callback while the map stays active instead.
    failDifferentG43Actions=true;
    Check(runtime.PlayerModelChanged(player,request,error)&&runtime.Backend(player,m16)&&runtime.Backend(player,g43));
    Check(m16.playerThink&&!g43.playerThink&&gun->RestoreInventoryBinding(player)&&gun->CanDeploy());
    Check(g43.ownerFrame(g43.context,g43Frame)&&!g43Frame.stamina);
    failDifferentG43Actions=false;
    Check(runtime.PlayerModelChanged(player,request,error)&&runtime.Backend(player,m16)&&runtime.Backend(player,g43));
    Check(g43.playerThink==m16.playerThink&&gun->RestoreInventoryBinding(player)&&gun->CanDeploy());
    globals.time=gun->ActionState().primary+1;g43Pushes=0;gun->SecondaryAttack();
    const auto due=g43.playerThink->Deadline();Check(due>globals.time&&m16.playerThink->Deadline()==due);
    Check(g43.playerThink->SourceHostId()==G43HostId&&m16.playerThink->SourceHostId()==G43HostId);
    Check(g43Life.BeginCommand());g43Stage.Fire(due);Check(g43Life.EndSharedCommand());
    Check(g43Pushes==1&&g43Ledger.amount==33&&m16.ownerFrame(m16.context,m16Frame)&&m16Frame.stamina==33);
    Check(runtime.PlayerModelChanged(player,request,error)&&runtime.Backend(player,m16)&&runtime.Backend(player,g43));
    Check(m16.playerThink==g43.playerThink&&g43.ownerLife==currentLife&&g43.ownerFrame(g43.context,g43Frame)&&g43Frame.stamina==33&&gun->RestoreInventoryBinding(player));
    player.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]=nullptr;player.m_pActiveItem=nullptr;
    DestroyP345();entity.pvPrivateData=nullptr;
    const auto epoch=runtime.MapEpoch();runtime.EndMap();Check(runtime.EngineEntitiesReleased(epoch));
    g43Actions.Reset();differentG43Actions.Reset();
}

void CheckG43Runtime(GlockRuntime& runtime,const fs::path& root,bool m16Prepared)
{
    const auto before=checks;G43MessageEngine();
    const auto put=[&](const char* path)
    {if(!context.files.contains(path))context.files.emplace(path,Read(root/path));};
    for(const auto& path:G43Models)put(path.host);
    for(const auto& path:G43ClientModels)put(path.host);
    for(const auto& path:G43Sounds)put(("sound/"+std::string(path.host)).c_str());
    for(const auto& path:G43LocalWaves)put(path.host);
    for(const auto& path:G43Events)put(path.host);
    put(G43Inventory.host);put(G43Particle.host);put(G43ClipTexture.host);
    context.epoch=runtime.LastIssuedMapEpoch()+1;
    for(unsigned missing : {0U, 1U, 3U, 4U})
    {
        auto host=G43Callbacks();
        if(missing==0)host.g43.prepare=nullptr;
        else if(missing==1)host.g43.effect=nullptr;
        else if(missing==3)host.g43.effects&=~(1u<<unsigned(G43Effect::Gunsmoke));
        else host.g43.soundRules=guest::cof::PlaySfxRules::Unsupported;
        Check(!runtime.BeginMap(host)&&!runtime.PreparedG43Resources());
    }
    const auto inventory=context.files.at(G43Inventory.host);context.files.erase(G43Inventory.host);
    Check(!runtime.BeginMap(G43Callbacks())&&!runtime.PreparedG43Resources());
    Check(runtime.EngineEntitiesReleased(runtime.LastIssuedMapEpoch()));context.files.emplace(G43Inventory.host,inventory);
    context.epoch=runtime.LastIssuedMapEpoch()+1;failG43Registration=true;
    Check(!runtime.BeginMap(G43Callbacks())&&!runtime.PreparedG43Resources());
    Check(runtime.EngineEntitiesReleased(runtime.LastIssuedMapEpoch()));failG43Registration=false;
    context.epoch=runtime.LastIssuedMapEpoch()+1;
    g43Actions.Reset();Check(g43Actions.Install(g43Stage,context.epoch));
    Check(bool(runtime.BeginMap(G43Callbacks()))&&runtime.PreparedG43Resources());
    Check(g43RegistrationCalls==2);
    auto request=PlayerPreparation(false);request.g43Rules=NativeOwnerRules{true,65,0,30};
    std::string error;Check(runtime.PlayerSpawned(player,request,error));
    G43Backends backend;Check(runtime.Backend(player,backend)&&backend.preparedEffects==G43RequiredEffects);
    G43OwnerFrame frame;Check(backend.ownerFrame(backend.context,frame)&&frame.stamina==100&&frame.reserveLimit==30);
    edict_t entity;InitEdict(entity);auto* gun=CreateG43(entity.v);Check(gun);gun->Spawn();
    Check(gun->ResourcesReady()&&gun->m_iId==G43HostId&&gun->m_iDefaultAmmo==10);
    Check(gun->AddToPlayer(&player)&&gun->m_iClip==10&&gun->m_iPrimaryAmmoType==8);
    gun->AttachToPlayer(&player);player.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]=gun;player.m_pActiveItem=gun;
    globals.time=500;player.pev->button=0;Check(gun->Deploy());
    globals.time=std::nextafter(gun->ActionState().primary,std::numeric_limits<float>::infinity());
    player.pev->button=IN_ATTACK;
    Check(runtime.BeginPreThink(player)&&runtime.BeginInput(player)&&runtime.AfterMovement(player));
    g43Fires=g43Pushes=0;gun->PrimaryAttack();
    Check(gun->Status()==G43Status::Applied&&gun->m_iClip==9&&g43Fires==1&&g43Packets==32);
    Check(runtime.EndPostThink(player));
    // A missing sound endpoint must report failure without permanently revoking
    // the real weapon/owner; after the endpoint returns the next shot can fire.
    const auto command=[&](int buttons)
    {
        globals.time=gun->ActionState().primary+1;player.pev->button=buttons;
        Check(runtime.BeginPreThink(player)&&runtime.BeginInput(player)&&runtime.AfterMovement(player));
    };
    command(0);Check(runtime.EndPostThink(player));command(IN_ATTACK);
    const auto writeString=g_engfuncs.pfnWriteString;g_engfuncs.pfnWriteString=nullptr;
    gun->PrimaryAttack();Check(gun->Status()==G43Status::MissingBackend&&gun->m_iClip==8&&g43Packets==32);
    Check(backend.current(backend.context)&&g43Life.Current()&&runtime.EndPostThink(player));
    g_engfuncs.pfnWriteString=writeString;command(0);Check(runtime.EndPostThink(player));command(IN_ATTACK);
    gun->PrimaryAttack();Check(gun->Status()==G43Status::Applied&&gun->m_iClip==7&&g43Fires==2&&g43Packets==64);
    Check(runtime.EndPostThink(player));
    g43Ledger.readable=false;command(0);Check(runtime.EndPostThink(player));command(IN_ATTACK);
    Check(backend.ownerFrame(backend.context,frame)&&!frame.stamina&&backend.current(backend.context));
    gun->PrimaryAttack();Check(gun->Status()==G43Status::Applied&&gun->m_iClip==6&&g43Packets==96);
    Check(runtime.EndPostThink(player));g43Ledger.readable=true;player.pev->button=0;const auto loads=context.loads;
    // Failed action preparation and invalid rules cannot retire the current gun.
    const auto* existingThink = backend.playerThink;
    failG43Actions=true;
    Check(runtime.PlayerModelChanged(player,request,error)&&runtime.Backend(player,backend)&&
        backend.playerThink==existingThink&&gun->RestoreInventoryBinding(player));
    failG43Actions=false;auto invalid=request;invalid.g43Rules->reserveLimit=90;
    Check(!runtime.PlayerModelChanged(player,invalid,error)&&backend.current(backend.context));
    globals.time=520;gun->m_iClip=2;player.m_rgAmmo[8]=1000;gun->Reload();Check(gun->ActionState().loading);
    const auto deadline=gun->ActionState().ownerAttack,primary=gun->m_flNextPrimaryAttack;
    const auto life=backend.ownerLife;g43Ledger.amount=37;runtime.PlayerModelChanging(player);
    Check(!backend.current(backend.context)&&g43Life.Current());
    Check(runtime.PlayerModelChanged(player,request,error)&&runtime.Backend(player,backend)&&backend.ownerLife==life&&gun->RestoreInventoryBinding(player));
    Check(gun->ActionState().loading&&gun->ActionState().ownerAttack==deadline&&gun->m_flNextPrimaryAttack==primary);
    Check(backend.ownerFrame(backend.context,frame)&&frame.stamina==37);
    globals.time=std::nextafter(deadline,std::numeric_limits<float>::infinity());gun->ItemPostFrame();
    Check(!gun->ActionState().loading&&gun->m_iClip==10&&player.m_rgAmmo[8]==990&&context.loads==loads);
    globals.time=540;player.pev->button=0;gun->ItemPostFrame();gun->SecondaryAttack();
    const auto due=backend.playerThink->Deadline();Check(due>540&&backend.playerThink->SourceHostId()==G43HostId);
    gun->Holster();player.m_pActiveItem=nullptr;
    runtime.PlayerModelChanging(player);Check(g43Life.Current()&&backend.playerThink->Deadline()==due&&backend.playerThink->SourceHostId()==G43HostId);
    Check(runtime.PlayerModelChanged(player,request,error)&&runtime.Backend(player,backend));
    Check(g43Life.BeginCommand());g43Stage.Fire(due);
    Check(g43Pushes==1&&backend.ownerFrame(backend.context,frame)&&frame.stamina==22);
    Check(g43Life.EndSharedCommand()&&g43Ledger.amount==22);
    StaminaCycleState hidden;StaminaCycleOwner hiddenOwner;Check(!g43Life.Read(hidden,hiddenOwner)&&!g43Life.SetStamina(100));
    // Death retires tasks independently of weapon selection. A new spawn gets
    // a new action life; the prior backend and task cannot revive.
    const auto stale=backend;player.pev->health=0;runtime.PlayerDied(player);Check(!g43Life.Current()&&!stale.current(stale.context));
    player.pev->health=100;++player.m_iNumSpawns;
    Check(runtime.PlayerSpawned(player,request,error)&&runtime.Backend(player,backend)&&backend.ownerLife!=life);
    Check(!backend.playerThink->Deadline()&&!stale.current(stale.context));
    player.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]=nullptr;player.m_pActiveItem=nullptr;
    DestroyP345();entity.pvPrivateData=nullptr;const auto epoch=runtime.MapEpoch();
    runtime.EndMap();Check(!g43Life.Current()&&!runtime.PreparedG43Resources()&&runtime.EngineEntitiesReleased(epoch));
    edict_t refused;InitEdict(refused);Check(!CreateG43(refused.v));g43Actions.Reset();
    if(m16Prepared)CheckG43SharedLongGuns(runtime);
    std::cout<<"G43 map/owner/factory/resource composition: "<<checks-before<<" checks; actual target damage/audio/visual endpoints remain controlled\n";
}
}
