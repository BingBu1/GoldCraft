// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "guest_cof_runtime.hpp"
#include "guest_cof_factory.hpp"
#include "guest_cof_native_effects.hpp"
#include "guest_cof_p345_native_effects.hpp"
#include "guest_cof_p345_factory.hpp"
#include "guest_cof_melee.hpp"
#include "native/guest/cof/catalog.hpp"
#include <limits>
#include <vector>
#include <exception>

namespace csol::server::cof
{
using namespace guest::cof::server;
struct GlockRuntime::State
{
    struct Map;
    struct Owner
    {
        Map* map{};
        CBasePlayer* player{};
        OwnerBackendView view{};
        std::unique_ptr<P345OwnerView> p345;
        P345OwnerBackendView p345View{};
    };
    struct Player
    {
        CBasePlayer* object{};
        int serial{};
        std::uint64_t life{}, model{};
        Owner* backend{};
        bool ready{};
    };
    struct Map
    {
        State* state;
        std::uint64_t epoch;
        GlockRuntimeHost host;
        GlockResources resources{};
        P345Resources p345Resources{};
        GlockOwnerRuntime owners;
        std::array<Player, GlockOwnerRuntime::SlotCount> players{};
        std::vector<std::unique_ptr<Owner>> bindings;
        unsigned calls{};
        bool active{}, prepared{}, p345Prepared{};
    };
    struct Call
    {
        Map& map;
        explicit Call(Map& value) noexcept : map(value) { ++map.calls; }
        ~Call() { --map.calls; }
    };
    GlockRuntimeLimits limits;
    std::vector<std::unique_ptr<Map>> maps;
    Map* active{};
    std::uint64_t generation{};

