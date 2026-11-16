// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "native/csol/server/weapons/guest_cof_m16_native_effects.hpp"
#include "native/guest/cof/presentation_protocol.hpp"
// Included after the real shared SDK fixture; only external host consumers are
// controlled. The map runtime, resources, owner, factory and M16 class are real.
namespace runtime_fixture
{
struct M16Stage final:goldcraft::IPlayerThinkStage
{
    void* context{};goldcraft::PlayerThinkStageCallback callback{};std::uint64_t token{},serial{};
    std::uint64_t Register(void* c,goldcraft::PlayerThinkStageCallback f) noexcept override
    {if(token)return 0;context=c;callback=f;return token=++serial;}
    bool Current(std::uint64_t value) const noexcept override {return value&&value==token;}
    bool Release(std::uint64_t value) noexcept override
    {if(!Current(value))return false;token=0;callback=nullptr;return true;}
    void Fire(double now) {if(callback)callback(context,player.edict(),0,now);}
};
M16Stage m16Stage;
PlayerActionRuntime m16Actions;
PlayerActionBinding m16Life;
unsigned m16Fires{},m16Pushes{},m16Effects{};
bool failM16Actions{},retireM16Effect{},failM16Registration{};
std::array<edict_t,33> m16Listeners;
std::array<CBasePlayer,33> m16ListenerObjects;
unsigned m16Packets{},m16RegistrationCalls{},m16WireBytes{},m16WireCoords{};
int m16Recipient{},m16WireIndex{};
std::array<int,4> m16WireHeader{};
std::array<float,3> m16WireOrigin{};
bool m16MessageOpen{};
void M16MessageEngine()
{
    globals.maxClients=32;m16Packets=m16RegistrationCalls=0;
    for(unsigned i=1;i<m16Listeners.size();++i)
    {
        auto& entity=m16Listeners[i];entity={};entity.serialnumber=int(100+i);
        entity.v.pContainingEntity=&entity;entity.pvPrivateData=&m16ListenerObjects[i];
        m16ListenerObjects[i].pev=&entity.v;
    }
    g_engfuncs.pfnRegUserMsg=[](const char* name,int bytes)
    {Check(!std::strcmp(name,guest::cof::SoundMessage)&&bytes==-1);++m16RegistrationCalls;return failM16Registration?0:77;};
    g_engfuncs.pfnPEntityOfEntIndex=[](int i)->edict_t*
    {return i==19?player.edict():i>0&&i<33?&m16Listeners[unsigned(i)]:nullptr;};
    g_engfuncs.pfnIndexOfEdict=[](const edict_t* entity)
    {if(entity==player.edict())return 19;for(unsigned i=1;i<33;++i)if(entity==&m16Listeners[i])return int(i);return 40;};
    g_engfuncs.pfnMessageBegin=[](int destination,int message,const float* origin,edict_t* recipient)
    {
        Check(!m16MessageOpen&&destination==MSG_ONE&&message==77&&!origin&&recipient);
        m16MessageOpen=true;m16Recipient=g_engfuncs.pfnIndexOfEdict(recipient);m16WireBytes=m16WireCoords=0;
    };
    g_engfuncs.pfnWriteByte=[](int value){Check(m16MessageOpen&&m16WireBytes<4);m16WireHeader[m16WireBytes++]=value;};
    g_engfuncs.pfnWriteCoord=[](float value){Check(m16MessageOpen&&m16WireCoords<3);m16WireOrigin[m16WireCoords++]=value;};
    g_engfuncs.pfnWriteShort=[](int value){Check(m16MessageOpen);m16WireIndex=value;};
    g_engfuncs.pfnWriteString=[](const char* value){Check(m16MessageOpen&&!std::strcmp(value,M16LocalWaves[2].original));};
    g_engfuncs.pfnMessageEnd=[]()
    {
        Check(m16MessageOpen&&m16WireBytes==4&&m16WireCoords==3&&m16WireIndex==m16Recipient);
        Check((m16WireHeader==std::array<int,4>{guest::cof::SoundProtocol,0,1,m16Recipient==19?0:1}));
        Check((m16WireOrigin==std::array<float,3>{2,3,4}));
        ++m16Packets;if(m16Recipient==19)++m16Fires;m16MessageOpen=false;
    };
}

bool M16ActionCurrent(void*,CBasePlayer& actor,std::uint64_t map,std::uint64_t life) noexcept
{return context.active&&&actor==&player&&map==context.epoch&&life;}
bool M16Task(void*,CBasePlayer&,PlayerThinkKind kind) noexcept
{
    Check(kind==PlayerThinkKind::Pushback);++m16Pushes;
    StaminaCycleState state;StaminaCycleOwner owner;Check(m16Life.Read(state,owner));
    return m16Life.SetStamina(state.amount-15);
}
bool M16CycleCurrent(void*) noexcept {return context.active;}
float M16Clock(void*) noexcept {return globals.time;}
bool M16Multiplayer(void*,bool& value) noexcept {value=true;return true;}
bool M16Random(void*,int,int,int& value) noexcept {value=0;return true;}
bool M16Voice(void*,const StaminaVoiceEvent&) noexcept {return true;}
bool M16Hud(void*,const StaminaHudEvent&) noexcept {return true;}
bool M16ActionsFor(void*,CBasePlayer& actor,std::uint64_t map,std::uint64_t life,PlayerActionBinding& output) noexcept
{
    if(failM16Actions)return false;
    StaminaCycleState seed;seed.amount=seed.lastSent=100;
    StaminaCycleOwner owner;owner.rulesPresent=true;
    const PlayerActionBackend backend{nullptr,M16ActionCurrent,M16Task,
        {nullptr,M16CycleCurrent,M16Clock,M16Multiplayer,M16Random,M16Voice,M16Hud},1};
    m16Life=m16Actions.Prepare(19,actor,life,seed,owner,backend);
    if(!m16Life.PreparedFor(actor,map,life))return false;
    output=m16Life;return true;
}
bool PrepareM16Boundary(void*,const M16Resources&,M16Prepared& value) noexcept
{
    value.ballistics.customDecalMessage=77;value.world=MeleeWorld::Multiplayer;
    value.pushbackReady=value.particleReady=value.deployPreludeReady=true;return true;
}
bool M16EffectBoundary(void*,const M16Resources& resources,const OwnerBinding& binding,CBasePlayer& actor,
    CBasePlayerWeapon& item,const M16Event& event) noexcept
{
    Check(binding.runtime&&binding.runtime->Current(binding)&&&actor==&player&&item.m_pPlayer==&player&&item.m_iId==M16HostId);
    ++m16Effects;
    Check(event.kind!=M16Effect::FireSound);
    if(event.kind==M16Effect::Gunsmoke)Check(event.enabled&&event.other==0&&event.modelString==resources.viewModelString);
    if(event.kind==M16Effect::Recoil&&retireM16Effect)
    {
        context.runtime->EndMap();
        context.releaseWhileBorrowed=context.runtime->EngineEntitiesReleased(context.epoch);
    }
    return true;
}
GlockRuntimeHost M16Callbacks()
{
    auto host=Callbacks(false);
    host.m16={PrepareM16Boundary,M16EffectBoundary,M16ActionsFor,
        M16RequiredEffects & ~(M16NativeSoundEffects | (1u<<unsigned(M16Effect::FireSound))),guest::cof::PlaySfxRules::Cooperative};
    host.weaponCurrent=[](void*,CBasePlayer&,int id) noexcept
    {return id==M16HostId||id==guest::cof::Pistols[0].hostId;};
    return host;
}
void CheckM16Runtime(GlockRuntime& runtime,const fs::path& root)
{
    const auto before=checks;M16MessageEngine();
    const auto put=[&](const char* path)
    {if(!context.files.contains(path))context.files.emplace(path,Read(root/path));};
    for(const auto& path:M16Models)put(path.host);
    for(const auto& path:M16ClientModels)put(path.host);
    for(const auto& path:M16Sounds)put(("sound/"+std::string(path.host)).c_str());
    for(const auto& path:M16LocalWaves)put(path.host);
    for(const auto& path:M16Events)put(path.host);
    put(M16Inventory.host);put(M16Particle.host);put(M16ClipTexture.host);
    context.epoch=runtime.LastIssuedMapEpoch()+1;
    for(unsigned missing=0;missing<5;++missing)
    {
        auto host=M16Callbacks();
        if(missing==0)host.m16.prepare=nullptr;else if(missing==1)host.m16.effect=nullptr;
        else if(missing==2)host.m16.actions=nullptr;else if(missing==4)host.m16.soundRules=guest::cof::PlaySfxRules::Unsupported;else host.m16.effects&=~(1u<<unsigned(M16Effect::Gunsmoke));
        Check(!runtime.BeginMap(host)&&!runtime.PreparedM16Resources());
    }
    const auto inventory=context.files.at(M16Inventory.host);context.files.erase(M16Inventory.host);
    Check(!runtime.BeginMap(M16Callbacks())&&!runtime.PreparedM16Resources());
    Check(runtime.EngineEntitiesReleased(runtime.LastIssuedMapEpoch()));context.files.emplace(M16Inventory.host,inventory);
    context.epoch=runtime.LastIssuedMapEpoch()+1;failM16Registration=true;
    Check(!runtime.BeginMap(M16Callbacks())&&!runtime.PreparedM16Resources());
    Check(runtime.EngineEntitiesReleased(runtime.LastIssuedMapEpoch()));failM16Registration=false;
    context.epoch=runtime.LastIssuedMapEpoch()+1;
    m16Actions.Reset();Check(m16Actions.Install(m16Stage,context.epoch));
    Check(bool(runtime.BeginMap(M16Callbacks()))&&runtime.PreparedM16Resources());
    Check(m16RegistrationCalls==2);
    auto request=PlayerPreparation(false);request.m16Rules=NativeOwnerRules{true,65,0,60};
    std::string error;Check(runtime.PlayerSpawned(player,request,error));
    M16Backends backend;Check(runtime.Backend(player,backend)&&backend.preparedEffects==M16RequiredEffects);
    M16OwnerFrame frame;Check(backend.ownerFrame(backend.context,frame)&&frame.stamina==100&&frame.reserveLimit==60);
    edict_t entity;InitEdict(entity);auto* gun=CreateM16(entity.v);Check(gun);gun->Spawn();
    Check(gun->ResourcesReady()&&gun->m_iId==M16HostId&&gun->m_iDefaultAmmo==20);
    Check(gun->AddToPlayer(&player)&&gun->m_iClip==20&&gun->m_iPrimaryAmmoType==7);
    gun->AttachToPlayer(&player);player.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]=gun;player.m_pActiveItem=gun;
    globals.time=500;player.pev->button=0;Check(gun->Deploy());
    globals.time=std::nextafter(gun->ActionState().primary,std::numeric_limits<float>::infinity());
    gun->ToggleFireMode();Check(!gun->BurstMode());
    globals.time=std::nextafter(gun->ActionState().primary,std::numeric_limits<float>::infinity());
    player.pev->button=IN_ATTACK;
    Check(runtime.BeginPreThink(player)&&runtime.BeginInput(player)&&runtime.AfterMovement(player));
    m16Fires=m16Pushes=0;gun->PrimaryAttack();
    Check(gun->Status()==M16Status::Applied&&gun->m_iClip==19&&m16Fires==1&&m16Packets==32);
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
    gun->PrimaryAttack();Check(gun->Status()==M16Status::MissingBackend&&gun->m_iClip==18&&m16Packets==32);
    Check(backend.current(backend.context)&&m16Life.Current()&&runtime.EndPostThink(player));
    g_engfuncs.pfnWriteString=writeString;command(0);Check(runtime.EndPostThink(player));command(IN_ATTACK);
    gun->PrimaryAttack();Check(gun->Status()==M16Status::Applied&&gun->m_iClip==17&&m16Fires==2&&m16Packets==64);
    Check(runtime.EndPostThink(player));player.pev->button=0;const auto loads=context.loads;
    // Failed action preparation and invalid rules cannot retire the current gun.
    failM16Actions=true;Check(!runtime.PlayerModelChanged(player,request,error)&&backend.current(backend.context));
    failM16Actions=false;auto invalid=request;invalid.m16Rules->reserveLimit=90;
    Check(!runtime.PlayerModelChanged(player,invalid,error)&&backend.current(backend.context));
    globals.time=520;gun->m_iClip=2;player.m_rgAmmo[7]=1000;gun->Reload();Check(gun->ActionState().loading);
    const auto deadline=gun->ActionState().ownerAttack,primary=gun->m_flNextPrimaryAttack;
    const auto life=backend.ownerLife;Check(m16Life.SetStamina(37));runtime.PlayerModelChanging(player);
    Check(!backend.current(backend.context)&&m16Life.Current());
    Check(runtime.PlayerModelChanged(player,request,error)&&runtime.Backend(player,backend)&&backend.ownerLife==life&&gun->RestoreInventoryBinding(player));
    Check(gun->ActionState().loading&&gun->ActionState().ownerAttack==deadline&&gun->m_flNextPrimaryAttack==primary);
    Check(backend.ownerFrame(backend.context,frame)&&frame.stamina==37);
    globals.time=std::nextafter(deadline,std::numeric_limits<float>::infinity());gun->ItemPostFrame();
    Check(!gun->ActionState().loading&&gun->m_iClip==20&&player.m_rgAmmo[7]==980&&context.loads==loads);
    globals.time=540;player.pev->button=0;gun->ItemPostFrame();gun->SecondaryAttack();
    const auto due=backend.playerThink->Deadline();Check(due>540&&backend.playerThink->SourceHostId()==M16HostId);
    gun->Holster();player.m_pActiveItem=nullptr;
    runtime.PlayerModelChanging(player);Check(m16Life.Current()&&backend.playerThink->Deadline()==due);
    Check(runtime.PlayerModelChanged(player,request,error)&&runtime.Backend(player,backend));
    Check(m16Life.BeginCommand());m16Stage.Fire(due);
    Check(m16Pushes==1&&backend.ownerFrame(backend.context,frame)&&frame.stamina==22);
    Check(m16Life.PostThink()==PlayerCycleResult::Applied);
    // Death retires tasks independently of weapon selection. A new spawn gets
    // a new action life; the prior backend and task cannot revive.
    const auto stale=backend;player.pev->health=0;runtime.PlayerDied(player);Check(!m16Life.Current()&&!stale.current(stale.context));
    player.pev->health=100;++player.m_iNumSpawns;
    Check(runtime.PlayerSpawned(player,request,error)&&runtime.Backend(player,backend)&&backend.ownerLife!=life);
    Check(!backend.playerThink->Deadline()&&!stale.current(stale.context));
    player.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]=nullptr;player.m_pActiveItem=nullptr;
    DestroyP345();entity.pvPrivateData=nullptr;const auto epoch=runtime.MapEpoch();
    runtime.EndMap();Check(!m16Life.Current()&&!runtime.PreparedM16Resources()&&runtime.EngineEntitiesReleased(epoch));
    edict_t refused;InitEdict(refused);Check(!CreateM16(refused.v));m16Actions.Reset();
    std::cout<<"M16 map/owner/factory/resource composition: "<<checks-before<<" checks; actual target damage/audio/visual endpoints remain controlled\n";
}
}
