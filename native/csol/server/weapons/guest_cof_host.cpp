// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "guest_cof_host.hpp"
#include "guest_cof_effects.hpp"
#include "guest_cof_p345_native_effects.hpp"
#include "guest_cof_revolver_native_effects.hpp"
#include "guest_cof_vp70_native_effects.hpp"
#include "native/guest/cof/first_draw_lifecycle.hpp"
#include "guest_cof_sound_field.hpp"
#include "primary_purchase.h"
#include "../characters.h"
#include "../scenario/core/game_rules_gate.h"
#include "native/guest/cof/catalog.hpp"
#include "native/guest/cof/admission.hpp"
#include "native/guest/cof/scenario_damage.hpp"
#include "native/server/bridge.h"
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

namespace
{
using namespace csol::server::cof;
using namespace guest::cof;
using namespace guest::cof::server;
constexpr char DamagePath[] = "configs/guest/cof/scenario_damage.json";
constexpr auto GlockId = Pistols[0].hostId;
constexpr auto P345Id = Pistols[1].hostId;
constexpr auto RevolverId = Pistols[2].hostId;
constexpr auto VP70Id = Pistols[3].hostId;
constexpr std::uint32_t EffectBit(GlockEffect kind) { return 1u << static_cast<unsigned>(kind); }
constexpr std::uint32_t HostEffects =
    EffectBit(GlockEffect::DeployPrelude) | EffectBit(GlockEffect::ReserveHud) |
    EffectBit(GlockEffect::InventoryChanged) | EffectBit(GlockEffect::ViewAnimation) |
    EffectBit(GlockEffect::Gunsmoke) | EffectBit(GlockEffect::FireEvent) |
    EffectBit(GlockEffect::Recoil) | EffectBit(GlockEffect::Shake) | EffectBit(GlockEffect::Aim) |
    EffectBit(GlockEffect::FireSound) | EffectBit(GlockEffect::ShotState) |
    EffectBit(GlockEffect::ShotLatch) | EffectBit(GlockEffect::DeployReset) |
    EffectBit(GlockEffect::Body) | EffectBit(GlockEffect::FirstDraw) | EffectBit(GlockEffect::Tutorial);
struct Host;
struct Map
{
    struct Player
    {
        CBasePlayer* object{};
        int serial{}, userid{}, modelIndex{}, modelString{};
        std::uint64_t revision{};
        NativeOwnerPersistentState persistent{};
        RevolverLifecycle revolver;
        FirstDrawLifecycle vp70FirstDraw;
        bool persistentCaptured{};
        bool capable{}, prepared{}, spawning{};
        std::uint32_t weapons{};
    };
    Map(Host& owner, std::uint64_t generation, CGameRules* gameRules) noexcept
        : host(owner), epoch(generation), rules(gameRules) {}
    Host& host;
    const std::uint64_t epoch;
    CGameRules* rules;
    GlockEffects effects;
    SoundField sounds;
    ScenarioDamageConfig damage;
    std::array<Player, GlockOwnerRuntime::SlotCount> players{};
    unsigned calls{};
    bool active{}, runtimeIssued{}, runtimeReleased{}, soundReleased{}, ready{};
};
struct Host
{
    // Bounded host retention budgets, not original CoF gameplay constants.
    // One old map may be retained until actual private-data cleanup completes.
    GlockRuntime runtime{{2, 4096}};
    std::vector<std::unique_ptr<Map>> maps;
    Map* active{};
};
Host host;
struct MapCall final
{
    Map& map;
    explicit MapCall(Map& value) noexcept : map(value) { ++map.calls; }
    ~MapCall() { --map.calls; }
};
unsigned Slot(CBasePlayer& player) noexcept
{
    if (!player.pev || !g_engfuncs.pfnIndexOfEdict) return 0;
    const int slot = ENTINDEX(player.edict());
    return slot > 0 && slot < static_cast<int>(GlockOwnerRuntime::SlotCount) ? unsigned(slot) : 0;
}
bool Current(void* context) noexcept
{
    const auto& map = *static_cast<Map*>(context);
    return map.active && map.host.active == &map && map.epoch && map.rules && map.rules == g_pGameRules;
}
bool Entity(CBasePlayer& player, const Map::Player& row) noexcept
{
    auto* entity = player.edict();
    return row.object == &player && entity && !entity->free && entity->serialnumber == row.serial &&
        entity->pvPrivateData == &player && player.pev == &entity->v && !player.has_disconnected;
}
RevolverConnection Connection(const Map& map,unsigned slot,CBasePlayer& player) noexcept
{
    const auto& row=map.players[slot];
    return {map.epoch,slot,row.serial,row.userid,reinterpret_cast<std::uintptr_t>(&player)};
}
bool PlayerAuthority(CBasePlayer& player,PlaySfxCall& output) noexcept
{
    if(GlockBackends backend{};host.runtime.Backend(player,backend))
    {output={backend.context,backend.current};return true;}
    if(P345Backends backend{};host.runtime.Backend(player,backend))
    {output={backend.context,backend.current};return true;}
    if(RevolverBackends backend{};host.runtime.Backend(player,backend))
    {output={backend.context,backend.current};return true;}
    if(VP70Backends backend{};host.runtime.Backend(player,backend))
    {output={backend.context,backend.current};return true;}
    return false;
}
bool Owner(void* context, CBasePlayer& player, const NativeOwnerMode& mode) noexcept
{
    auto& map = *static_cast<Map*>(context);
    const auto slot = Slot(player);
    return slot && Current(&map) && map.ready && map.effects.Current() && map.sounds.Current() &&
        map.players[slot].capable && Entity(player, map.players[slot]) &&
        player.m_iTeam == CT && player.pev->deadflag == DEAD_NO && player.pev->health > 0 &&
        player.pev->modelindex == map.players[slot].modelIndex &&
        static_cast<int>(player.pev->model) == map.players[slot].modelString &&
        mode.movement == OwnerMovement::NativeCounterStrike && mode.freeActivity &&
        mode.phoneUnavailable && mode.substanceFree && mode.finiteAmmo && mode.firstPerson &&
        !GoldCraft_MinecraftFallAuthority(player.edict()) && Current(&map);
}
bool Prepare(void* context, const GlockResources& resources, GlockPrepared& output) noexcept
{
    auto& map = *static_cast<Map*>(context);
    return Current(&map) && map.ready && map.effects.FillPrepared(resources, output);
}
bool PrepareP345(void* context,const P345Resources& resources,P345Prepared& output) noexcept
{
    auto& map=*static_cast<Map*>(context);
    return Current(&map)&&map.ready&&map.effects.FillPrepared(resources,output);
}
bool PrepareVP70(void* context,const VP70Resources& resources,VP70Prepared& output) noexcept
{
    auto& map=*static_cast<Map*>(context);
    return Current(&map)&&map.ready&&map.effects.FillPrepared(resources,output);
}
bool PrepareRevolver(void* context,const RevolverResources& resources,RevolverPrepared& output) noexcept
{
    auto& map=*static_cast<Map*>(context);
    if(!Current(&map)||!map.ready||!map.effects.FillPrepared(resources,output))return false;
    const auto* rules=map.damage.Find(Pistol::Revolver);
    if(!rules)return false;
    output.csPenetration=rules->penetration;
    return Current(&map);
}
bool Effect(void* context, const GlockResources& resources, const OwnerBinding& binding,
    CBasePlayer& player, CBasePlayerWeapon& weapon, const GlockEvent& event) noexcept
{
    auto& map = *static_cast<Map*>(context);
    if (!Current(&map) || !binding.runtime || !binding.runtime->Current(binding)) return false;
    if (event.kind == GlockEffect::ShotLatch)
    {
        const EHANDLE item(&weapon);
        struct Call { Map& map; const OwnerBinding binding; CBasePlayerWeapon& weapon; const EHANDLE& item; CBasePlayer& player; };
        Call state{map, binding, weapon, item, player};
        return map.sounds.ShotLatch(player, event.value, event.a, {&state, [](void* p) noexcept {
            auto& c = *static_cast<Call*>(p);
            return Current(&c.map) && c.binding.runtime->Current(c.binding) &&
                c.item.Get<CBasePlayerWeapon>() == &c.weapon && c.weapon.m_pPlayer == &c.player &&
                c.player.m_pActiveItem == &c.weapon;
        }});
    }
    return map.effects.Dispatch(resources, binding, player, weapon, event);
}
bool EffectP345(void* context,const P345Resources& resources,const OwnerBinding& binding,
    CBasePlayer& player,CBasePlayerWeapon& weapon,const P345Event& event) noexcept
{
    auto& map=*static_cast<Map*>(context);
    if(!Current(&map)||!binding.runtime||!binding.runtime->Current(binding))return false;
    if(event.kind==P345Effect::ShotLatch)
    {
        const EHANDLE item(&weapon);
        struct Call{Map& map;OwnerBinding binding;CBasePlayerWeapon& weapon;const EHANDLE& item;CBasePlayer& player;};
        Call state{map,binding,weapon,item,player};
        return map.sounds.ShotLatch(player,event.value,event.a,{&state,[](void* p) noexcept {
            auto& c=*static_cast<Call*>(p);
            return Current(&c.map)&&c.binding.runtime->Current(c.binding)&&
                c.item.Get<CBasePlayerWeapon>()==&c.weapon&&c.weapon.m_pPlayer==&c.player&&c.player.m_pActiveItem==&c.weapon;
        }});
    }
    return map.effects.Dispatch(resources,binding,player,weapon,event);
}
bool EffectVP70(void* context,const VP70Resources& resources,const OwnerBinding& binding,
    CBasePlayer& player,CBasePlayerWeapon& weapon,const VP70Event& event) noexcept
{
    auto& map=*static_cast<Map*>(context);MapCall call(map);
    const auto slot=Slot(player);
    if(!slot||!Current(&map)||!binding.runtime||!binding.runtime->Current(binding)||
        !Entity(player,map.players[slot]))return false;
    bool firstDrawn{};
    if(event.kind==VP70Effect::FirstDraw)
    {
        const auto key=Connection(map,slot,player);
        if(!host.runtime.PersistentVP70FirstDraw(player,firstDrawn)||!firstDrawn||!Current(&map)||
            !map.players[slot].vp70FirstDraw.MarkDrawn({key.map,key.slot,key.serial,key.userid,key.player}))return false;
    }
    if(event.kind==VP70Effect::ShotLatch)
    {
        const EHANDLE item(&weapon);
        struct Call{Map& map;OwnerBinding binding;CBasePlayerWeapon& weapon;const EHANDLE& item;CBasePlayer& player;};
        Call state{map,binding,weapon,item,player};
        return map.sounds.ShotLatch(player,event.value,event.a,{&state,[](void* p) noexcept {
            auto& c=*static_cast<Call*>(p);
            return Current(&c.map)&&c.binding.runtime->Current(c.binding)&&
                c.item.Get<CBasePlayerWeapon>()==&c.weapon&&c.weapon.m_pPlayer==&c.player&&c.player.m_pActiveItem==&c.weapon;
        }});
    }
    return map.effects.Dispatch(resources,binding,player,weapon,event,firstDrawn);
}
bool EffectRevolver(void* context,const RevolverResources& resources,const OwnerBinding& binding,
    CBasePlayer& player,CBasePlayerWeapon& weapon,const RevolverEvent& event) noexcept
{
    auto& map=*static_cast<Map*>(context);MapCall call(map);
    const auto slot=Slot(player);
    if(!slot||!Current(&map)||!binding.runtime||!binding.runtime->Current(binding)||
        !Entity(player,map.players[slot]))return false;
    RevolverOwnerState value{};
    if(!host.runtime.PersistentState(player,value)||!Current(&map))return false;
    if(event.kind==RevolverEffect::FirstDraw||event.kind==RevolverEffect::Cylinder||event.kind==RevolverEffect::ReloadStage)
    {
        if(!map.players[slot].revolver.Store(Connection(map,slot,player),value))return false;
    }
    if(event.kind==RevolverEffect::ShotLatch)
    {
        const EHANDLE item(&weapon);
        struct Call{Map& map;OwnerBinding binding;CBasePlayerWeapon& weapon;const EHANDLE& item;CBasePlayer& player;};
        Call state{map,binding,weapon,item,player};
        return map.sounds.ShotLatch(player,event.value,event.a,{&state,[](void* p) noexcept {
            auto& c=*static_cast<Call*>(p);
            return Current(&c.map)&&c.binding.runtime->Current(c.binding)&&
                c.item.Get<CBasePlayerWeapon>()==&c.weapon&&c.weapon.m_pPlayer==&c.player&&c.player.m_pActiveItem==&c.weapon;
        }});
    }
    return map.effects.Dispatch(resources,binding,player,weapon,event,value);
}
bool ReleaseRetired() noexcept
{
    for (auto it = host.maps.begin(); it != host.maps.end();)
    {
        auto& map = **it;
        if (map.active) { ++it; continue; }
        if (map.calls) return false;
        if (!map.soundReleased)
        {
            if (map.sounds.Generation() && !map.sounds.EngineEntitiesReleased(map.epoch)) return false;
            map.soundReleased = true;
        }
        if (!map.runtimeReleased)
        {
            if (map.runtimeIssued && !host.runtime.EngineEntitiesReleased(map.epoch)) return false;
            map.runtimeReleased = true;
        }
        it = host.maps.erase(it);
    }
    return true;
}
PistolAdmission Capability(CBasePlayer& player) noexcept
{
    if (!CSOL_CanUseExtendedWeapons(&player) || !g_engfuncs.pfnInfoKeyValue ||
        !g_engfuncs.pfnGetInfoKeyBuffer) return {};
    auto* info=GET_INFO_BUFFER(player.edict());if(!info)return {};
    const auto* protocol=GET_KEY_VALUE(info,"cof_protocol");
    if(!protocol||Q_strcmp(protocol,"1"))return {};
    const auto* weapons=GET_KEY_VALUE(info,"cof_weapons");
    return ParsePistolAdmission("1",weapons?weapons:"");
}
void PreparePlayer(CBasePlayer& player, bool spawn) noexcept
{
    auto* map = host.active;
    const auto slot = Slot(player);
    if (!map || !slot || !Current(map) || !map->ready) return;
    MapCall call(*map);
    auto& row = map->players[slot];
    if (row.spawning && !spawn) return;
    const EHANDLE owner(&player);
    const auto previousRevision = row.revision;
    NativeOwnerPersistentState persistent = !spawn && row.persistentCaptured ? row.persistent : NativeOwnerPersistentState{};
    if (!spawn && !row.persistentCaptured) host.runtime.PersistentState(player, persistent);
    const auto admission=Capability(player);
    const bool capable = admission.weapons!=0;
    if (!Current(map) || owner.Get<CBasePlayer>() != &player || row.revision != previousRevision) return;
    const int userid = GETPLAYERUSERID(player.edict());
    if (!Current(map) || owner.Get<CBasePlayer>() != &player || row.revision != previousRevision) return;
    if (!spawn && row.prepared && Entity(player, row) && row.userid == userid &&
        row.modelIndex == player.pev->modelindex && row.modelString == static_cast<int>(player.pev->model) &&
        row.capable == capable&&row.weapons==admission.weapons) return;
    if (row.revision == UINT64_MAX) return;
    // Capability changes retire the old authority just like a model change.
    // Capture while its old mask is still authoritative, including withdrawal.
    if (!spawn && row.prepared && Entity(player, row) && row.userid == userid &&
        player.pev->deadflag == DEAD_NO && player.pev->health > 0 && player.m_iTeam == CT)
    {
        host.runtime.PlayerModelChanging(player);
        if (!Current(map) || owner.Get<CBasePlayer>() != &player || row.revision != previousRevision) return;
    }
    const auto revision = ++row.revision;
    row.object = &player; row.serial = player.edict()->serialnumber; row.userid = userid;
    row.modelIndex = player.pev->modelindex; row.modelString = static_cast<int>(player.pev->model);
    row.capable = capable; row.prepared = false;
    row.weapons=admission.weapons;
    row.persistent = persistent; row.persistentCaptured = false;
    const auto current = [&] {
        return Current(map) && owner.Get<CBasePlayer>() == &player && row.revision == revision;
    };
    map->effects.RetirePlayer(player);
    if (!current()) return;
    if (!capable || player.pev->deadflag != DEAD_NO || player.pev->health <= 0 || player.m_iTeam != CT)
    { host.runtime.PlayerDisconnected(player); return; }
    try
    {
        auto* raw = static_cast<const std::byte*>(GET_MODEL_PTR(player.edict()));
        if (!current() || !raw) return;
        studiohdr_t header{};
        std::memcpy(static_cast<void*>(&header), raw, sizeof(header));
        if (header.id != 0x54534449 || header.version != 10 ||
            header.length < static_cast<int>(sizeof(header))) return;
        // Workspace ReHLDS PlanStudioStorage allocates either this validated
        // prefix or at least the entire logical file. Never span removed pixels.
        std::size_t bytes = header.length;
        auto extent = PlayerModelExtent::CompleteFile;
        if (header.numtextures > 0 && header.textureindex >= static_cast<int>(sizeof(header)) &&
            header.texturedataindex >= static_cast<int>(sizeof(header)) && header.texturedataindex < header.length)
        { bytes = header.texturedataindex; extent = PlayerModelExtent::ValidatedCachePrefix; }
        const auto* damage = map->damage.Find(Pistol::Glock);
        if (!damage || damage->shot != std::trunc(damage->shot) || damage->melee != std::trunc(damage->melee)) return;
        const NativeOwnerMode mode{OwnerMovement::NativeCounterStrike, true, true, true, true, true};
        const auto rig = CSOL_CharacterModel(&player) ? PistolAnimationRig::CSO : PistolAnimationRig::Stock;
        GlockPlayerPreparation request{mode, {true, int(damage->shot), int(damage->melee), 45},
            persistent, rig, {raw, bytes}, [](const char* a, const char* b) { return Q_stricmp(a, b); }, extent};
        if(admission.weapons&PistolCapability(Pistol::P345))
        {
            const auto* p345=map->damage.Find(Pistol::P345);
            if(!p345||p345->shot!=std::trunc(p345->shot)||p345->melee!=std::trunc(p345->melee))return;
            request.p345Rules=NativeOwnerRules{true,int(p345->shot),int(p345->melee),24};
        }
        if(admission.weapons&PistolCapability(Pistol::Revolver))
        {
            const auto* revolver=map->damage.Find(Pistol::Revolver);RevolverOwnerState seed{};
            if(!revolver||revolver->shot!=std::trunc(revolver->shot)||revolver->melee!=std::trunc(revolver->melee)||
                !row.revolver.Bind(Connection(*map,slot,player),spawn,seed))return;
            request.revolver=GlockPlayerPreparation::RevolverPreparation{{true,int(revolver->shot),int(revolver->melee),15},seed};
        }
        if(admission.weapons&PistolCapability(Pistol::VP70))
        {
            const auto* vp70=map->damage.Find(Pistol::VP70);bool firstDrawn{};
            const auto key=Connection(*map,slot,player);
            if(!vp70||vp70->shot!=std::trunc(vp70->shot)||vp70->melee!=std::trunc(vp70->melee)||
                !row.vp70FirstDraw.Bind({key.map,key.slot,key.serial,key.userid,key.player},firstDrawn))return;
            request.vp70=GlockPlayerPreparation::VP70Preparation{{true,int(vp70->shot),int(vp70->melee),54},firstDrawn};
        }
        std::string error;
        const bool prepared = spawn ? host.runtime.PlayerSpawned(player, request, error) :
            host.runtime.PlayerModelChanged(player, request, error);
        if (!current()) return;
        row.prepared = prepared;
        if(prepared)
        {
            int remaining=gpGlobals->maxEntities;
            for(auto* item=player.m_rgpPlayerItems[PISTOL_SLOT];item&&remaining-->0;)
            {
                const EHANDLE next(item->m_pNext);
                auto* glock=dynamic_cast<GlockWeapon*>(item);
                if(glock&&glock->m_pPlayer==&player&&glock->m_iId==GlockId)
                {
                    const bool rebound=glock->RestoreInventoryBinding(player);
                    if(!current())return;
                    if(!rebound){row.prepared=false;return;}
                }
                else if(auto* p345=dynamic_cast<P345Weapon*>(item);p345&&p345->m_pPlayer==&player&&p345->m_iId==P345Id)
                {
                    const bool rebound=p345->RestoreInventoryBinding(player);
                    if(!current())return;
                    if(!rebound){row.prepared=false;return;}
                }
                else if(auto* vp70=dynamic_cast<VP70Weapon*>(item);vp70&&vp70->m_pPlayer==&player&&vp70->m_iId==VP70Id)
                {
                    const bool rebound=vp70->RestoreInventoryBinding(player);
                    if(!current())return;
                    if(!rebound){row.prepared=false;return;}
                }
                else if(auto* revolver=dynamic_cast<RevolverWeapon*>(item);revolver&&revolver->m_pPlayer==&player&&revolver->m_iId==RevolverId)
                {
                    const bool rebound=revolver->RestoreInventoryBinding(player);
                    if(!current())return;
                    if(!rebound){row.prepared=false;return;}
                }
                item=next.Get<CBasePlayerItem>();
            }
            if(remaining<=0)row.prepared=false;
        }
        if (!prepared) ALERT(at_error, "[CoF] Glock owner preparation failed: %s\n", error.c_str());
    }
    catch (...) { if (current()) host.runtime.PlayerDisconnected(player); }
}
void Retire(CBasePlayer& player, bool disconnect, bool modelChanging = false) noexcept
{
    auto* map = host.active;
    const auto slot = Slot(player);
    if (!map || !slot || !Current(map)) return;
    MapCall call(*map);
    auto& row = map->players[slot];
    if (row.revision == UINT64_MAX) return;
    const auto revision = ++row.revision;
    row.prepared = false;
    const EHANDLE entity(&player);
    struct Call { Map& map; const EHANDLE& entity; CBasePlayer& player; Map::Player& row; std::uint64_t revision; };
    Call state{*map, entity, player, row, revision};
    const auto current = [](void* p) noexcept {
        auto& c = *static_cast<Call*>(p);
        return Current(&c.map) && c.entity.Get<CBasePlayer>() == &c.player && c.row.revision == c.revision;
    };
    map->effects.RetirePlayer(player);
    if (!current(&state)) return;
    map->sounds.ResetPlayer(player, {&state, current});
    if (!current(&state)) return;
    if (disconnect) host.runtime.PlayerDisconnected(player);
    else if (modelChanging) host.runtime.PlayerModelChanging(player);
    else host.runtime.PlayerDied(player);
    if (disconnect) { row = {}; row.revision = revision; }
}
}