    static unsigned Slot(CBasePlayer& player) noexcept
    {
        if (!player.pev || !g_engfuncs.pfnIndexOfEdict) return 0;
        const int index = ENTINDEX(player.edict());
        return index > 0 && index < static_cast<int>(GlockOwnerRuntime::SlotCount) ? unsigned(index) : 0;
    }
    static bool Local(const Map& map) noexcept
    { return map.active && map.state->active == &map && map.epoch; }
    static bool Current(void* opaque) noexcept
    {
        auto& map = *static_cast<Map*>(opaque);
        Call call(map);
        return Local(map) && map.host.current &&
            map.host.current(map.host.context, map.epoch) && Local(map);
    }
    static bool PlayerCurrent(void* opaque, CBasePlayer& player,
        std::uint64_t epoch, std::uint64_t life, std::uint64_t model,
        const NativeOwnerMode& mode) noexcept
    {
        auto& map = *static_cast<Map*>(opaque);
        Call call(map);
        const auto index = Slot(player);
        if (!index || !Current(&map) || epoch != map.epoch) return false;
        const auto matches = [&] {
            const auto& p = map.players[index];
            auto* entity = player.edict();
            return Local(map) && p.ready && p.object == &player && p.life == life && p.model == model &&
                entity && !entity->free && entity->serialnumber == p.serial && entity->pvPrivateData == &player;
        };
        return matches() && map.host.ownerCurrent &&
            map.host.ownerCurrent(map.host.context, player, mode) && matches() && Current(&map);
    }
    static bool OwnerCurrent(void* opaque) noexcept
    {
        auto& owner = *static_cast<Owner*>(opaque);
        Call call(*owner.map);
        return Current(owner.map) && owner.map->owners.Current(owner.view.binding) && Current(owner.map);
    }
    static bool Frame(void* opaque, GlockOwnerFrame& output) noexcept
    {
        auto& owner = *static_cast<Owner*>(opaque);
        Call call(*owner.map);
        return GlockCurrent(&owner) && owner.map->owners.Read(owner.view.binding, output);
    }
    static bool WeaponCurrent(Owner& owner,int id) noexcept
    {
        Call call(*owner.map);
        if(!OwnerCurrent(&owner))return false;
        const auto& host=owner.map->host;
        return (!host.weaponCurrent||host.weaponCurrent(host.context,*owner.player,id))&&OwnerCurrent(&owner);
    }
    static bool GlockCurrent(void* opaque) noexcept
    {return WeaponCurrent(*static_cast<Owner*>(opaque),guest::cof::Pistols[0].hostId);}
    static bool P345Current(void* opaque) noexcept
    {
        auto& owner=*static_cast<Owner*>(opaque);
        Call call(*owner.map);
        return WeaponCurrent(owner,guest::cof::Pistols[1].hostId)&&owner.p345&&
            owner.p345->Current(owner.p345View.binding)&&OwnerCurrent(&owner);
    }
    static bool P345Frame(void* opaque,P345OwnerFrame& output) noexcept
    {
        auto& owner=*static_cast<Owner*>(opaque);
        Call call(*owner.map);
        return P345Current(&owner)&&owner.p345->Read(owner.p345View.binding,output)&&P345Current(&owner);
    }
    struct EffectLease
    {
        Owner& owner;
        CBasePlayerWeapon& weapon;
        edict_t* entity;
        int serial;
        bool p345{};
    };
    static bool EffectCurrent(void* opaque) noexcept
    {
        auto& lease = *static_cast<EffectLease*>(opaque);
        return (lease.p345?P345Current(&lease.owner):GlockCurrent(&lease.owner)) && lease.entity && !lease.entity->free &&
            lease.entity->serialnumber == lease.serial && lease.entity->pvPrivateData == &lease.weapon &&
            lease.weapon.pev == &lease.entity->v && lease.weapon.m_pPlayer == lease.owner.player;
    }
    static bool P345Effect(void* opaque,CBasePlayer& player,CBasePlayerWeapon& weapon,
        const P345Event& event) noexcept
    {
        auto& owner=*static_cast<Owner*>(opaque);auto& map=*owner.map;Call call(map);
        if(owner.player!=&player||!weapon.pev)return false;
        auto* entity=weapon.edict();if(!entity)return false;
        EffectLease lease{owner,weapon,entity,entity->serialnumber,true};
        if(!EffectCurrent(&lease)||static_cast<unsigned>(event.kind)>=static_cast<unsigned>(guest::cof::server::P345Effect::Count))return false;
        const auto bit=1u<<static_cast<unsigned>(event.kind);
        if(bit&P345NativeSoundEffects)
        {
            P345OwnerFrame frame{};if(!P345Frame(&owner,frame))return false;
            const NativeSoundAuthority authority{&lease,EffectCurrent,map.p345Resources.generation,
                frame.quietAudio,map.host.pickupSuppressed};
            return NativeSoundSucceeded(EmitP345NativeSound(authority,map.p345Resources,player,weapon,event))&&EffectCurrent(&lease);
        }
        return map.host.p345.effect&&(bit&map.host.p345.effects)&&map.host.p345.effect(map.host.context,
            map.p345Resources,owner.view.binding,player,weapon,event)&&EffectCurrent(&lease);
    }
    static bool Effect(void* opaque, CBasePlayer& player, CBasePlayerWeapon& weapon,
        const GlockEvent& event) noexcept
    {
        auto& owner = *static_cast<Owner*>(opaque);
        auto& map = *owner.map;
        Call call(map);
        if (owner.player != &player || !weapon.pev) return false;
        auto* entity = weapon.edict();
        if (!entity) return false;
        EffectLease lease{owner, weapon, entity, entity->serialnumber};
        if (!EffectCurrent(&lease)) return false;
        if (static_cast<unsigned>(event.kind) >= static_cast<unsigned>(GlockEffect::Count)) return false;
        const auto bit = 1u << static_cast<unsigned>(event.kind);
        if (bit & GlockNativeSoundEffects)
        {
            GlockOwnerFrame frame{};
            if (!Frame(&owner, frame)) return false;
            const NativeSoundAuthority authority{&lease, EffectCurrent, map.resources.generation,
                frame.quietAudio, map.host.pickupSuppressed};
            return NativeSoundSucceeded(EmitGlockNativeSound(authority, map.resources, player, weapon, event)) &&
                EffectCurrent(&lease);
        }
        return map.host.effect && (bit & map.host.effects) &&
            map.host.effect(map.host.context, map.resources, owner.view.binding, player, weapon, event) &&
            EffectCurrent(&lease);
    }
    static bool Prepare(void* opaque, GlockWeapon&, GlockPrepared& output) noexcept
    {
        auto& map = *static_cast<Map*>(opaque);
        Call call(map);
        if (!Current(&map) || !map.prepared || !map.host.prepare) return false;
        GlockPrepared prepared{};
        if (!map.host.prepare(map.host.context, map.resources, prepared) || !Current(&map)) return false;
        const auto& resources = map.resources;
        prepared.sequences = resources.sequences;
        prepared.aimOffset = resources.aimOffset;
        prepared.aimZoom = resources.aimZoom;
        prepared.viewModelString = resources.viewModelString;
        prepared.playerModelString = resources.playerModelString;
        prepared.worldModelString = resources.worldModelString;
        prepared.inventoryItemString = resources.inventoryItemString;
        prepared.shellModel = resources.models[static_cast<unsigned>(GlockModel::Shell)];
        prepared.ballistics.bubbleModel = resources.models[static_cast<unsigned>(GlockModel::Bubble)];
        prepared.modelsReady = prepared.soundsReady = prepared.aimMetadataReady = true;
        prepared.pickupSoundReady = prepared.taclightSoundsReady = true;
        prepared.inventoryReady = true; // Actual NativeCSInventory gate below.
        prepared.melee.soundsReady = true;
        prepared.melee.samples = {
            GlockSounds[static_cast<unsigned>(GlockSound::Swing1)].host,
            GlockSounds[static_cast<unsigned>(GlockSound::Swing2)].host,
            GlockSounds[static_cast<unsigned>(GlockSound::Swing3)].host,
            GlockSounds[static_cast<unsigned>(GlockSound::Swing4)].host,
            GlockSounds[static_cast<unsigned>(GlockSound::Swing5)].host,
            GlockSounds[static_cast<unsigned>(GlockSound::Swing6)].host,
            GlockSounds[static_cast<unsigned>(GlockSound::PunchSwing)].host,
            GlockSounds[static_cast<unsigned>(GlockSound::PunchSmack)].host};
        prepared.hostSlot = guest::cof::GlockHudSlot;
        prepared.hostPosition = guest::cof::GlockHudPosition;
        output = prepared;
        return true;
    }
    static bool Backends(void* opaque, CBasePlayer& player, GlockBackends& output) noexcept
    {
        auto& map = *static_cast<Map*>(opaque);
        Call call(map);
        const auto index = Slot(player);
        if (!index || !Current(&map)) return false;
        auto* owner = map.players[index].backend;
        if (!owner || owner->player != &player || !GlockCurrent(owner)) return false;
        output = {owner, GlockCurrent, Frame, Effect, map.host.effects | GlockNativeSoundEffects,
            owner->view.animation, owner->view.animationGeneration};
        return true;
    }
    static bool PrepareP345(void* opaque,P345Weapon&,P345Prepared& output) noexcept
    {
        auto& map=*static_cast<Map*>(opaque);Call call(map);
        if(!Current(&map)||!map.p345Prepared||!map.host.p345.prepare)return false;
        P345Prepared prepared{};
        if(!map.host.p345.prepare(map.host.context,map.p345Resources,prepared)||!Current(&map))return false;
        const auto& resources=map.p345Resources;
        prepared.sequences=resources.sequences;prepared.aimOffset=resources.aimOffset;prepared.aimZoom=resources.aimZoom;
        prepared.viewModelString=resources.viewModelString;prepared.playerModelString=resources.playerModelString;
        prepared.worldModelString=resources.worldModelString;prepared.inventoryItemString=resources.inventoryItemString;
        prepared.shellModel=resources.models[static_cast<unsigned>(P345Model::Shell)];
        prepared.ballistics.bubbleModel=resources.models[static_cast<unsigned>(P345Model::Bubble)];
        prepared.modelsReady=prepared.soundsReady=prepared.aimMetadataReady=prepared.pickupSoundReady=prepared.inventoryReady=true;
        prepared.melee.soundsReady=true;prepared.melee.samples=P345MeleeSamples;
        prepared.hostSlot=guest::cof::P345HudSlot;prepared.hostPosition=guest::cof::P345HudPosition;
        output=prepared;return true;
    }
    static bool P345BackendsFor(void* opaque,CBasePlayer& player,P345Backends& output) noexcept
    {
        auto& map=*static_cast<Map*>(opaque);Call call(map);
        const auto index=Slot(player);if(!index||!Current(&map)||!map.p345Prepared)return false;
        auto* owner=map.players[index].backend;
        if(!owner||owner->player!=&player||!P345Current(owner))return false;
        output={owner,P345Current,P345Frame,P345Effect,map.host.p345.effects|P345NativeSoundEffects,
            owner->p345View.animation,owner->p345View.animationGeneration};
        return true;
    }
    static bool P345InventoryPickup(void* opaque,CBasePlayer& player,P345Weapon& weapon,int itemString) noexcept
    {
        auto& map=*static_cast<Map*>(opaque);Call call(map);P345Backends backend{};
        // Same explicit NativeCSInventory policy as Glock: real CanHave already
        // ran in WorldTouch; actual AddPlayerItem performs the insertion next.
        return itemString>0&&itemString==map.p345Resources.inventoryItemString&&weapon.ResourcesReady()&&
            P345BackendsFor(&map,player,backend)&&Current(&map);
    }
    static bool InventoryPickup(void* opaque, CBasePlayer& player, GlockWeapon& weapon,
        int itemString) noexcept
    {
        auto& map = *static_cast<Map*>(opaque);
        Call call(map);
        GlockBackends backend{};
        // Explicit NativeCSInventory adaptation: WorldTouch already performed
        // real CanHavePlayerItem; AddPlayerItem performs actual insertion next.
        // This gate authenticates the resource and prepared player, not an
        // invented CoF backpack capacity or a second insertion.
        return itemString == map.resources.inventoryItemString && itemString > 0 &&
            weapon.ResourcesReady() && Backends(&map, player, backend) && Current(&map);
    }
    void Invalidate(CBasePlayer& player) noexcept
    {
        if (!active) return;
        const auto index = Slot(player);
        if (!index || active->players[index].object != &player) return;
        if(auto* owner=active->players[index].backend;owner&&owner->p345)owner->p345->Invalidate();
        active->players[index].ready = false;
        active->players[index].backend = nullptr;
        active->owners.Invalidate(index, &player);
    }
    bool PreparePlayer(CBasePlayer& player, const GlockPlayerPreparation& request,
        bool spawn, std::string& error)
    {
        auto* map = active;
        const auto index = Slot(player);
        if (!map || !index) { error = "No current Glock map/native slot"; return false; }
        Call call(*map);
        auto& p = map->players[index];
        const bool same = p.object == &player && player.edict()->serialnumber == p.serial;
        const auto oldLife = p.life;
        Invalidate(player);
        if (!Current(map) || map->bindings.size() >= limits.ownerBindingsPerMap ||
            p.model == std::numeric_limits<std::uint64_t>::max() ||
            ((spawn || !same) && oldLife == std::numeric_limits<std::uint64_t>::max()))
        { error = "Glock map authority or retained binding budget exhausted"; return false; }
        p.object = &player; p.serial = player.edict()->serialnumber;
        p.life = (spawn || !same) ? oldLife + 1 : oldLife;
        ++p.model;
        p.ready = true;
        OwnerBackendView view{};
        const OwnerPreparation prepare{index, map->epoch, p.life, p.model, request.mode,
            request.rules, request.persistent, {map, PlayerCurrent}, request.rig,
            request.validatedModel, request.compare, request.extent};
        if (!map->owners.Prepare(player, prepare, view, error) || !Current(map))
        { p.ready = false; map->owners.Invalidate(index, &player); return false; }
        try
        {
            auto owner=std::make_unique<Owner>();owner->map=map;owner->player=&player;owner->view=view;
            if(request.p345Rules)
            {
                if(!map->p345Prepared){p.ready=false;map->owners.Invalidate(index,&player);error="P345 map resources were not opted in";return false;}
                owner->p345=std::make_unique<P345OwnerView>();
                if(!owner->p345->Prepare(player,view,*request.p345Rules,request.rig,request.validatedModel,
                    request.compare,owner->p345View,error,request.extent)||!Current(map))
                {p.ready=false;map->owners.Invalidate(index,&player);return false;}
            }
            p.backend = owner.get();
            map->bindings.push_back(std::move(owner));
        }
        catch (...) { p.ready = false; p.backend = nullptr; map->owners.Invalidate(index, &player); throw; }
        return true;
    }
};

GlockRuntime::GlockRuntime(GlockRuntimeLimits limits) : state_(std::make_unique<State>())
{ state_->limits = limits; state_->maps.reserve(limits.retainedMaps); }
GlockRuntime::~GlockRuntime()
{
    // Silently deleting still-referenced map contexts creates a callback UAF.
    if (!state_->maps.empty()) std::terminate();
}
ResourceResult GlockRuntime::BeginMap(const GlockRuntimeHost& host) noexcept
{
    EndMap();
    auto& state = *state_;
    const bool p345Requested=host.p345.prepare||host.p345.effect||host.p345.effects;
    if (!host.current || !host.ownerCurrent || !host.prepare || !host.effect ||
        (host.effects & ~GlockRequiredEffects) ||
        !state.limits.ownerBindingsPerMap || state.maps.size() >= state.limits.retainedMaps ||
        state.generation == std::numeric_limits<std::uint64_t>::max()||
        (p345Requested&&(!host.p345.prepare||!host.p345.effect||
            (host.p345.effects&~P345RequiredEffects)||
            ((host.p345.effects|P345NativeSoundEffects)&P345RequiredEffects)!=P345RequiredEffects)))
        return {ResourceStatus::InvalidAuthority};
    try
    {
        auto map = std::make_unique<State::Map>();
        map->state = &state; map->epoch = ++state.generation; map->host = host;
        map->bindings.reserve(state.limits.ownerBindingsPerMap);
        state.maps.push_back(std::move(map));
        state.active = state.maps.back().get(); state.active->active = true;
        auto& current = *state.active;
        State::Call call(current);
        if (!current.owners.BeginMap(current.epoch)) { EndMap(); return {ResourceStatus::InvalidAuthority}; }
        const auto result = PrepareGlockResources({&current, State::Current, current.epoch}, current.resources);
        if (!result || !State::Current(&current)) { EndMap(); return result ? ResourceResult{ResourceStatus::Cancelled} : result; }
        current.prepared = true;
        if (!InstallGlockFactory({&current, State::Current, State::Prepare, State::Backends, State::InventoryPickup}))
        { EndMap(); return {ResourceStatus::InvalidAuthority}; }
        if(p345Requested)
        {
            const auto p345=PrepareP345Resources({&current,State::Current,current.epoch},current.p345Resources);
            if(!p345||!State::Current(&current))
            {EndMap();return p345?ResourceResult{ResourceStatus::Cancelled}:p345;}
            current.p345Prepared=true;
            if(!InstallP345Factory({&current,State::Current,State::PrepareP345,State::P345BackendsFor,State::P345InventoryPickup}))
            {EndMap();return {ResourceStatus::InvalidAuthority};}
        }
        return result;
    }
    catch (...) { EndMap(); return {ResourceStatus::EngineRejected}; }
}
void GlockRuntime::EndMap() noexcept
{
    auto& state = *state_;
    if (!state.active) return;
    auto* map = state.active;
    state.active = nullptr; map->active = map->prepared = map->p345Prepared = false;
    ResetGlockFactory();
    ResetP345Factory();
    for(auto& owner:map->bindings)if(owner->p345)owner->p345->Invalidate();
    map->owners.EndMap();
    for (auto& player : map->players) { player.ready = false; player.backend = nullptr; }
}
bool GlockRuntime::EngineEntitiesReleased(std::uint64_t epoch) noexcept
{
    auto& maps = state_->maps;
    for (auto it = maps.begin(); it != maps.end(); ++it)
        if ((*it)->epoch == epoch)
        {
            if ((*it)->active || (*it)->calls) return false;
            maps.erase(it);
            return true;
        }
    return false;
}
std::uint64_t GlockRuntime::MapEpoch() const noexcept { return state_->active ? state_->active->epoch : 0; }
std::uint64_t GlockRuntime::LastIssuedMapEpoch() const noexcept { return state_->generation; }
const GlockResources* GlockRuntime::Resources() const noexcept
{ return state_->active && state_->active->prepared ? &state_->active->resources : nullptr; }
const P345Resources* GlockRuntime::PreparedP345Resources() const noexcept
{return state_->active&&state_->active->p345Prepared?&state_->active->p345Resources:nullptr;}
bool GlockRuntime::PlayerSpawned(CBasePlayer& player, const GlockPlayerPreparation& request, std::string& error)
{ return state_->PreparePlayer(player, request, true, error); }
bool GlockRuntime::PlayerModelChanged(CBasePlayer& player, const GlockPlayerPreparation& request, std::string& error)
{ return state_->PreparePlayer(player, request, false, error); }
void GlockRuntime::PlayerDied(CBasePlayer& player) noexcept { state_->Invalidate(player); }
void GlockRuntime::PlayerDisconnected(CBasePlayer& player) noexcept { state_->Invalidate(player); }
bool GlockRuntime::Backend(CBasePlayer& player, GlockBackends& output) noexcept
{ return state_->active && State::Backends(state_->active, player, output); }
bool GlockRuntime::Backend(CBasePlayer& player,P345Backends& output) noexcept
{return state_->active&&State::P345BackendsFor(state_->active,player,output);}
bool GlockRuntime::FirstDraw(const OwnerBinding& binding, bool value) noexcept
{ return state_->active && state_->active->owners.FirstDraw(binding, value); }
bool GlockRuntime::Light(const OwnerBinding& binding, bool value) noexcept
{ return state_->active && state_->active->owners.Light(binding, value); }
bool GlockRuntime::PersistentState(CBasePlayer& player, NativeOwnerPersistentState& output) noexcept
{
    GlockBackends backend{};
    if (!Backend(player, backend)) return false;
    auto& owner = *static_cast<State::Owner*>(backend.context);
    return owner.map->owners.PersistentState(owner.view.binding, output);
}
#define COF_STAGE(Name) \
bool GlockRuntime::Name(CBasePlayer& player) noexcept \
{ return state_->active && state_->active->owners.Name(State::Slot(player), player); }
COF_STAGE(BeginPreThink)
COF_STAGE(BeginInput)
COF_STAGE(AcceptedJump)
COF_STAGE(AfterMovement)
COF_STAGE(EndPostThink)
COF_STAGE(CancelFrame)
#undef COF_STAGE
}
