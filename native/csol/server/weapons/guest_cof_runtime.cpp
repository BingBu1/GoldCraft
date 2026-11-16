// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "guest_cof_runtime.hpp"
#include "guest_cof_factory.hpp"
#include "guest_cof_native_effects.hpp"
#include "guest_cof_p345_native_effects.hpp"
#include "guest_cof_p345_factory.hpp"
#include "guest_cof_revolver_factory.hpp"
#include "guest_cof_revolver_native_effects.hpp"
#include "guest_cof_vp70_native_effects.hpp"
#include "guest_cof_vp70_factory.hpp"
#include "guest_cof_tmp_factory.hpp"
#include "guest_cof_tmp_native_effects.hpp"
#include "guest_cof_mp5_factory.hpp"
#include "guest_cof_mp5_native_effects.hpp"
#include "guest_cof_m16_factory.hpp"
#include "guest_cof_m16_native_effects.hpp"
#include "guest_cof_g43_factory.hpp"
#include "guest_cof_g43_native_effects.hpp"
#include <type_traits>
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
        std::unique_ptr<RevolverOwnerView> revolver;
        RevolverOwnerBackendView revolverView{};
        std::unique_ptr<VP70OwnerView> vp70;
        VP70OwnerBackendView vp70View{};
        std::unique_ptr<TMPOwnerView> tmp;
        TMPOwnerBackendView tmpView{};
        std::unique_ptr<MP5OwnerView> mp5;
        MP5OwnerBackendView mp5View{};
        std::unique_ptr<M16OwnerView> m16;
        M16OwnerBackendView m16View{};
        std::unique_ptr<G43OwnerView> g43;
        G43OwnerBackendView g43View{};
    };
    struct Player
    {
        CBasePlayer* object{};
        int serial{};
        std::uint64_t life{}, model{};
        Owner* backend{};
        PlayerActionBinding actions{};
        bool ready{};
    };
    struct Map
    {
        State* state;
        std::uint64_t epoch;
        GlockRuntimeHost host;
        GlockResources resources{};
        P345Resources p345Resources{};
        RevolverResources revolverResources{};
        VP70Resources vp70Resources{};
        TMPResources tmpResources{};
        MP5Resources mp5Resources{};
        M16Resources m16Resources{};
        int m16SoundMessage{};
        G43Resources g43Resources{};
        int g43SoundMessage{};
        GlockOwnerRuntime owners;
        std::array<Player, GlockOwnerRuntime::SlotCount> players{};
        std::vector<std::unique_ptr<Owner>> bindings;
        unsigned calls{};
        bool active{}, prepared{}, p345Prepared{}, revolverPrepared{}, vp70Prepared{}, tmpPrepared{}, mp5Prepared{}, m16Prepared{}, g43Prepared{};
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
    static bool VP70Current(void* opaque) noexcept
    {
        auto& owner=*static_cast<Owner*>(opaque);
        Call call(*owner.map);
        return WeaponCurrent(owner,guest::cof::Pistols[3].hostId)&&owner.vp70&&
            owner.vp70->Current(owner.vp70View.binding)&&OwnerCurrent(&owner);
    }
    template<LongGun Weapon> static auto& AutomaticResources(Map& map) noexcept
    { if constexpr(Weapon==LongGun::TMP)return map.tmpResources;else return map.mp5Resources; }
    template<LongGun Weapon> static auto& AutomaticHost(Map& map) noexcept
    { if constexpr(Weapon==LongGun::TMP)return map.host.tmp;else return map.host.mp5; }
    template<LongGun Weapon> static auto& AutomaticOwner(Owner& owner) noexcept
    { if constexpr(Weapon==LongGun::TMP)return owner.tmp;else return owner.mp5; }
    template<LongGun Weapon> static auto& AutomaticView(Owner& owner) noexcept
    { if constexpr(Weapon==LongGun::TMP)return owner.tmpView;else return owner.mp5View; }
    template<LongGun Weapon> static bool AutomaticPrepared(const Map& map) noexcept
    { if constexpr(Weapon==LongGun::TMP)return map.tmpPrepared;else return map.mp5Prepared; }
    template<LongGun Weapon> static bool AutomaticCurrent(void* opaque) noexcept
    {
        auto& owner=*static_cast<Owner*>(opaque);Call call(*owner.map);
        auto& weaponOwner=AutomaticOwner<Weapon>(owner);
        return WeaponCurrent(owner,Weapon==LongGun::TMP?TMPHostId:MP5HostId)&&weaponOwner&&
            weaponOwner->Current(AutomaticView<Weapon>(owner).binding)&&OwnerCurrent(&owner);
    }
    static bool TMPCurrent(void* opaque) noexcept { return AutomaticCurrent<LongGun::TMP>(opaque); }
    static bool MP5Current(void* opaque) noexcept { return AutomaticCurrent<LongGun::MP5>(opaque); }
    static bool M16Current(void* opaque) noexcept
    {
        auto& owner = *static_cast<Owner*>(opaque); Call call(*owner.map);
        return WeaponCurrent(owner, M16HostId) && owner.m16 &&
            owner.m16->Current(owner.m16View.binding) && OwnerCurrent(&owner);
    }
    static bool M16Frame(void* opaque, M16OwnerFrame& output) noexcept
    {
        auto& owner = *static_cast<Owner*>(opaque); Call call(*owner.map);
        return M16Current(&owner) && owner.m16->Read(owner.m16View.binding, output) && M16Current(&owner);
    }

    static bool G43Current(void* opaque) noexcept
    {
        auto& owner = *static_cast<Owner*>(opaque); Call call(*owner.map);
        return WeaponCurrent(owner, G43HostId) && owner.g43 &&
            owner.g43->Current(owner.g43View.binding) && OwnerCurrent(&owner);
    }
    static bool G43Frame(void* opaque, G43OwnerFrame& output) noexcept
    {
        auto& owner = *static_cast<Owner*>(opaque); Call call(*owner.map);
        return G43Current(&owner) && owner.g43->Read(owner.g43View.binding, output) && G43Current(&owner);
    }

    static bool P345Frame(void* opaque,P345OwnerFrame& output) noexcept
    {
        auto& owner=*static_cast<Owner*>(opaque);
        Call call(*owner.map);
        return P345Current(&owner)&&owner.p345->Read(owner.p345View.binding,output)&&P345Current(&owner);
    }
    static bool VP70Frame(void* opaque,VP70OwnerFrame& output) noexcept
    {
        auto& owner=*static_cast<Owner*>(opaque);
        Call call(*owner.map);
        return VP70Current(&owner)&&owner.vp70->Read(owner.vp70View.binding,output)&&VP70Current(&owner);
    }
    template<LongGun Weapon> static bool AutomaticFrame(void* opaque,TMPOwnerFrame& output) noexcept
    {
        auto& owner=*static_cast<Owner*>(opaque);Call call(*owner.map);
        return AutomaticCurrent<Weapon>(&owner)&&AutomaticOwner<Weapon>(owner)->Read(
            AutomaticView<Weapon>(owner).binding,output)&&AutomaticCurrent<Weapon>(&owner);
    }

    static bool RevolverCurrent(void* opaque) noexcept
    {
        auto& owner=*static_cast<Owner*>(opaque);Call call(*owner.map);
        return WeaponCurrent(owner,guest::cof::Pistols[2].hostId)&&owner.revolver&&
            owner.revolver->Current(owner.revolverView.binding)&&OwnerCurrent(&owner);
    }
    static bool RevolverFrame(void* opaque,RevolverOwnerFrame& output) noexcept
    {
        auto& owner=*static_cast<Owner*>(opaque);Call call(*owner.map);
        return RevolverCurrent(&owner)&&owner.revolver->Read(owner.revolverView.binding,output)&&RevolverCurrent(&owner);
    }
    enum class EffectWeapon : unsigned char { Glock, P345, Revolver, VP70, TMP, MP5, M16, G43 };
    struct EffectLease
    {
        Owner& owner;
        CBasePlayerWeapon& weapon;
        edict_t* entity;
        int serial;
        EffectWeapon kind{EffectWeapon::Glock};
    };
    static bool EffectCurrent(void* opaque) noexcept
    {
        auto& lease = *static_cast<EffectLease*>(opaque);
        const bool current=lease.kind==EffectWeapon::Revolver?RevolverCurrent(&lease.owner):
            lease.kind==EffectWeapon::P345?P345Current(&lease.owner):
            lease.kind==EffectWeapon::VP70?VP70Current(&lease.owner):
            lease.kind==EffectWeapon::TMP?TMPCurrent(&lease.owner):
            lease.kind==EffectWeapon::MP5?MP5Current(&lease.owner):
            lease.kind==EffectWeapon::M16?M16Current(&lease.owner):
            lease.kind==EffectWeapon::G43?G43Current(&lease.owner):
            lease.kind==EffectWeapon::Glock&&GlockCurrent(&lease.owner);
        return current && lease.entity && !lease.entity->free &&
            lease.entity->serialnumber == lease.serial && lease.entity->pvPrivateData == &lease.weapon &&
            lease.weapon.pev == &lease.entity->v && lease.weapon.m_pPlayer == lease.owner.player;
    }
    static bool P345Effect(void* opaque,CBasePlayer& player,CBasePlayerWeapon& weapon,
        const P345Event& event) noexcept
    {
        auto& owner=*static_cast<Owner*>(opaque);auto& map=*owner.map;Call call(map);
        if(owner.player!=&player||!weapon.pev)return false;
        auto* entity=weapon.edict();if(!entity)return false;
        EffectLease lease{owner,weapon,entity,entity->serialnumber,EffectWeapon::P345};
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
    static constexpr auto M16SoundEffects = M16NativeSoundEffects | (1u << unsigned(M16Effect::FireSound));
    static bool M16EffectBoundary(void* opaque, CBasePlayer& player, CBasePlayerWeapon& weapon,
        const M16Event& event) noexcept
    {
        auto& owner = *static_cast<Owner*>(opaque); auto& map = *owner.map; Call call(map);
        if (owner.player != &player || !weapon.pev) return false;
        auto* entity = weapon.edict(); if (!entity) return false;
        EffectLease lease{owner, weapon, entity, entity->serialnumber, EffectWeapon::M16};
        if (!EffectCurrent(&lease) || static_cast<unsigned>(event.kind) >= static_cast<unsigned>(M16Effect::Count)) return false;
        const auto bit = 1u << static_cast<unsigned>(event.kind);
        if (bit & M16NativeSoundEffects)
        {
            M16OwnerFrame frame{}; if (!M16Frame(&owner, frame)) return false;
            const NativeSoundAuthority authority{&lease, EffectCurrent, map.m16Resources.generation,
                frame.quietAudio, map.host.pickupSuppressed};
            return NativeSoundSucceeded(EmitM16NativeSound(authority, map.m16Resources, player, weapon, event)) && EffectCurrent(&lease);
        }
        if (event.kind == M16Effect::FireSound)
        {
            if (event.value != 0 || event.other != 1 || !event.enabled) return false;
            const guest::cof::PlaySfxRequest request{M16LocalWaves[2].original, event.first, 0, 1, true, map.host.m16.soundRules};
            return guest::cof::SendPlaySfx(player, request, map.m16SoundMessage, {&lease, EffectCurrent}).status ==
                guest::cof::PlaySfxStatus::Sent && EffectCurrent(&lease);
        }
        return map.host.m16.effect && (bit & map.host.m16.effects) &&
            map.host.m16.effect(map.host.context, map.m16Resources, owner.view.binding, player, weapon, event) &&
            EffectCurrent(&lease);
    }
    static constexpr auto G43SoundEffects = G43NativeSoundEffects | (1u << unsigned(G43Effect::FireSound));
    static bool G43EffectBoundary(void* opaque, CBasePlayer& player, CBasePlayerWeapon& weapon,
        const G43Event& event) noexcept
    {
        auto& owner = *static_cast<Owner*>(opaque); auto& map = *owner.map; Call call(map);
        if (owner.player != &player || !weapon.pev) return false;
        auto* entity = weapon.edict(); if (!entity) return false;
        EffectLease lease{owner, weapon, entity, entity->serialnumber, EffectWeapon::G43};
        if (!EffectCurrent(&lease) || static_cast<unsigned>(event.kind) >= static_cast<unsigned>(G43Effect::Count)) return false;
        const auto bit = 1u << static_cast<unsigned>(event.kind);
        if (bit & G43NativeSoundEffects)
        {
            G43OwnerFrame frame{}; if (!G43Frame(&owner, frame)) return false;
            const NativeSoundAuthority authority{&lease, EffectCurrent, map.g43Resources.generation,
                frame.quietAudio, map.host.pickupSuppressed};
            return NativeSoundSucceeded(EmitG43NativeSound(authority, map.g43Resources, player, weapon, event)) && EffectCurrent(&lease);
        }
        if (event.kind == G43Effect::FireSound)
        {
            if (event.value != 0 || event.other != 1 || !event.enabled) return false;
            const guest::cof::PlaySfxRequest request{G43LocalWaves[0].original, event.first, 0, 1, true, map.host.g43.soundRules};
            return guest::cof::SendPlaySfx(player, request, map.g43SoundMessage, {&lease, EffectCurrent}).status ==
                guest::cof::PlaySfxStatus::Sent && EffectCurrent(&lease);
        }
        return map.host.g43.effect && (bit & map.host.g43.effects) &&
            map.host.g43.effect(map.host.context, map.g43Resources, owner.view.binding, player, weapon, event) &&
            EffectCurrent(&lease);
    }
    static bool VP70Effect(void* opaque,CBasePlayer& player,CBasePlayerWeapon& weapon,
        const VP70Event& event) noexcept
    {
        auto& owner=*static_cast<Owner*>(opaque);auto& map=*owner.map;Call call(map);
        if(owner.player!=&player||!weapon.pev)return false;
        auto* entity=weapon.edict();if(!entity)return false;
        EffectLease lease{owner,weapon,entity,entity->serialnumber,EffectWeapon::VP70};
        if(!EffectCurrent(&lease)||static_cast<unsigned>(event.kind)>=static_cast<unsigned>(guest::cof::server::VP70Effect::Count))return false;
        const auto bit=1u<<static_cast<unsigned>(event.kind);
        if(event.kind==VP70Effect::FirstDraw&&!owner.vp70->FirstDraw(owner.vp70View.binding))return false;
        if(bit&VP70NativeSoundEffects)
        {
            VP70OwnerFrame frame{};if(!VP70Frame(&owner,frame))return false;
            const NativeSoundAuthority authority{&lease,EffectCurrent,map.vp70Resources.generation,
                frame.quietAudio,map.host.pickupSuppressed};
            return NativeSoundSucceeded(EmitVP70NativeSound(authority,map.vp70Resources,player,weapon,event))&&EffectCurrent(&lease);
        }
        return map.host.vp70.effect&&(bit&map.host.vp70.effects)&&map.host.vp70.effect(map.host.context,
            map.vp70Resources,owner.view.binding,player,weapon,event)&&EffectCurrent(&lease);
    }
    template<LongGun Weapon> static bool AutomaticEffect(void* opaque,CBasePlayer& player,CBasePlayerWeapon& weapon,
        const TMPEvent& event) noexcept
    {
        auto& owner=*static_cast<Owner*>(opaque);auto& map=*owner.map;Call call(map);
        if(owner.player!=&player||!weapon.pev)return false;
        auto* entity=weapon.edict();if(!entity)return false;
        constexpr auto kind=Weapon==LongGun::TMP?EffectWeapon::TMP:EffectWeapon::MP5;
        EffectLease lease{owner,weapon,entity,entity->serialnumber,kind};
        if(!EffectCurrent(&lease)||static_cast<unsigned>(event.kind)>=static_cast<unsigned>(TMPEffect::Count))return false;
        const auto bit=1u<<static_cast<unsigned>(event.kind);
        if constexpr(Weapon==LongGun::TMP)
            if(event.kind==TMPEffect::FirstDraw&&!owner.tmp->FirstDraw(owner.tmpView.binding))return false;
        const auto& resources=AutomaticResources<Weapon>(map);
        if(bit&TMPNativeSoundEffects)
        {
            TMPOwnerFrame frame{};if(!AutomaticFrame<Weapon>(&owner,frame))return false;
            const NativeSoundAuthority authority{&lease,EffectCurrent,resources.generation,
                frame.quietAudio,map.host.pickupSuppressed};
            const auto emitted=[&] {
                if constexpr(Weapon==LongGun::TMP)return EmitTMPNativeSound(authority,resources,player,weapon,event);
                else return EmitMP5NativeSound(authority,resources,player,weapon,event);
            }();
            return NativeSoundSucceeded(emitted)&&EffectCurrent(&lease);
        }
        const auto& host=AutomaticHost<Weapon>(map);
        return host.effect&&(bit&host.effects)&&host.effect(map.host.context,
            resources,owner.view.binding,player,weapon,event)&&EffectCurrent(&lease);
    }

    static bool RevolverEffectBoundary(void* opaque,CBasePlayer& player,CBasePlayerWeapon& weapon,
        const RevolverEvent& event) noexcept
    {
        auto& owner=*static_cast<Owner*>(opaque);auto& map=*owner.map;Call call(map);
        if(owner.player!=&player||!weapon.pev)return false;
        auto* entity=weapon.edict();if(!entity)return false;
        EffectLease lease{owner,weapon,entity,entity->serialnumber,EffectWeapon::Revolver};
        if(!EffectCurrent(&lease)||static_cast<unsigned>(event.kind)>=static_cast<unsigned>(RevolverEffect::Count))return false;
        const auto bit=1u<<static_cast<unsigned>(event.kind);
        if(bit&RevolverNativeSoundEffects)
        {
            RevolverOwnerFrame frame{};if(!RevolverFrame(&owner,frame))return false;
            const NativeSoundAuthority authority{&lease,EffectCurrent,map.revolverResources.generation,
                frame.quietAudio,map.host.pickupSuppressed};
            return NativeSoundSucceeded(EmitRevolverNativeSound(authority,map.revolverResources,player,weapon,event))&&EffectCurrent(&lease);
        }
        if(event.kind==RevolverEffect::FirstDraw&&!owner.revolver->FirstDraw(owner.revolverView.binding,true))return false;
        if(event.kind==RevolverEffect::Cylinder&&!owner.revolver->Cylinder(owner.revolverView.binding,event))return false;
        if(event.kind==RevolverEffect::ReloadStage&&!owner.revolver->ReloadStage(owner.revolverView.binding,event))return false;
        return map.host.revolver.effect&&(bit&map.host.revolver.effects)&&map.host.revolver.effect(map.host.context,
            map.revolverResources,owner.view.binding,player,weapon,event)&&EffectCurrent(&lease);
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
    static bool PrepareVP70(void* opaque,VP70Weapon&,VP70Prepared& output) noexcept
    {
        auto& map=*static_cast<Map*>(opaque);Call call(map);
        if(!Current(&map)||!map.vp70Prepared||!map.host.vp70.prepare)return false;
        VP70Prepared prepared{};
        if(!map.host.vp70.prepare(map.host.context,map.vp70Resources,prepared)||!Current(&map))return false;
        const auto& resources=map.vp70Resources;
        prepared.sequences=resources.sequences;prepared.aimOffset=resources.aimOffset;prepared.aimZoom=resources.aimZoom;
        prepared.viewModelString=resources.viewModelString;prepared.playerModelString=resources.playerModelString;
        prepared.worldModelString=resources.worldModelString;prepared.inventoryItemString=resources.inventoryItemString;
        prepared.shellModel=resources.models[static_cast<unsigned>(VP70Model::Shell)];
        prepared.ballistics.bubbleModel=resources.models[static_cast<unsigned>(VP70Model::Bubble)];
        prepared.modelsReady=prepared.soundsReady=prepared.aimMetadataReady=prepared.pickupSoundReady=prepared.inventoryReady=true;
        prepared.melee.soundsReady=true;prepared.melee.samples=VP70MeleeSamples;
        prepared.hostSlot=VP70HudSlot;prepared.hostPosition=VP70HudPosition;
        output=prepared;return true;
    }
    template<LongGun Weapon> static bool PrepareAutomatic(void* opaque,AutomaticWeapon<Weapon>&,TMPPrepared& output) noexcept
    {
        auto& map=*static_cast<Map*>(opaque);Call call(map);
        const auto& host=AutomaticHost<Weapon>(map);
        if(!Current(&map)||!AutomaticPrepared<Weapon>(map)||!host.prepare)return false;
        TMPPrepared prepared{};const auto& resources=AutomaticResources<Weapon>(map);
        if(!host.prepare(map.host.context,resources,prepared)||!Current(&map))return false;
        using Model=std::conditional_t<Weapon==LongGun::TMP,TMPModel,MP5Model>;
        using Sound=std::conditional_t<Weapon==LongGun::TMP,TMPSound,MP5Sound>;
        prepared.sequences=resources.sequences;prepared.aimOffset=resources.aimOffset;prepared.aimZoom=resources.aimZoom;
        prepared.viewModelString=resources.viewModelString;prepared.playerModelString=resources.playerModelString;
        prepared.worldModelString=resources.worldModelString;prepared.inventoryItemString=resources.inventoryItemString;
        prepared.shellModel=resources.models[static_cast<unsigned>(Model::Shell)];
        prepared.ballistics.bubbleModel=resources.models[static_cast<unsigned>(Model::Bubble)];
        prepared.modelsReady=prepared.soundsReady=prepared.aimMetadataReady=prepared.pickupSoundReady=prepared.inventoryReady=true;
        prepared.automaticFireReady=resources.sounds[static_cast<unsigned>(Sound::Fire)]>0;
        prepared.automaticTailReady=resources.sounds[static_cast<unsigned>(Sound::Tail)]>0;
        prepared.melee.soundsReady=true;
        if constexpr(Weapon==LongGun::TMP)prepared.melee.samples=TMPMeleeSamples;
        else prepared.melee.samples=MP5MeleeSamples;
        prepared.hostSlot=Weapon==LongGun::TMP?TMPHudSlot:MP5HudSlot;
        prepared.hostPosition=Weapon==LongGun::TMP?TMPHudPosition:MP5HudPosition;
        output=prepared;return true;
    }
    static bool PrepareM16(void* opaque, M16Weapon&, M16Prepared& output) noexcept
    {
        auto& map = *static_cast<Map*>(opaque); Call call(map);
        if (!Current(&map) || !map.m16Prepared || !map.host.m16.prepare) return false;
        M16Prepared prepared{};
        if (!map.host.m16.prepare(map.host.context, map.m16Resources, prepared) || !Current(&map)) return false;
        const auto& resources = map.m16Resources;
        prepared.sequences = resources.sequences; prepared.aimOffset = resources.aimOffset; prepared.aimZoom = resources.aimZoom;
        prepared.viewModelString = resources.viewModelString; prepared.playerModelString = resources.playerModelString;
        prepared.worldModelString = resources.worldModelString; prepared.inventoryItemString = resources.inventoryItemString;
        prepared.shellModel = resources.models[static_cast<unsigned>(M16Model::Shell)];
        prepared.ballistics.bubbleModel = resources.models[static_cast<unsigned>(M16Model::Bubble)];
        prepared.modelsReady = prepared.soundsReady = prepared.aimMetadataReady = true;
        prepared.pickupSoundReady = prepared.inventoryReady = true;
        prepared.hostSlot = M16HudSlot; prepared.hostPosition = M16HudPosition;
        output = prepared; return true;
    }
    static bool M16BackendsFor(void* opaque, CBasePlayer& player, M16Backends& output) noexcept
    {
        auto& map = *static_cast<Map*>(opaque); Call call(map);
        const auto index = Slot(player); if (!index || !Current(&map) || !map.m16Prepared) return false;
        auto* owner = map.players[index].backend;
        if (!owner || owner->player != &player || !M16Current(owner)) return false;
        const auto& view = owner->m16View;
        output = {owner, M16Current, M16Frame, M16EffectBoundary, map.host.m16.effects | M16SoundEffects,
            view.animation, view.animationGeneration, owner->view.binding.life, view.playerThink};
        return true;
    }
    static bool M16InventoryPickup(void* opaque, CBasePlayer& player, M16Weapon& weapon, int itemString) noexcept
    {
        auto& map = *static_cast<Map*>(opaque); Call call(map); M16Backends backend{};
        return itemString > 0 && itemString == map.m16Resources.inventoryItemString && weapon.ResourcesReady() &&
            M16BackendsFor(&map, player, backend) && Current(&map);
    }

    static bool PrepareG43(void* opaque, G43Weapon&, G43Prepared& output) noexcept
    {
        auto& map = *static_cast<Map*>(opaque); Call call(map);
        if (!Current(&map) || !map.g43Prepared || !map.host.g43.prepare) return false;
        G43Prepared prepared{};
        if (!map.host.g43.prepare(map.host.context, map.g43Resources, prepared) || !Current(&map)) return false;
        const auto& resources = map.g43Resources;
        prepared.sequences = resources.sequences; prepared.aimOffset = resources.aimOffset; prepared.aimZoom = resources.aimZoom;
        prepared.viewModelString = resources.viewModelString; prepared.playerModelString = resources.playerModelString;
        prepared.worldModelString = resources.worldModelString; prepared.inventoryItemString = resources.inventoryItemString;
        prepared.shellModel = resources.models[static_cast<unsigned>(G43Model::Shell)];
        prepared.ballistics.bubbleModel = resources.models[static_cast<unsigned>(G43Model::Bubble)];
        prepared.modelsReady = prepared.soundsReady = prepared.aimMetadataReady = true;
        prepared.pickupSoundReady = prepared.inventoryReady = true;
        prepared.hostSlot = G43HudSlot; prepared.hostPosition = G43HudPosition;
        output = prepared; return true;
    }
    static bool G43BackendsFor(void* opaque, CBasePlayer& player, G43Backends& output) noexcept
    {
        auto& map = *static_cast<Map*>(opaque); Call call(map);
        const auto index = Slot(player); if (!index || !Current(&map) || !map.g43Prepared) return false;
        auto* owner = map.players[index].backend;
        if (!owner || owner->player != &player || !G43Current(owner)) return false;
        const auto& view = owner->g43View;
        output = {owner, G43Current, G43Frame, G43EffectBoundary, map.host.g43.effects | G43SoundEffects,
            view.animation, view.animationGeneration, owner->view.binding.life, view.playerThink};
        return true;
    }
    static bool G43InventoryPickup(void* opaque, CBasePlayer& player, G43Weapon& weapon, int itemString) noexcept
    {
        auto& map = *static_cast<Map*>(opaque); Call call(map); G43Backends backend{};
        return itemString > 0 && itemString == map.g43Resources.inventoryItemString && weapon.ResourcesReady() &&
            G43BackendsFor(&map, player, backend) && Current(&map);
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
    static bool VP70BackendsFor(void* opaque,CBasePlayer& player,VP70Backends& output) noexcept
    {
        auto& map=*static_cast<Map*>(opaque);Call call(map);
        const auto index=Slot(player);if(!index||!Current(&map)||!map.vp70Prepared)return false;
        auto* owner=map.players[index].backend;
        if(!owner||owner->player!=&player||!VP70Current(owner))return false;
        output={owner,VP70Current,VP70Frame,VP70Effect,map.host.vp70.effects|VP70NativeSoundEffects,
            owner->vp70View.animation,owner->vp70View.animationGeneration,owner->view.binding.life};
        return true;
    }
    template<LongGun Weapon> static bool AutomaticBackendsFor(void* opaque,CBasePlayer& player,TMPBackends& output) noexcept
    {
        auto& map=*static_cast<Map*>(opaque);Call call(map);
        const auto index=Slot(player);if(!index||!Current(&map)||!AutomaticPrepared<Weapon>(map))return false;
        auto* owner=map.players[index].backend;
        if(!owner||owner->player!=&player||!AutomaticCurrent<Weapon>(owner))return false;
        const auto& view=AutomaticView<Weapon>(*owner);
        output={owner,AutomaticCurrent<Weapon>,AutomaticFrame<Weapon>,AutomaticEffect<Weapon>,
            AutomaticHost<Weapon>(map).effects|TMPNativeSoundEffects,view.animation,view.animationGeneration,owner->view.binding.life};
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
    static bool VP70InventoryPickup(void* opaque,CBasePlayer& player,VP70Weapon& weapon,int itemString) noexcept
    {
        auto& map=*static_cast<Map*>(opaque);Call call(map);VP70Backends backend{};
        // Same explicit NativeCSInventory policy as Glock: real CanHave already
        // ran in WorldTouch; actual AddPlayerItem performs the insertion next.
        return itemString>0&&itemString==map.vp70Resources.inventoryItemString&&weapon.ResourcesReady()&&
            VP70BackendsFor(&map,player,backend)&&Current(&map);
    }
    template<LongGun Weapon> static bool AutomaticInventoryPickup(void* opaque,CBasePlayer& player,AutomaticWeapon<Weapon>& weapon,int itemString) noexcept
    {
        auto& map=*static_cast<Map*>(opaque);Call call(map);TMPBackends backend{};
        // WorldTouch already ran CanHavePlayerItem; native AddPlayerItem inserts once.
        return itemString>0&&itemString==AutomaticResources<Weapon>(map).inventoryItemString&&weapon.ResourcesReady()&&
            AutomaticBackendsFor<Weapon>(&map,player,backend)&&Current(&map);
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
    static bool PrepareRevolver(void* opaque,RevolverWeapon&,RevolverPrepared& output) noexcept
    {
        auto& map=*static_cast<Map*>(opaque);Call call(map);
        if(!Current(&map)||!map.revolverPrepared||!map.host.revolver.prepare)return false;
        RevolverPrepared prepared{};
        if(!map.host.revolver.prepare(map.host.context,map.revolverResources,prepared)||!Current(&map))return false;
        const auto& resources=map.revolverResources;
        prepared.sequences=resources.sequences;prepared.aimOffset=resources.aimOffset;prepared.aimZoom=resources.aimZoom;
        prepared.viewModelString=resources.viewModelString;prepared.playerModelString=resources.playerModelString;
        prepared.worldModelString=resources.worldModelString;prepared.inventoryItemString=resources.inventoryItemString;
        prepared.shellModel=resources.models[static_cast<unsigned>(RevolverModel::Shell)];
        prepared.ballistics.bubbleModel=resources.models[static_cast<unsigned>(RevolverModel::Bubble)];
        prepared.modelsReady=prepared.soundsReady=prepared.aimMetadataReady=prepared.pickupSoundReady=prepared.inventoryReady=true;
        prepared.melee.soundsReady=true;prepared.melee.samples=RevolverMeleeSamples;
        prepared.hostSlot=RevolverHudSlot;prepared.hostPosition=RevolverHudPosition;
        output=prepared;return true;
    }
    static bool RevolverBackendsFor(void* opaque,CBasePlayer& player,RevolverBackends& output) noexcept
    {
        auto& map=*static_cast<Map*>(opaque);Call call(map);
        const auto index=Slot(player);if(!index||!Current(&map)||!map.revolverPrepared)return false;
        auto* owner=map.players[index].backend;
        if(!owner||owner->player!=&player||!RevolverCurrent(owner))return false;
        output={owner,RevolverCurrent,RevolverFrame,RevolverEffectBoundary,map.host.revolver.effects|RevolverNativeSoundEffects,
            owner->revolverView.animation,owner->revolverView.animationGeneration,owner->view.binding.life};
        return true;
    }
    static bool RevolverInventoryPickup(void* opaque,CBasePlayer& player,RevolverWeapon& weapon,int itemString) noexcept
    {
        auto& map=*static_cast<Map*>(opaque);Call call(map);RevolverBackends backend{};
        return itemString>0&&itemString==map.revolverResources.inventoryItemString&&weapon.ResourcesReady()&&
            RevolverBackendsFor(&map,player,backend)&&Current(&map);
    }
    void Invalidate(CBasePlayer& player, bool modelChanging = false) noexcept
    {
        auto* map=active;
        if (!map) return;
        Call call(*map);
        const auto index = Slot(player);
        if (!index || map->players[index].object != &player) return;
        auto* owner=map->players[index].backend;
        if (modelChanging)
        {
            map->owners.ModelChanging(index, player);
            if(!Current(map)||map->players[index].object!=&player||map->players[index].backend!=owner)return;
        }
        if(owner)
        {
            if(owner->p345)owner->p345->Invalidate();
            if(owner->revolver)owner->revolver->Invalidate();
            if(owner->vp70)owner->vp70->Invalidate();
            if(owner->tmp)owner->tmp->Invalidate();
            if(owner->mp5)owner->mp5->Invalidate();
            if(owner->m16)owner->m16->Invalidate();
        if(owner->g43)owner->g43->Invalidate();
        }
        map->players[index].ready = false;
        map->players[index].backend = nullptr;
        if (!modelChanging)
        { map->players[index].actions.Retire(); map->players[index].actions = {}; map->owners.Invalidate(index, &player); }
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
        const auto oldModel = p.model;
        PlayerActionBinding m16Actions;
        if (request.m16Rules)
        {
            const auto rules = *request.m16Rules;
            if (!Current(map) || !map->m16Prepared || !map->host.m16.actions ||
                map->bindings.size() >= limits.ownerBindingsPerMap || p.model == std::numeric_limits<std::uint64_t>::max() || rules.bulletDamage < 0 ||
                (rules.reserveLimit != 60 && rules.reserveLimit != 200) ||
                ((spawn || !same) && oldLife == std::numeric_limits<std::uint64_t>::max()))
            { error = "M16 map resources, rules or player-action service are unavailable"; return false; }
            const auto nextLife = (spawn || !same) ? oldLife + 1 : oldLife;
            if (!map->host.m16.actions(map->host.context, player, map->epoch, nextLife, m16Actions) ||
                !Current(map) || p.life != oldLife || p.model != oldModel ||
                !m16Actions.PreparedFor(player, map->epoch, nextLife))
            { error = "M16 player actions do not match the current map/player life"; return false; }
        }
        PlayerActionBinding g43Actions;
        if (request.g43Rules)
        {
            const auto rules = *request.g43Rules;
            if (!Current(map) || !map->g43Prepared || !map->host.g43.actions ||
                map->bindings.size() >= limits.ownerBindingsPerMap || p.model == std::numeric_limits<std::uint64_t>::max() || rules.bulletDamage < 0 ||
                (rules.reserveLimit != 30 && rules.reserveLimit != 40) ||
                ((spawn || !same) && oldLife == std::numeric_limits<std::uint64_t>::max()))
            { error = "G43 map resources, rules or player-action service are unavailable"; return false; }
            const auto nextLife = (spawn || !same) ? oldLife + 1 : oldLife;
            if (!map->host.g43.actions(map->host.context, player, map->epoch, nextLife, g43Actions) ||
                !Current(map) || p.life != oldLife || p.model != oldModel ||
                !g43Actions.PreparedFor(player, map->epoch, nextLife))
            { error = "G43 player actions do not match the current map/player life"; return false; }
        }
        if (request.m16Rules && request.g43Rules && m16Actions.Think() != g43Actions.Think())
        { error = "Long guns require the same player-owned action service"; return false; }
        // Same-life renewal can arrive through userinfo without a model prehook.
        // Retain authenticated inventory tasks before replacing their authority.
        Invalidate(player, !spawn && same);
        if (!Current(map) || map->bindings.size() >= limits.ownerBindingsPerMap ||
            p.model == std::numeric_limits<std::uint64_t>::max() ||
            ((spawn || !same) && oldLife == std::numeric_limits<std::uint64_t>::max()))
        { error = "Glock map authority or retained binding budget exhausted"; return false; }
        p.object = &player; p.serial = player.edict()->serialnumber;
        p.life = (spawn || !same) ? oldLife + 1 : oldLife;
        if (request.m16Rules) p.actions = m16Actions;
        else if (request.g43Rules) p.actions = g43Actions;
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
            if(request.vp70)
            {
                if(!map->vp70Prepared){p.ready=false;map->owners.Invalidate(index,&player);error="VP70 map resources were not opted in";return false;}
                owner->vp70=std::make_unique<VP70OwnerView>();
                if(!owner->vp70->Prepare(player,view,request.vp70->rules,request.vp70->firstDrawn,request.rig,request.validatedModel,
                    request.compare,owner->vp70View,error,request.extent)||!Current(map))
                {p.ready=false;map->owners.Invalidate(index,&player);return false;}
            }
            if(request.tmp)
            {
                if(!map->tmpPrepared){p.ready=false;map->owners.Invalidate(index,&player);error="TMP map resources were not opted in";return false;}
                owner->tmp=std::make_unique<TMPOwnerView>();
                if(!owner->tmp->Prepare(player,view,request.tmp->rules,request.tmp->firstDrawn,request.rig,request.validatedModel,
                    request.compare,owner->tmpView,error,request.extent)||!Current(map))
                {p.ready=false;map->owners.Invalidate(index,&player);return false;}
            }
            if(request.mp5Rules)
            {
                if(!map->mp5Prepared){p.ready=false;map->owners.Invalidate(index,&player);error="MP5 map resources were not opted in";return false;}
                owner->mp5=std::make_unique<MP5OwnerView>();
                if(!owner->mp5->Prepare(player,view,*request.mp5Rules,false,request.rig,request.validatedModel,
                    request.compare,owner->mp5View,error,request.extent)||!Current(map))
                {p.ready=false;map->owners.Invalidate(index,&player);return false;}
            }
            if(request.revolver)
            {
                if(!map->revolverPrepared){p.ready=false;map->owners.Invalidate(index,&player);error="Revolver map resources were not opted in";return false;}
                owner->revolver=std::make_unique<RevolverOwnerView>();
                if(!owner->revolver->Prepare(player,view,request.revolver->rules,request.revolver->state,request.rig,
                    request.validatedModel,request.compare,owner->revolverView,error,request.extent)||!Current(map))
                {p.ready=false;map->owners.Invalidate(index,&player);return false;}
            }
            if (request.m16Rules)
            {
                owner->m16 = std::make_unique<M16OwnerView>();
                if (!owner->m16->Prepare(player, view, *request.m16Rules, m16Actions, request.rig,
                    request.validatedModel, request.compare, owner->m16View, error, request.extent) || !Current(map))
                { p.ready = false; map->owners.Invalidate(index, &player); return false; }
            }
            if (request.g43Rules)
            {
                owner->g43 = std::make_unique<G43OwnerView>();
                if (!owner->g43->Prepare(player, view, *request.g43Rules, g43Actions, request.rig,
                    request.validatedModel, request.compare, owner->g43View, error, request.extent) || !Current(map))
                { p.ready = false; map->owners.Invalidate(index, &player); return false; }
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
    const bool revolverRequested=host.revolver.prepare||host.revolver.effect||host.revolver.effects;
    const bool vp70Requested=host.vp70.prepare||host.vp70.effect||host.vp70.effects;
    const bool tmpRequested=host.tmp.prepare||host.tmp.effect||host.tmp.effects;
    const bool mp5Requested=host.mp5.prepare||host.mp5.effect||host.mp5.effects;
    const bool m16Requested=host.m16.prepare||host.m16.effect||host.m16.actions||host.m16.effects||host.m16.soundRules != guest::cof::PlaySfxRules::Unsupported;
    const bool g43Requested=host.g43.prepare||host.g43.effect||host.g43.actions||host.g43.effects||host.g43.soundRules != guest::cof::PlaySfxRules::Unsupported;
    if (!host.current || !host.ownerCurrent || !host.prepare || !host.effect ||
        (host.effects & ~GlockRequiredEffects) ||
        !state.limits.ownerBindingsPerMap || state.maps.size() >= state.limits.retainedMaps ||
        state.generation == std::numeric_limits<std::uint64_t>::max()||
        (p345Requested&&(!host.p345.prepare||!host.p345.effect||
            (host.p345.effects&~P345RequiredEffects)||
            ((host.p345.effects|P345NativeSoundEffects)&P345RequiredEffects)!=P345RequiredEffects))||
        (revolverRequested&&(!host.revolver.prepare||!host.revolver.effect||
            (host.revolver.effects&~RevolverRequiredEffects)||
            ((host.revolver.effects|RevolverNativeSoundEffects)&RevolverRequiredEffects)!=RevolverRequiredEffects))||
        (vp70Requested&&(!host.vp70.prepare||!host.vp70.effect||
            (host.vp70.effects&~VP70RequiredEffects)||
            ((host.vp70.effects|VP70NativeSoundEffects)&VP70RequiredEffects)!=VP70RequiredEffects))||
        (tmpRequested&&(!host.tmp.prepare||!host.tmp.effect||
            (host.tmp.effects&~TMPRequiredEffects)||
            ((host.tmp.effects|TMPNativeSoundEffects)&TMPRequiredEffects)!=TMPRequiredEffects))||
        (mp5Requested&&(!host.mp5.prepare||!host.mp5.effect||
            (host.mp5.effects&~MP5RequiredEffects)||
            ((host.mp5.effects|MP5NativeSoundEffects)&MP5RequiredEffects)!=MP5RequiredEffects))||
        (m16Requested&&(!host.m16.prepare||!host.m16.effect||!host.m16.actions||host.m16.soundRules != guest::cof::PlaySfxRules::Cooperative||
            (host.m16.effects & ~M16RequiredEffects)||
            ((host.m16.effects | State::M16SoundEffects) & M16RequiredEffects) != M16RequiredEffects))||
        (g43Requested&&(!host.g43.prepare||!host.g43.effect||!host.g43.actions||host.g43.soundRules != guest::cof::PlaySfxRules::Cooperative||
            (host.g43.effects & ~G43RequiredEffects)||
            ((host.g43.effects | State::G43SoundEffects) & G43RequiredEffects) != G43RequiredEffects)))
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
        if(vp70Requested)
        {
            const auto vp70=PrepareVP70Resources({&current,State::Current,current.epoch},current.vp70Resources);
            if(!vp70||!State::Current(&current))
            {EndMap();return vp70?ResourceResult{ResourceStatus::Cancelled}:vp70;}
            current.vp70Prepared=true;
            if(!InstallVP70Factory({&current,State::Current,State::PrepareVP70,State::VP70BackendsFor,State::VP70InventoryPickup}))
            {EndMap();return {ResourceStatus::InvalidAuthority};}
        }
        if(tmpRequested)
        {
            const auto tmp=PrepareTMPResources({&current,State::Current,current.epoch},current.tmpResources);
            if(!tmp||!State::Current(&current))
            {EndMap();return tmp?ResourceResult{ResourceStatus::Cancelled}:tmp;}
            current.tmpPrepared=true;
            if(!InstallTMPFactory({&current,State::Current,State::PrepareAutomatic<LongGun::TMP>,State::AutomaticBackendsFor<LongGun::TMP>,State::AutomaticInventoryPickup<LongGun::TMP>}))
            {EndMap();return {ResourceStatus::InvalidAuthority};}
        }
        if(mp5Requested)
        {
            const auto mp5=PrepareMP5Resources({&current,State::Current,current.epoch},current.mp5Resources);
            if(!mp5||!State::Current(&current))
            {EndMap();return mp5?ResourceResult{ResourceStatus::Cancelled}:mp5;}
            current.mp5Prepared=true;
            if(!InstallMP5Factory({&current,State::Current,State::PrepareAutomatic<LongGun::MP5>,
                State::AutomaticBackendsFor<LongGun::MP5>,State::AutomaticInventoryPickup<LongGun::MP5>}))
            {EndMap();return {ResourceStatus::InvalidAuthority};}
        }
        if (m16Requested)
        {
            const auto m16 = PrepareM16Resources({&current, State::Current, current.epoch}, current.m16Resources);
            if (!m16 || !State::Current(&current))
            { EndMap(); return m16 ? ResourceResult{ResourceStatus::Cancelled} : m16; }
            current.m16SoundMessage = guest::cof::RegisterPlaySfxMessage({&current, State::Current});
            if (!current.m16SoundMessage || !State::Current(&current))
            { EndMap(); return {ResourceStatus::InvalidAuthority}; }
            current.m16Prepared = true;
            if (!InstallM16Factory({&current, State::Current, State::PrepareM16, State::M16BackendsFor, State::M16InventoryPickup}))
            { EndMap(); return {ResourceStatus::InvalidAuthority}; }
        }
        if (g43Requested)
        {
            const auto g43 = PrepareG43Resources({&current, State::Current, current.epoch}, current.g43Resources);
            if (!g43 || !State::Current(&current))
            { EndMap(); return g43 ? ResourceResult{ResourceStatus::Cancelled} : g43; }
            current.g43SoundMessage = guest::cof::RegisterPlaySfxMessage({&current, State::Current});
            if (!current.g43SoundMessage || !State::Current(&current))
            { EndMap(); return {ResourceStatus::InvalidAuthority}; }
            current.g43Prepared = true;
            if (!InstallG43Factory({&current, State::Current, State::PrepareG43, State::G43BackendsFor, State::G43InventoryPickup}))
            { EndMap(); return {ResourceStatus::InvalidAuthority}; }
        }
        if(revolverRequested)
        {
            const auto revolver=PrepareRevolverResources({&current,State::Current,current.epoch},current.revolverResources);
            if(!revolver||!State::Current(&current))
            {EndMap();return revolver?ResourceResult{ResourceStatus::Cancelled}:revolver;}
            current.revolverPrepared=true;
            if(!InstallRevolverFactory({&current,State::Current,State::PrepareRevolver,State::RevolverBackendsFor,State::RevolverInventoryPickup}))
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
    state.active = nullptr; map->active = map->prepared = map->p345Prepared = map->revolverPrepared = map->vp70Prepared = map->tmpPrepared = map->mp5Prepared = map->m16Prepared = map->g43Prepared = false;
    ResetGlockFactory();
    ResetP345Factory();
    ResetRevolverFactory();
    ResetVP70Factory();
    ResetTMPFactory();
    ResetMP5Factory();
    ResetM16Factory();
    ResetG43Factory();
    for(auto& owner:map->bindings)
    {
        if(owner->p345)owner->p345->Invalidate();
        if(owner->revolver)owner->revolver->Invalidate();
        if(owner->vp70)owner->vp70->Invalidate();
        if(owner->tmp)owner->tmp->Invalidate();
        if(owner->mp5)owner->mp5->Invalidate();
        if(owner->m16)owner->m16->Invalidate();
            if(owner->g43)owner->g43->Invalidate();
    }
    map->owners.EndMap();
    for (auto& player : map->players)
    { player.actions.Retire(); player.actions = {}; player.ready = false; player.backend = nullptr; }
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
const VP70Resources* GlockRuntime::PreparedVP70Resources() const noexcept
{return state_->active&&state_->active->vp70Prepared?&state_->active->vp70Resources:nullptr;}
const TMPResources* GlockRuntime::PreparedTMPResources() const noexcept
{return state_->active&&state_->active->tmpPrepared?&state_->active->tmpResources:nullptr;}

const MP5Resources* GlockRuntime::PreparedMP5Resources() const noexcept
{return state_->active&&state_->active->mp5Prepared?&state_->active->mp5Resources:nullptr;}
const M16Resources* GlockRuntime::PreparedM16Resources() const noexcept
{return state_->active&&state_->active->m16Prepared?&state_->active->m16Resources:nullptr;}
const G43Resources* GlockRuntime::PreparedG43Resources() const noexcept
{return state_->active&&state_->active->g43Prepared?&state_->active->g43Resources:nullptr;}

const RevolverResources* GlockRuntime::PreparedRevolverResources() const noexcept
{return state_->active&&state_->active->revolverPrepared?&state_->active->revolverResources:nullptr;}
bool GlockRuntime::PlayerSpawned(CBasePlayer& player, const GlockPlayerPreparation& request, std::string& error)
{ return state_->PreparePlayer(player, request, true, error); }
bool GlockRuntime::PlayerModelChanged(CBasePlayer& player, const GlockPlayerPreparation& request, std::string& error)
{ return state_->PreparePlayer(player, request, false, error); }
void GlockRuntime::PlayerModelChanging(CBasePlayer& player) noexcept { state_->Invalidate(player, true); }
void GlockRuntime::PlayerDied(CBasePlayer& player) noexcept { state_->Invalidate(player); }
void GlockRuntime::PlayerDisconnected(CBasePlayer& player) noexcept { state_->Invalidate(player); }
bool GlockRuntime::Backend(CBasePlayer& player, GlockBackends& output) noexcept
{ return state_->active && State::Backends(state_->active, player, output); }
bool GlockRuntime::Backend(CBasePlayer& player,P345Backends& output) noexcept
{return state_->active&&State::P345BackendsFor(state_->active,player,output);}
bool GlockRuntime::Backend(CBasePlayer& player,VP70Backends& output) noexcept
{return state_->active&&State::VP70BackendsFor(state_->active,player,output);}
bool GlockRuntime::Backend(CBasePlayer& player,TMPBackends& output) noexcept
{return state_->active&&State::AutomaticBackendsFor<LongGun::TMP>(state_->active,player,output);}

bool GlockRuntime::MP5Backend(CBasePlayer& player,TMPBackends& output) noexcept
{return state_->active&&State::AutomaticBackendsFor<LongGun::MP5>(state_->active,player,output);}
bool GlockRuntime::Backend(CBasePlayer& player,M16Backends& output) noexcept
{return state_->active&&State::M16BackendsFor(state_->active,player,output);}
bool GlockRuntime::Backend(CBasePlayer& player,G43Backends& output) noexcept
{return state_->active&&State::G43BackendsFor(state_->active,player,output);}

bool GlockRuntime::Backend(CBasePlayer& player,RevolverBackends& output) noexcept
{return state_->active&&State::RevolverBackendsFor(state_->active,player,output);}
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
bool GlockRuntime::PersistentState(CBasePlayer& player,RevolverOwnerState& output) noexcept
{
    if(!state_->active)return false;
    auto& map=*state_->active;State::Call call(map);const auto slot=State::Slot(player);
    if(!slot||!State::Current(&map))return false;
    auto* owner=map.players[slot].backend;
    return owner&&owner->player==&player&&State::OwnerCurrent(owner)&&owner->revolver&&
        owner->revolver->PersistentState(owner->revolverView.binding,output)&&State::OwnerCurrent(owner);
}
bool GlockRuntime::PersistentVP70FirstDraw(CBasePlayer& player,bool& output) noexcept
{
    if(!state_->active)return false;
    auto& map=*state_->active;State::Call call(map);const auto slot=State::Slot(player);
    if(!slot||!State::Current(&map))return false;
    auto* owner=map.players[slot].backend;
    bool value{};
    if(!owner||owner->player!=&player||!State::VP70Current(owner)||
        !owner->vp70->PersistentFirstDraw(owner->vp70View.binding,value)||!State::VP70Current(owner))return false;
    output=value;return true;
}
bool GlockRuntime::PersistentTMPFirstDraw(CBasePlayer& player,bool& output) noexcept
{
    if(!state_->active)return false;
    auto& map=*state_->active;State::Call call(map);const auto slot=State::Slot(player);
    if(!slot||!State::Current(&map))return false;
    auto* owner=map.players[slot].backend;
    bool value{};
    if(!owner||owner->player!=&player||!State::TMPCurrent(owner)||
        !owner->tmp->PersistentFirstDraw(owner->tmpView.binding,value)||!State::TMPCurrent(owner))return false;
    output=value;return true;
}

bool GlockRuntime::PreserveRevolverReload(CBasePlayer& player,int playerAnimation,int& desiredSequence) noexcept
{
    if(!state_->active)return false;
    auto& map=*state_->active;State::Call call(map);const auto slot=State::Slot(player);
    if(!slot||!State::Current(&map))return false;
    auto* owner=map.players[slot].backend;
    if(!owner||owner->player!=&player||!State::RevolverCurrent(owner))return false;
    const auto sequence=owner->revolver->ReloadSequence(owner->revolverView.binding,playerAnimation);
    if(!sequence||!State::RevolverCurrent(owner))return false;
    desiredSequence=*sequence;
    player.m_Activity=player.m_IdealActivity=ACT_RELOAD;
    return true;
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
