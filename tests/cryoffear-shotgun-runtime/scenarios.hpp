// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "native/csol/server/weapons/guest_cof_shotgun_native_effects.hpp"

// The real SDK class/runtime/resources/owner/factory execute here. Reuse the
// existing shared CSOL ledger and message recorder; damage and rendering remain
// controlled endpoints, not claimed gameplay or eight-pellet acceptance.
namespace guest::cof
{
inline unsigned runtimeShotgunPellets{};
BallisticsResult FireShotgunBallistics(CBaseEntity& owner,const PistolBallistics& shot,
    const BallisticsEffects& effects,const BallisticsCall& call) noexcept
{
    Check(&owner==&player&&CheckPistolBallistics(shot,effects,call)==BallisticsStatus::Ready);
    ++runtimeShotgunPellets;
    return {BallisticsStatus::Fired,{}};
}
}
namespace runtime_fixture
{
unsigned shotgunShells{},shotgunExternal{};
bool retireShotgunEffect{},differentShotgunActions{};
bool ShotgunActionsFor(void* context,CBasePlayer& actor,std::uint64_t map,std::uint64_t life,
    PlayerActionBinding& output) noexcept
{
    return differentShotgunActions?DifferentG43ActionsFor(context,actor,map,life,output):
        G43ActionsBase(context,actor,map,life,output);
}
bool PrepareShotgunBoundary(void*,const ShotgunResources&,ShotgunPrepared& prepared) noexcept
{
    prepared.ballistics.customDecalMessage=77;prepared.world=MeleeWorld::Multiplayer;
    prepared.pushbackReady=prepared.particleReady=prepared.deployPreludeReady=true;
    return true;
}
bool ShotgunEffectBoundary(void*,const ShotgunResources& resources,const OwnerBinding& binding,
    CBasePlayer& actor,CBasePlayerWeapon& item,const ShotgunEvent& event) noexcept
{
    Check(binding.runtime&&binding.runtime->Current(binding)&&&actor==&player&&
        item.m_pPlayer==&player&&item.m_iId==ShotgunHostId);
    Check(event.kind!=ShotgunEffect::FireSound&&event.kind!=ShotgunEffect::ReloadStage&&
        event.kind!=ShotgunEffect::AmmoWarning);
    ++shotgunExternal;
    if(event.kind==ShotgunEffect::Shell)++shotgunShells;
    if(event.kind==ShotgunEffect::Gunsmoke)
        Check(event.enabled&&event.other==1&&event.modelString==resources.viewModelString);
    if(event.kind==ShotgunEffect::Recoil&&retireShotgunEffect)
    {
        context.runtime->EndMap();
        context.releaseWhileBorrowed=context.runtime->EngineEntitiesReleased(context.epoch);
    }
    return true;
}
GlockRuntimeHost ShotgunCallbacks()
{
    auto host=Callbacks(false);
    constexpr auto internal=ShotgunNativeSoundEffects|(1u<<unsigned(ShotgunEffect::FireSound))|
        (1u<<unsigned(ShotgunEffect::ReloadStage))|(1u<<unsigned(ShotgunEffect::AmmoWarning));
    host.shotgun={PrepareShotgunBoundary,ShotgunEffectBoundary,ShotgunActionsFor,
        ShotgunRequiredEffects&~internal,guest::cof::PlaySfxRules::Cooperative};
    host.weaponCurrent=[](void*,CBasePlayer&,int id) noexcept
    {return id==ShotgunHostId||id==guest::cof::Pistols[0].hostId;};
    return host;
}
void ShotgunMessageEngine()
{
    G43MessageEngine();g43Fires=0;
    g_engfuncs.pfnWriteString=[](const char* value)
    {Check(g43MessageOpen&&!std::strcmp(value,ShotgunLocalWaves[0].original));};
}
void CheckShotgunSharedLongGuns(GlockRuntime& runtime)
{
    context.epoch=runtime.LastIssuedMapEpoch()+1;
    Check(g43Actions.Install(g43Stage,context.epoch));
    Check(differentG43Actions.Install(differentG43Stage,context.epoch));
    auto host=M16Callbacks();host.g43=G43Callbacks().g43;host.shotgun=ShotgunCallbacks().shotgun;
    host.m16.actions=host.g43.actions=G43ActionsBase;
    host.weaponCurrent=[](void*,CBasePlayer&,int id) noexcept
    {return id==ShotgunHostId||id==M16HostId||id==G43HostId||id==guest::cof::Pistols[0].hostId;};
    Check(bool(runtime.BeginMap(host))&&runtime.PreparedM16Resources()&&
        runtime.PreparedG43Resources()&&runtime.PreparedShotgunResources());
    auto request=PlayerPreparation(false);request.m16Rules=NativeOwnerRules{true,65,0,60};
    request.g43Rules=NativeOwnerRules{true,65,0,30};
    request.shotgun=GlockPlayerPreparation::ShotgunPreparation{{true,37,0,15},{}};
    std::string error;g43Ledger.amount=48;
    Check(runtime.PlayerSpawned(player,request,error));
    M16Backends m16;G43Backends g43;ShotgunBackends shotgun;
    Check(runtime.Backend(player,m16)&&runtime.Backend(player,g43)&&runtime.Backend(player,shotgun));
    Check(m16.playerThink==g43.playerThink&&g43.playerThink==shotgun.playerThink&&
        m16.ownerLife==shotgun.ownerLife&&g43.ownerLife==shotgun.ownerLife);
    ShotgunOwnerFrame frame;Check(shotgun.ownerFrame(shotgun.context,frame)&&frame.stamina==48);
    // A different valid service must not retire any of the three current guns.
    differentShotgunActions=true;
    Check(!runtime.PlayerModelChanged(player,request,error)&&shotgun.current(shotgun.context)&&
        m16.current(m16.context)&&g43.current(g43.context));
    differentShotgunActions=false;
    player.m_rgAmmo[9]=0;
    edict_t entity;InitEdict(entity);auto* gun=CreateShotgun(entity.v);Check(gun);gun->Spawn();
    Check(gun->AddToPlayer(&player));gun->AttachToPlayer(&player);
    player.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]=gun;player.m_pActiveItem=gun;
    globals.time=5000;player.pev->button=0;Check(gun->Deploy());
    globals.time=gun->ActionState().primary+1;g43Pushes=0;gun->SecondaryAttack();
    const auto due=shotgun.playerThink->Deadline();
    Check(due>globals.time&&m16.playerThink->Deadline()==due&&
        g43.playerThink->Deadline()==due&&shotgun.playerThink->SourceHostId()==ShotgunHostId);
    Check(g43Life.BeginCommand());g43Stage.Fire(due);Check(g43Life.EndSharedCommand());
    M16OwnerFrame m16Frame;G43OwnerFrame g43Frame;
    Check(g43Pushes==1&&g43Ledger.amount==33&&m16.ownerFrame(m16.context,m16Frame)&&
        g43.ownerFrame(g43.context,g43Frame)&&m16Frame.stamina==33&&g43Frame.stamina==33);
    player.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]=nullptr;player.m_pActiveItem=nullptr;
    DestroyP345();entity.pvPrivateData=nullptr;
    const auto epoch=runtime.MapEpoch();runtime.EndMap();Check(runtime.EngineEntitiesReleased(epoch));
    g43Actions.Reset();differentG43Actions.Reset();
}
void CheckShotgunRuntime(GlockRuntime& runtime,const fs::path& root,bool shared)
{
    const auto before=checks;ShotgunMessageEngine();
    const auto put=[&](const char* path)
    {if(!context.files.contains(path))context.files.emplace(path,Read(root/path));};
    for(const auto& path:ShotgunModels)put(path.host);
    for(const auto& path:ShotgunSounds)put(("sound/"+std::string(path.host)).c_str());
    for(const auto& path:ShotgunLocalWaves)put(path.host);
    for(const auto& path:ShotgunEvents)put(path.host);
    for(const auto& path:ShotDecals)put(path.host);
    put(ShotgunInventory.host);put(ShotgunParticle.host);
    context.epoch=runtime.LastIssuedMapEpoch()+1;
    for(unsigned missing=0;missing<5;++missing)
    {
        auto host=ShotgunCallbacks();
        if(missing==0)host.shotgun.prepare=nullptr;
        else if(missing==1)host.shotgun.effect=nullptr;
        else if(missing==2)host.shotgun.actions=nullptr;
        else if(missing==3)host.shotgun.effects&=~(1u<<unsigned(ShotgunEffect::Shell));
        else host.shotgun.soundRules=guest::cof::PlaySfxRules::Unsupported;
        Check(!runtime.BeginMap(host)&&!runtime.PreparedShotgunResources());
    }
    const auto inventory=context.files.at(ShotgunInventory.host);context.files.erase(ShotgunInventory.host);
    Check(!runtime.BeginMap(ShotgunCallbacks())&&!runtime.PreparedShotgunResources());
    Check(runtime.EngineEntitiesReleased(runtime.LastIssuedMapEpoch()));context.files.emplace(ShotgunInventory.host,inventory);
    context.epoch=runtime.LastIssuedMapEpoch()+1;failG43Registration=true;
    Check(!runtime.BeginMap(ShotgunCallbacks())&&!runtime.PreparedShotgunResources());
    Check(runtime.EngineEntitiesReleased(runtime.LastIssuedMapEpoch()));failG43Registration=false;
    context.epoch=runtime.LastIssuedMapEpoch()+1;
    Check(g43Actions.Install(g43Stage,context.epoch));Check(bool(runtime.BeginMap(ShotgunCallbacks())));
    auto request=PlayerPreparation(false);
    request.shotgun=GlockPlayerPreparation::ShotgunPreparation{{true,37,0,15},{}};
    std::string error;g43Ledger={100,true,false};
    Check(runtime.PlayerSpawned(player,request,error));ShotgunBackends backend;
    Check(runtime.Backend(player,backend)&&backend.playerThink);
    edict_t entity;InitEdict(entity);auto* gun=CreateShotgun(entity.v);Check(gun);gun->Spawn();
    Check(gun->ResourcesReady()&&gun->AddToPlayer(&player));gun->AttachToPlayer(&player);
    player.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]=gun;player.m_pActiveItem=gun;
    globals.time=1000;player.pev->button=0;Check(gun->Deploy());
    const auto command=[&](int buttons)
    {
        globals.time=gun->ActionState().primary+1;player.pev->button=buttons;
        Check(runtime.BeginPreThink(player)&&runtime.BeginInput(player)&&runtime.AfterMovement(player));
    };
    command(0);Check(runtime.EndPostThink(player));command(IN_ATTACK);
    guest::cof::runtimeShotgunPellets=shotgunShells=0;const auto clip=gun->m_iClip;
    gun->PrimaryAttack();Check(gun->Status()==ShotgunStatus::Applied&&gun->m_iClip==clip-1&&
        guest::cof::runtimeShotgunPellets==1&&g43Fires==1&&g43Packets==32&&gun->m_pfnThink);
    Check(runtime.EndPostThink(player));
    globals.time=gun->pev->nextthink;gun->DispatchThink();Check(shotgunShells==1&&!gun->m_pfnThink);
    const auto writer=g_engfuncs.pfnWriteString;
    command(0);Check(runtime.EndPostThink(player));command(IN_ATTACK);
    const auto beforeMissing=gun->m_iClip;g_engfuncs.pfnWriteString=nullptr;gun->PrimaryAttack();
    Check(gun->Status()==ShotgunStatus::MissingBackend&&gun->m_iClip==beforeMissing-1&&
        backend.current(backend.context)&&g43Packets==32&&g43Life.Current());
    Check(runtime.EndPostThink(player));g_engfuncs.pfnWriteString=writer;
    globals.time=gun->pev->nextthink;gun->DispatchThink();
    command(0);Check(runtime.EndPostThink(player));command(IN_ATTACK);gun->PrimaryAttack();
    Check(gun->Status()==ShotgunStatus::Applied&&gun->m_iClip==beforeMissing-2&&g43Packets==64);
    Check(runtime.EndPostThink(player));globals.time=gun->pev->nextthink;gun->DispatchThink();
    // Expanded skill ammo remains usable; unavailable stamina only suspends
    // pushback and must not revoke shooting or canonical weapon identity.
    command(0);Check(runtime.EndPostThink(player));g43Ledger.readable=false;
    command(IN_ATTACK);gun->m_iClip=75;gun->PrimaryAttack();
    Check(gun->m_iClip==74&&gun->Status()==ShotgunStatus::Applied&&g43Packets==96);
    Check(runtime.EndPostThink(player));g43Ledger.readable=true;
    globals.time=gun->pev->nextthink;gun->DispatchThink();
    failG43Actions=true;Check(!runtime.PlayerModelChanged(player,request,error)&&backend.current(backend.context));
    failG43Actions=false;auto invalid=request;invalid.shotgun->rules.reserveLimit=90;
    Check(!runtime.PlayerModelChanged(player,invalid,error)&&backend.current(backend.context));
    invalid=request;invalid.shotgun->state.reloadUntil=std::numeric_limits<float>::infinity();
    Check(!runtime.PlayerModelChanged(player,invalid,error)&&backend.current(backend.context));
    // The original weapon insertion task and authoritative animation deadline
    // both survive a same-life model replacement, with one shell consumed.
    command(0);Check(runtime.EndPostThink(player));gun->m_iClip=1;player.m_rgAmmo[9]=1000;
    gun->Reload();Check(gun->ActionState().reloadStage==1);
    const int reloadSequence=player.pev->sequence;int desired=-77;
    Check(runtime.PreserveShotgunReload(player,PLAYER_IDLE,desired)&&desired==reloadSequence&&player.m_Activity==ACT_RELOAD);
    desired=-77;Check(!runtime.PreserveShotgunReload(player,PLAYER_JUMP,desired)&&desired==-77);
    player.pev->flags|=FL_FROZEN;
    Check(!runtime.PreserveShotgunReload(player,PLAYER_WALK,desired)&&desired==-77);player.pev->flags&=~FL_FROZEN;
    globals.time=gun->ActionState().primary+1;gun->ItemPostFrame();
    Check(gun->ActionState().reloadStage==2&&gun->m_pfnThink);
    ShotgunOwnerState saved;Check(runtime.PersistentState(player,saved)&&saved.reloadUntil);
    request.shotgun->state=saved;const auto due=gun->pev->nextthink;const auto life=backend.ownerLife;
    const auto loads=context.loads;runtime.PlayerModelChanging(player);
    Check(!backend.current(backend.context)&&!gun->m_pfnThink&&g43Life.Current());
    Check(runtime.PlayerModelChanged(player,request,error)&&runtime.Backend(player,backend)&&
        backend.ownerLife==life&&gun->RestoreInventoryBinding(player));
    Check(gun->m_pfnThink&&gun->pev->nextthink==due);
    ShotgunOwnerState restored;Check(runtime.PersistentState(player,restored)&&restored.reloadUntil==saved.reloadUntil);
    globals.time=due;gun->DispatchThink();Check(gun->m_iClip==2&&player.m_rgAmmo[9]==999&&context.loads==loads);
    globals.time=std::nextafter(std::max({globals.time,gun->ActionState().primary,
        gun->ActionState().idle,gun->ActionState().ownerAttack}),std::numeric_limits<float>::infinity());
    gun->ItemPostFrame();Check(gun->m_pfnThink);
    // Death clears retained tasks and makes the previous owner unusable even
    // after a fresh player life is admitted on the same native edict.
    const auto stale=backend;player.pev->health=0;runtime.PlayerDied(player);
    Check(!g43Life.Current()&&!stale.current(stale.context)&&!gun->m_pfnThink);
    player.pev->health=100;++player.m_iNumSpawns;request.shotgun->state={};
    Check(runtime.PlayerSpawned(player,request,error)&&runtime.Backend(player,backend)&&backend.ownerLife!=life);
    Check(!backend.playerThink->Deadline()&&!stale.current(stale.context));
    player.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]=nullptr;player.m_pActiveItem=nullptr;
    DestroyP345();entity.pvPrivateData=nullptr;
    const auto epoch=runtime.MapEpoch();runtime.EndMap();Check(!runtime.PreparedShotgunResources()&&
        !g43Life.Current()&&runtime.EngineEntitiesReleased(epoch));g43Actions.Reset();
    edict_t refused;InitEdict(refused);Check(!CreateShotgun(refused.v));
    if(shared)CheckShotgunSharedLongGuns(runtime);
    // Reentrant map retirement must retain borrowed storage until the real
    // effect returns, then allow its final entity-release acknowledgement.
    context.epoch=runtime.LastIssuedMapEpoch()+1;Check(g43Actions.Install(g43Stage,context.epoch));
    Check(bool(runtime.BeginMap(ShotgunCallbacks()))&&runtime.PlayerSpawned(player,request,error));
    player.m_rgAmmo[9]=0;
    edict_t retiredEntity;InitEdict(retiredEntity);gun=CreateShotgun(retiredEntity.v);Check(gun);gun->Spawn();
    Check(gun->AddToPlayer(&player));gun->AttachToPlayer(&player);
    player.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]=gun;player.m_pActiveItem=gun;
    globals.time=6000;player.pev->button=0;Check(gun->Deploy());
    command(0);Check(runtime.EndPostThink(player));command(IN_ATTACK);
    retireShotgunEffect=true;context.releaseWhileBorrowed=false;gun->PrimaryAttack();retireShotgunEffect=false;
    Check(!runtime.MapEpoch()&&!context.releaseWhileBorrowed&&!g43Life.Current());
    player.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]=nullptr;player.m_pActiveItem=nullptr;
    DestroyP345();retiredEntity.pvPrivateData=nullptr;
    Check(runtime.EngineEntitiesReleased(context.epoch));g43Actions.Reset();
    std::cout<<"870 map/owner/factory/resources/shared actions: "<<checks-before<<" checks; render and damage endpoints controlled\n";
}
}