void CSOL_EndGuestCofMap() noexcept
{
    auto* map = host.active;
    if (!map) return;
    host.active = nullptr; map->active = map->ready = false;
    map->effects.EndMap(); map->sounds.EndMap(); host.runtime.EndMap();
}
void CSOL_PrecacheGuestCof()
{
    CSOL_EndGuestCofMap();
    // Actual ClientPrecache runs in SV_SpawnServer after Host_ClearMemory and
    // old private-data callbacks; ServerDeactivate alone is never used here.
    if (!ReleaseRetired() || !CSOL_UsesScenarioRules() || !g_pGameRules ||
        !g_pGameRules->IsMultiplayer() || !g_engfuncs.pfnGetFileSize || GET_FILE_SIZE(DamagePath) <= 0) return;
    try
    {
        const auto epoch = host.runtime.LastIssuedMapEpoch();
        if (epoch == UINT64_MAX || host.maps.size() >= 2) return;
        auto map = std::make_unique<Map>(host, epoch + 1, g_pGameRules);
        int size{};
        const auto freeFile = [](byte* data) { FREE_FILE(data); };
        std::unique_ptr<byte, decltype(freeFile)> file(LOAD_FILE_FOR_ME(const_cast<char*>(DamagePath), &size), freeFile);
        if (!file) return;
        std::string error;
        const bool config = size > 0 && map->damage.Parse({reinterpret_cast<const char*>(file.get()), std::size_t(size)}, error);
        if (!config) { ALERT(at_error, "[CoF] Guest weapons disabled: %s\n", error.c_str()); return; }
        host.maps.push_back(std::move(map));
        host.active = host.maps.back().get(); host.active->active = true;
        auto& current = *host.active;
        MapCall call(current);
        GlockRuntimeHost callbacks{&current, [](void* p, std::uint64_t generation) noexcept {
            auto& value = *static_cast<Map*>(p); return generation == value.epoch && Current(p);
        }, Owner, Prepare, Effect, HostEffects, false};
        callbacks.p345={PrepareP345,EffectP345,P345RequiredEffects&~P345NativeSoundEffects};
        callbacks.vp70={PrepareVP70,EffectVP70,VP70RequiredEffects&~VP70NativeSoundEffects};
        callbacks.revolver={PrepareRevolver,EffectRevolver,RevolverRequiredEffects&~RevolverNativeSoundEffects};
        callbacks.weaponCurrent=[](void* p,CBasePlayer& player,int id) noexcept {
            auto& map=*static_cast<Map*>(p);const auto index=Slot(player);const auto* definition=FindPistol(id);
            return definition&&index&&Current(&map)&&Entity(player,map.players[index])&&
                (map.players[index].weapons&PistolCapability(definition->identity));
        };
        const auto prepared = host.runtime.BeginMap(callbacks);
        current.runtimeIssued = host.runtime.LastIssuedMapEpoch() == current.epoch;
        if (!prepared || !Current(&current))
        { CSOL_EndGuestCofMap(); ALERT(at_error, "[CoF] Glock resource preparation failed: %s\n", prepared.path ? prepared.path : "map authority"); return; }
        const auto* resources = host.runtime.Resources();
        const ResourceAuthority authority{&current, Current, current.epoch};
        const auto* revolverResources=host.runtime.PreparedRevolverResources();
        if (!resources || !revolverResources || !current.effects.Prepare(authority, *resources, {true, true, true}) ||
            !current.sounds.BeginMap(authority) ||
            ((current.effects.Capabilities() | EffectBit(GlockEffect::ShotLatch)) & HostEffects) != HostEffects||
            ((current.effects.P345Capabilities()|P345NativeSoundEffects|(1u<<unsigned(P345Effect::ShotLatch)))&P345RequiredEffects)!=P345RequiredEffects||
            ((current.effects.VP70Capabilities()|VP70NativeSoundEffects|(1u<<unsigned(VP70Effect::ShotLatch)))&VP70RequiredEffects)!=VP70RequiredEffects||
            ((current.effects.RevolverCapabilities(*revolverResources)|RevolverNativeSoundEffects|(1u<<unsigned(RevolverEffect::ShotLatch)))&RevolverRequiredEffects)!=RevolverRequiredEffects)
        { CSOL_EndGuestCofMap(); ALERT(at_error, "[CoF] Actual Glock consumers were not prepared\n"); return; }
        for(const auto& definition:Pistols)
        {
            const auto hud=PreparePistolPresentationResources(authority,definition.identity);
            if(!hud||!Current(&current))
            {CSOL_EndGuestCofMap();ALERT(at_error,"[CoF] Guest HUD resource preparation failed: %s\n",hud.path?hud.path:"map authority");return;}
        }
        current.ready = true;
        for(const auto& definition:Pistols)
        {
            UTIL_PrecacheOtherWeapon(definition.classname.data());
            if(!Current(&current))return;
            const auto* info=CSOL_ItemInfo(definition.hostId);
            if(!info||!info->pszName||Q_strcmp(info->pszName,definition.classname.data()))
            {CSOL_EndGuestCofMap();ALERT(at_error,"[CoF] Guest weapon registration failed: %s\n",definition.classname.data());return;}
        }
        PRECACHE_GENERIC(DamagePath);
    }
    catch (...) { CSOL_EndGuestCofMap(); ALERT(at_error, "[CoF] Guest weapon initialization failed\n"); }
}
void CSOL_ShutdownGuestCof() noexcept { CSOL_EndGuestCofMap(); ReleaseRetired(); }
void CSOL_GuestCofEntityRetiring(edict_t* entity) noexcept
{ for (auto& map : host.maps) map->sounds.EntityRetiring(entity); }
void CSOL_GuestCofSpawnStarted(CBasePlayer& player) noexcept
{
    auto* map = host.active;
    const auto slot = Slot(player);
    if (!map || !slot || !Current(map)) return;
    MapCall call(*map);
    const EHANDLE entity(&player);
    Retire(player, false);
    if (!Current(map) || entity.Get<CBasePlayer>() != &player) return;
    auto& row = map->players[slot];
    row.spawning = true; row.persistent = {}; row.persistentCaptured = false;
}
void CSOL_GuestCofSpawned(CBasePlayer& player) noexcept
{
    const auto slot = Slot(player);
    if (!host.active || !slot) return;
    host.active->players[slot].spawning = false;
    PreparePlayer(player, true);
}
void CSOL_GuestCofModelChanging(CBasePlayer& player) noexcept
{
    auto* map = host.active;
    const auto slot = Slot(player);
    if (!map || !slot || !Current(map) || map->players[slot].spawning) return;
    MapCall call(*map);
    auto& row = map->players[slot];
    const EHANDLE entity(&player);
    NativeOwnerPersistentState persistent{};
    const bool captured = host.runtime.PersistentState(player, persistent);
    if (!Current(map) || entity.Get<CBasePlayer>() != &player) return;
    if (captured) { row.persistent = persistent; row.persistentCaptured = true; }
    Retire(player, false, true);
}
void CSOL_GuestCofModelChanged(CBasePlayer& player) noexcept { PreparePlayer(player, false); }
void CSOL_GuestCofUserInfoChanged(CBasePlayer& player) noexcept { PreparePlayer(player, false); }
void CSOL_GuestCofDied(CBasePlayer& player) noexcept { Retire(player, false); }
void CSOL_GuestCofDisconnected(CBasePlayer& player) noexcept { Retire(player, true); }
void CSOL_GuestCofPreThink(CBasePlayer& player) noexcept
{
    if (!host.active) return;
    // A host hook can abort an earlier native frame. Discard only its phase,
    // never synthesize an original input/movement or persistent-state update.
    host.runtime.CancelFrame(player);
    host.runtime.BeginPreThink(player);
}
void CSOL_GuestCofInput(CBasePlayer& player) noexcept { if (host.active) host.runtime.BeginInput(player); }
void CSOL_GuestCofAcceptedJump(CBasePlayer& player) noexcept { if (host.active) host.runtime.AcceptedJump(player); }
void CSOL_GuestCofAfterMovement(CBasePlayer& player) noexcept { if (host.active) host.runtime.AfterMovement(player); }
void CSOL_GuestCofPostThink(CBasePlayer& player) noexcept
{
    auto* map = host.active;
    if (!map || !Current(map)) return;
    MapCall call(*map);
    if (!host.runtime.EndPostThink(player)) host.runtime.CancelFrame(player);
    if (!Current(map)) return;
    map->effects.SynchronizeSelection(player);
    if (!Current(map)) return;
    PlaySfxCall authority{};
    if(PlayerAuthority(player,authority))map->sounds.PostThink(player,{authority.context,authority.current});
}
void CSOL_GuestCofClientSnapshot(CBasePlayer& player) noexcept
{
    auto* map=host.active;
    if(!map||!Current(map))return;
    MapCall call(*map);
    PlaySfxCall authority{};
    if(PlayerAuthority(player,authority)&&Current(map))map->effects.CrosshairSnapshot(player,authority);
}
bool CSOL_GuestCofCanUse(CBasePlayer& player, int id) noexcept
{
    if ((id!=GlockId&&id!=P345Id&&id!=RevolverId&&id!=VP70Id) || !host.active || !Current(host.active)) return false;
    const auto slot = Slot(player);
    if (!slot || !host.active->players[slot].prepared) return false;
    if(id==P345Id){P345Backends backend{};return host.runtime.Backend(player,backend);}
    if(id==RevolverId){RevolverBackends backend{};return host.runtime.Backend(player,backend);}
    if(id==VP70Id){VP70Backends backend{};return host.runtime.Backend(player,backend);}
    GlockBackends backend{};return host.runtime.Backend(player,backend);
}
bool CSOL_GuestCofPreserveReload(CBasePlayer& player,int playerAnimation,int& desiredSequence) noexcept
{
    auto* map=host.active;
    if(!map||!Current(map)||!map->ready)return false;
    MapCall call(*map);
    return host.runtime.PreserveRevolverReload(player,playerAnimation,desiredSequence);
}
bool CSOL_IsGuestCof(const CBasePlayerWeapon& weapon) noexcept
{
    if(!weapon.pev)return false;
    return (weapon.m_iId==GlockId&&dynamic_cast<const GlockWeapon*>(&weapon)&&FClassnameIs(weapon.pev,Pistols[0].classname.data()))||
        (weapon.m_iId==P345Id&&dynamic_cast<const P345Weapon*>(&weapon)&&FClassnameIs(weapon.pev,Pistols[1].classname.data()))||
        (weapon.m_iId==RevolverId&&dynamic_cast<const RevolverWeapon*>(&weapon)&&FClassnameIs(weapon.pev,Pistols[2].classname.data()))||
        (weapon.m_iId==VP70Id&&dynamic_cast<const VP70Weapon*>(&weapon)&&FClassnameIs(weapon.pev,Pistols[3].classname.data()));
}
const char* CSOL_GuestCofWorldModel(const CBasePlayerItem& item) noexcept
{
    const auto* weapon = dynamic_cast<const CBasePlayerWeapon*>(&item);
    const auto* definition=weapon?FindPistol(weapon->m_iId):nullptr;
    return definition&&CSOL_IsGuestCof(*weapon)?definition->worldModel.data():nullptr;
}
bool CSOL_DeliverGuestCof(CBasePlayer& player,int id)
{
    const auto* definition=FindPistol(id);
    if(!definition || !CSOL_GuestCofCanUse(player,id))return false;
    const CSOLGunPurchaseAdapter adapter{id,PISTOL_SLOT,definition->classname.data(),CSOL_GuestCofCanUse,
        [](const CBasePlayerWeapon& weapon,int wanted) noexcept {
            return weapon.m_iId==wanted&&CSOL_IsGuestCof(weapon);
        },
        [](const CBasePlayerWeapon& weapon,int wanted) noexcept {
            if(weapon.m_iId!=wanted)return false;
            const EHANDLE item(const_cast<CBasePlayerWeapon*>(&weapon));
            auto* owner=weapon.m_pPlayer;const EHANDLE actor(owner);
            const bool identity=CSOL_IsGuestCof(weapon);
            if(!identity || item.Get<CBaseEntity>()!=&weapon || !owner || actor.Get<CBaseEntity>()!=owner)return false;
            const auto* glock=dynamic_cast<const GlockWeapon*>(&weapon);
            const auto* p345=dynamic_cast<const P345Weapon*>(&weapon);
            const auto* revolver=dynamic_cast<const RevolverWeapon*>(&weapon);
            const auto* vp70=dynamic_cast<const VP70Weapon*>(&weapon);
            return weapon.m_pPlayer==owner&&owner->m_pActiveItem==&weapon&&
                ((glock&&glock->Status()==GlockStatus::Applied)||(p345&&p345->Status()==P345Status::Applied)||
                    (revolver&&revolver->Status()==RevolverStatus::Applied)||(vp70&&vp70->Status()==VP70Status::Applied));
        },true};
    return CSOL_DeliverAdaptedGun(player,adapter);
}
