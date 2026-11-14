// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "guest_cof_effects.hpp"
#include "weapons.h"
#include "native/guest/cof/catalog.hpp"
#include "native/guest/cof/particle_protocol.hpp"
#include "native/guest/cof/server/control_dispatch.hpp"
#include "native/guest/cof/server/effects_dispatch.hpp"
#include "native/guest/cof/server/engine_message.hpp"
#include "native/guest/cof/server/screen_shake.hpp"
#include "native/guest/cof/server/crosshair_dispatch.hpp"
#include <cmath>
#include <limits>
#include <bit>
#include <algorithm>

namespace csol::server::cof
{
using namespace guest::cof;
using namespace guest::cof::server;
namespace
{
constexpr std::uint32_t Bit(GlockEffect kind) noexcept { return 1u << static_cast<unsigned>(kind); }
unsigned Slot(CBasePlayer& player) noexcept
{
    if (!g_engfuncs.pfnIndexOfEdict || !player.pev) return 0;
    const int index = ENTINDEX(player.edict());
    return index > 0 && index < static_cast<int>(GlockOwnerRuntime::SlotCount) ? unsigned(index) : 0;
}
bool Sent(WeaponEffectStatus status) noexcept { return status == WeaponEffectStatus::Issued; }
bool MessageId(int id) noexcept { return id >= 64 && id <= 255; }
unsigned OriginalWeapon(const CBasePlayerWeapon& item) noexcept
{
    if(item.m_iId==Pistols[0].hostId&&dynamic_cast<const GlockWeapon*>(&item))return 3;
    if(item.m_iId==Pistols[1].hostId&&dynamic_cast<const P345Weapon*>(&item))return 7;
    if(item.m_iId==Pistols[2].hostId&&dynamic_cast<const RevolverWeapon*>(&item))return 21;
    return 0;
}
float Spread(const CBasePlayerWeapon& item) noexcept
{
    if(auto* weapon=dynamic_cast<const GlockWeapon*>(&item))return weapon->Spread().scalar;
    if(auto* weapon=dynamic_cast<const P345Weapon*>(&item))return weapon->Spread().scalar;
    if(auto* weapon=dynamic_cast<const RevolverWeapon*>(&item))return weapon->Spread().scalar;
    return -1;
}
}

struct GlockEffects::Lease
{
    GlockEffects& effects;
    const OwnerBinding binding;
    CBasePlayer& player;
    CBasePlayerWeapon& weapon;
    edict_t *owner, *item;
    const int ownerSerial, itemSerial;
    std::optional<float> revolverAngle{};
};
bool GlockEffects::Active(void* context) noexcept
{
    auto& call = *static_cast<Lease*>(context);
    return call.effects.Current() && call.binding.runtime && call.binding.runtime->Current(call.binding) &&
        call.owner && !call.owner->free && call.owner->serialnumber == call.ownerSerial &&
        call.owner->pvPrivateData == &call.player && call.player.pev == &call.owner->v &&
        call.item && !call.item->free && call.item->serialnumber == call.itemSerial &&
        call.item->pvPrivateData == &call.weapon && call.weapon.pev == &call.item->v &&
        call.weapon.m_pPlayer == &call.player && call.effects.Current();
}
bool GlockEffects::Current() const noexcept
{
    const auto call = authority_;
    return call.current && call.generation && call.current(call.context) &&
        authority_.context == call.context && authority_.current == call.current &&
        authority_.generation == call.generation;
}
void GlockEffects::EndMap() noexcept
{
    // Do not clear storage while a synchronous sender holds this map object.
    // Revocation alone makes every existing stack lease fail its next check.
    authority_ = {};
}
bool GlockEffects::Prepare(ResourceAuthority authority, const GlockResources& resources,
    GlockEffectPolicy policy) noexcept
{
    if (Current() && authority_.generation == authority.generation) return false;
    EndMap();
    if (!authority.current || !authority.generation || resources.generation != authority.generation ||
        !policy.nativeMultiplayer || !policy.noCustomClothing || !g_engfuncs.pfnRegUserMsg ||
        !authority.current(authority.context)) return false;
    authority_ = authority; policy_ = policy; resources_ = resources; clients_ = {};
    const PlaySfxCall call{this, [](void* p) noexcept { return static_cast<GlockEffects*>(p)->Current(); }};
    presentation_ = RegisterPresentationMessage(call);
    if (!Current() || !MessageId(presentation_)) return false;
    sound_ = RegisterPlaySfxMessage(call);
    if (!Current() || !MessageId(sound_)) return false;
    control_ = RegisterControlMessage({call.context, call.current});
    if (!Current() || !MessageId(control_)) return false;
    particle_ = REG_USER_MSG(ParticleMessage, -1);
    if (!Current() || !MessageId(particle_)) return false;
    decal_ = REG_USER_MSG(guest::cof::resources::CustomDecalMessage, 18);
    if (!Current() || !MessageId(decal_)) return false;
    shake_ = REG_USER_MSG("ScreenShake", 6);
    if(!Current()||!MessageId(shake_))return false;
    crosshair_=REG_USER_MSG(CrosshairMessage,CrosshairBytes);
    if(!Current()||!MessageId(crosshair_))return false;
    ammoX_=REG_USER_MSG("AmmoX",2);
    return Current()&&MessageId(ammoX_);
}
std::uint32_t GlockEffects::Capabilities() const noexcept
{
    if (!Current()) return 0;
    std::uint32_t result{};
    if (policy_.nativeMultiplayer && policy_.noCustomClothing)
        result |= Bit(GlockEffect::DeployPrelude) | Bit(GlockEffect::FirstDraw) | Bit(GlockEffect::Tutorial);
    if (g_engfuncs.pfnMessageBegin && g_engfuncs.pfnMessageEnd && g_engfuncs.pfnWriteByte &&
        g_engfuncs.pfnGetPlayerUserId && g_engfuncs.pfnInfoKeyValue && g_engfuncs.pfnGetInfoKeyBuffer)
        result |= Bit(GlockEffect::InventoryChanged) | Bit(GlockEffect::ReserveHud) | Bit(GlockEffect::ViewAnimation);
    if (MessageId(presentation_)) result |= Bit(GlockEffect::Aim);
    if (MessageId(control_))
        result |= Bit(GlockEffect::DeployReset) | Bit(GlockEffect::Body) | Bit(GlockEffect::ShotState);
    if (MessageId(particle_)) result |= Bit(GlockEffect::Gunsmoke);
    if (MessageId(sound_)) result |= Bit(GlockEffect::FireSound);
    if (MessageId(shake_)) result |= Bit(GlockEffect::Shake);
    if (resources_.events[0] && g_engfuncs.pfnPlaybackEvent && g_engfuncs.pfnIndexOfEdict)
        result |= Bit(GlockEffect::FireEvent);
    if (resources_.events[1] && g_engfuncs.pfnPlaybackEvent)
        result |= Bit(GlockEffect::Recoil);
    return result;
}
bool GlockEffects::FillPrepared(const GlockResources& resources, GlockPrepared& prepared) noexcept
{
    if (!Current() || resources.generation != authority_.generation ||
        resources.events != resources_.events) return false;
    prepared.ballistics.customDecalMessage = decal_;
    prepared.melee.shakeMessage = shake_;
    prepared.melee.recoil = MeleeRecoil;
    prepared.melee.context = this;
    prepared.world = MeleeWorld::Multiplayer;
    prepared.particleReady = MessageId(particle_);
    prepared.deployPreludeReady = policy_.nativeMultiplayer && policy_.noCustomClothing;
    return Current();
}
std::uint32_t GlockEffects::P345Capabilities() const noexcept
{
    constexpr auto mask=(1u<<static_cast<unsigned>(P345Effect::Count))-1;
    auto result=Capabilities()&mask;
    if(Current()&&g_engfuncs.pfnMessageBegin&&g_engfuncs.pfnMessageEnd&&g_engfuncs.pfnWriteByte&&
        g_engfuncs.pfnWriteCoord&&g_engfuncs.pfnWriteAngle&&g_engfuncs.pfnWriteShort)
        result|=1u<<static_cast<unsigned>(P345Effect::Shell);
    return result;
}
bool GlockEffects::FillPrepared(const P345Resources& resources,P345Prepared& prepared) noexcept
{
    if(!Current()||resources.generation!=authority_.generation||resources.events!=resources_.events)return false;
    prepared.ballistics.customDecalMessage=decal_;prepared.melee.shakeMessage=shake_;
    prepared.melee.recoil=MeleeRecoil;prepared.melee.context=this;
    prepared.world=MeleeWorld::Multiplayer;prepared.particleReady=MessageId(particle_);
    prepared.deployPreludeReady=policy_.nativeMultiplayer&&policy_.noCustomClothing;
    return Current();
}
std::uint32_t GlockEffects::RevolverCapabilities(const RevolverResources& resources) const noexcept
{
    if(!Current()||resources.generation!=authority_.generation||
        resources.events[0]!=resources_.events[0]||resources.events[1]!=resources_.events[1])return 0;
    auto result=Capabilities()&((1u<<static_cast<unsigned>(RevolverEffect::ReloadStage))-1u);
    if(resources.events[2]&&g_engfuncs.pfnPlaybackEvent)
        result|=1u<<static_cast<unsigned>(RevolverEffect::EjectEvent);
    if(MessageId(ammoX_)&&g_engfuncs.pfnMessageBegin&&g_engfuncs.pfnMessageEnd&&g_engfuncs.pfnWriteByte)
        result|=1u<<static_cast<unsigned>(RevolverEffect::AmmoX);
    if(MessageId(presentation_))result|=1u<<static_cast<unsigned>(RevolverEffect::Cylinder);
    // The native consumer for the original player sequence deadline is still
    // required. Its absent bit prevents an incomplete whole-weapon admission.
    return result;
}
bool GlockEffects::FillPrepared(const RevolverResources& resources,RevolverPrepared& prepared) noexcept
{
    const auto capabilities=RevolverCapabilities(resources);
    if(!capabilities)return false;
    prepared.ballistics.customDecalMessage=decal_;prepared.melee.shakeMessage=shake_;
    prepared.melee.recoil=MeleeRecoil;prepared.melee.context=this;
    prepared.world=MeleeWorld::Multiplayer;prepared.particleReady=MessageId(particle_);
    prepared.deployPreludeReady=policy_.nativeMultiplayer&&policy_.noCustomClothing;
    prepared.cylinderReady=(capabilities&(1u<<unsigned(RevolverEffect::Cylinder)))!=0;
    prepared.ejectEventReady=(capabilities&(1u<<unsigned(RevolverEffect::EjectEvent)))!=0;
    prepared.ammoXReady=(capabilities&(1u<<unsigned(RevolverEffect::AmmoX)))!=0;
    prepared.reloadStagesReady=false;
    return Current();
}
void GlockEffects::MeleeRecoil(void* context, CBasePlayer& player, const Vector& impulse) noexcept
{
    auto& self = *static_cast<GlockEffects*>(context);
    if (!self.Current() || !player.pev || !player.m_pActiveItem) return;
    auto* weapon = dynamic_cast<CBasePlayerWeapon*>(player.m_pActiveItem);
    if (!weapon || !OriginalWeapon(*weapon) || weapon->m_pPlayer != &player) return;
    const EHANDLE owner(&player), item(weapon);
    struct Call { GlockEffects& map; const EHANDLE& owner; const EHANDLE& item; CBasePlayer& player; CBasePlayerWeapon& weapon; };
    Call state{self, owner, item, player, *weapon};
    const PlaySfxCall call{&state, [](void* p) noexcept {
        auto& c = *static_cast<Call*>(p);
        return c.map.Current() && c.owner.Get<CBasePlayer>() == &c.player &&
            c.item.Get<CBasePlayerWeapon>() == &c.weapon && c.player.m_pActiveItem == &c.weapon &&
            c.weapon.m_pPlayer == &c.player && c.player.IsAlive();
    }};
    CooperativeViewRecoil(player, impulse, self.resources_.events[1], call);
}
bool GlockEffects::Presentation(Lease& lease, bool enabled, int fov, float speed,bool skipIdentical) noexcept
{
    if(!Active(&lease))return false;
    const auto original=OriginalWeapon(lease.weapon);
    if (!std::isfinite(speed) || speed < 0 || speed > 4095 ||
        !original || (original==21)!=lease.revolverAngle.has_value() ||
        (enabled && (fov < 1 || fov > 179))) return false;
    const auto slot = Slot(lease.player);
    if (!slot || lease.binding.map > UINT32_MAX || lease.binding.life > UINT32_MAX) return false;
    auto& client = clients_[slot];
    const bool instance = !client.active || client.key.sessionEpoch != lease.binding.map ||
        client.key.lifeEpoch != lease.binding.life || client.weapon.Get<CBasePlayerWeapon>() != &lease.weapon ||
        client.ownerSerial != lease.ownerSerial || client.model != lease.binding.model;
    if ((instance && client.nextInstance == UINT32_MAX) || client.revision == UINT32_MAX) return false;
    const AuthorityKey key{static_cast<std::uint32_t>(lease.binding.map),
        static_cast<std::uint32_t>(lease.binding.life), instance ? client.nextInstance + 1 : client.key.generation};
    const auto fovMode=static_cast<std::uint16_t>(enabled?fov:0);
    const auto zoomSpeed=static_cast<std::int16_t>(speed*8.0F);
    if(skipIdentical&&!instance&&client.aimEnabled==enabled&&client.fovMode==fovMode&&
        client.zoomSpeedEighths==zoomSpeed&&client.revolverAngle&&lease.revolverAngle&&
        std::bit_cast<std::uint32_t>(*client.revolverAngle)==std::bit_cast<std::uint32_t>(*lease.revolverAngle))
        return Active(&lease);
    const PresentationState state{key, client.revision + 1, static_cast<std::uint8_t>(original), enabled,
        fovMode,zoomSpeed,lease.revolverAngle};
    // Commit counters before the engine call so reentrant sends cannot reuse a
    // revision. A retired scope never publishes this row into another map.
    client.key = key; client.revision = state.revision; client.nextInstance = key.generation;
    client.weapon = &lease.weapon; client.ownerSerial = lease.ownerSerial; client.active = true;
    client.model = lease.binding.model;
    client.aimEnabled=enabled;client.fovMode=fovMode;client.zoomSpeedEighths=zoomSpeed;
    client.revolverAngle=lease.revolverAngle;
    return Sent(SendPresentationState(lease.player, state, presentation_, {&lease, Active})) && Active(&lease);
}
bool GlockEffects::Release(CBasePlayer& player, const PlaySfxCall& call) noexcept
{
    const auto slot = Slot(player);
    if (!slot || !call.current || !call.current(call.context)) return false;
    auto& client = clients_[slot];
    if (!client.active) return true;
    if (client.ownerSerial != player.edict()->serialnumber || client.revision == UINT32_MAX) return false;
    const PresentationState state{client.key, client.revision + 1, 0, false, 0, 0};
    client.revision = state.revision; client.active = false; client.weapon = nullptr;client.revolverAngle.reset();
    return Sent(SendPresentationState(player, state, presentation_, call));
}
bool GlockEffects::SynchronizeSelection(CBasePlayer& player) noexcept
{
    if (!Current() || !player.pev) return false;
    const EHANDLE owner(&player);
    struct Call { GlockEffects& map; CBasePlayer& player; const EHANDLE& entity; } state{*this, player, owner};
    const PlaySfxCall call{&state, [](void* p) noexcept {
        auto& c = *static_cast<Call*>(p);
        return c.map.Current() && c.entity.Get<CBasePlayer>() == &c.player;
    }};
    if (!call.current(call.context)) return false;
    auto* item = player.IsAlive() ? dynamic_cast<CBasePlayerWeapon*>(player.m_pActiveItem) : nullptr;
    return item&&OriginalWeapon(*item) ? item->m_pPlayer == &player : Release(player, call);
}
bool GlockEffects::RetirePlayer(CBasePlayer& player) noexcept
{
    if (!Current() || !player.pev) return false;
    const EHANDLE entity(&player);
    struct Call { GlockEffects& map; CBasePlayer& player; const EHANDLE& entity; } state{*this, player, entity};
    return Release(player, {&state, [](void* p) noexcept {
        auto& c = *static_cast<Call*>(p);
        return c.map.Current() && c.entity.Get<CBasePlayer>() == &c.player;
    }});
}
bool GlockEffects::CrosshairSnapshot(CBasePlayer& player,const PlaySfxCall& authority) noexcept
{
    const auto source=authority;
    if(!source.current||!source.current(source.context)||!Current())return false;
    const auto slot=Slot(player);
    if(!slot||!source.current(source.context)||!Current())return false;
    auto& client=clients_[slot];
    auto* item=client.weapon.Get<CBasePlayerWeapon>();
    if(!client.active||!item||!OriginalWeapon(*item)||player.m_pActiveItem!=item||item->m_pPlayer!=&player||
       client.ownerSerial!=player.edict()->serialnumber||client.snapshotSequence==UINT32_MAX)return false;
    const EHANDLE owner(&player),weapon(item);
    const auto key=client.key;const auto revision=client.revision;
    struct Call
    {
        GlockEffects& effects;const PlaySfxCall source;const EHANDLE& owner;const EHANDLE& weapon;
        CBasePlayer& player;CBasePlayerWeapon& item;Client& client;AuthorityKey key;std::uint32_t revision;
    } state{*this,source,owner,weapon,player,*item,client,key,revision};
    const auto current=[](void* value) noexcept {
        auto& c=*static_cast<Call*>(value);
        return c.source.current(c.source.context)&&c.effects.Current()&&c.client.active&&
            c.client.key==c.key&&c.client.revision==c.revision&&c.owner.Get<CBasePlayer>()==&c.player&&
            c.weapon.Get<CBasePlayerWeapon>()==&c.item&&c.player.m_pActiveItem==&c.item&&c.item.m_pPlayer==&c.player;
    };
    if(!current(&state))return false;
    std::uint8_t scalar{};
    if(!QuantizeCrosshair(Spread(*item),scalar)||!current(&state))return false;
    const guest::cof::CrosshairSnapshot snapshot{key,revision,++client.snapshotSequence,scalar};
    return SendCrosshairSnapshot(player,snapshot,crosshair_,{&state,current})==CrosshairSend::Sent;
}
bool GlockEffects::Dispatch(const GlockResources& resources, const OwnerBinding& binding,
    CBasePlayer& player, CBasePlayerWeapon& weapon, const GlockEvent& event) noexcept
{
    return DispatchWeapon({resources.generation,resources.viewModelString,resources.events,resources.models[unsigned(GlockModel::Shell)]},binding,player,weapon,
        event,3,GlockLocalWaves[6].original,Capabilities());
}
bool GlockEffects::Dispatch(const P345Resources& resources,const OwnerBinding& binding,
    CBasePlayer& player,CBasePlayerWeapon& weapon,const P345Event& event) noexcept
{
    constexpr std::array mapping{GlockEffect::DeployPrelude,GlockEffect::ReserveHud,GlockEffect::InventoryChanged,
        GlockEffect::ViewAnimation,GlockEffect::EmptyClick,GlockEffect::Shell,GlockEffect::Gunsmoke,
        GlockEffect::FireEvent,GlockEffect::Recoil,GlockEffect::Shake,GlockEffect::Aim,GlockEffect::FireSound,
        GlockEffect::ShotState,GlockEffect::ShotLatch,GlockEffect::PickupSound,GlockEffect::DeployReset,
        GlockEffect::Sleeve,GlockEffect::AmmoPickupSound};
    static_assert(mapping.size()==static_cast<unsigned>(P345Effect::Count));
    static_assert([&]{for(unsigned i=0;i<mapping.size();++i)if(static_cast<unsigned>(mapping[i])!=i)return false;return true;}());
    const auto index=static_cast<unsigned>(event.kind);if(index>=mapping.size())return false;
    const GlockEvent translated{mapping[index],event.first,event.second,event.a,event.b,event.c,
        event.value,event.other,event.enabled,event.modelString};
    return DispatchWeapon({resources.generation,resources.viewModelString,resources.events,resources.models[unsigned(P345Model::Shell)]},binding,player,weapon,
        translated,7,P345LocalWaves[6].original,P345Capabilities());
}
bool GlockEffects::Dispatch(const RevolverResources& resources,const OwnerBinding& binding,
    CBasePlayer& player,CBasePlayerWeapon& weapon,const RevolverEvent& event,RevolverOwnerState state) noexcept
{
    const auto capabilities=RevolverCapabilities(resources);
    const auto index=static_cast<unsigned>(event.kind);
    if(guest::cof::detail::messageOpen||index>=static_cast<unsigned>(RevolverEffect::Count)||!(capabilities&(1u<<index))||
        !player.pev||!weapon.pev||OriginalWeapon(weapon)!=21||
        !std::isfinite(state.cylinderAngle)||state.cylinderAngle<0||state.cylinderAngle>360||
        !std::isfinite(state.cylinderGate))return false;
    auto* owner=player.edict();auto* item=weapon.edict();if(!owner||!item)return false;
    Lease lease{*this,binding,player,weapon,owner,item,owner->serialnumber,item->serialnumber,state.cylinderAngle};
    if(!Active(&lease))return false;
    const PlaySfxCall call{&lease,Active};
    switch(event.kind)
    {
    case RevolverEffect::EjectEvent:
        return Sent(PlayRevolverShellEvent(player,{event.first,event.a,event.value},resources.events[2],call))&&Active(&lease);
    case RevolverEffect::FirstDraw:
        // The actual typed runtime updates this original21 flag before its
        // external callback. Never write the independent Glock flag here.
        return state.firstDrawn&&Active(&lease);
    case RevolverEffect::AmmoX:
    {
        if(event.value<0||event.value>=MAX_AMMO_SLOTS||event.value>255||event.value!=weapon.m_iPrimaryAmmoType||
            event.other!=std::clamp(player.m_rgAmmo[event.value],0,254))return false;
        guest::cof::detail::MessagePacket packet;
        MESSAGE_BEGIN(MSG_ONE,ammoX_,nullptr,owner);
        WRITE_BYTE(event.value);WRITE_BYTE(event.other);MESSAGE_END();
        return Active(&lease);
    }
    case RevolverEffect::Cylinder:
    {
        if(std::bit_cast<std::uint32_t>(event.a)!=std::bit_cast<std::uint32_t>(state.cylinderAngle)||
            (event.enabled&&std::bit_cast<std::uint32_t>(event.b)!=std::bit_cast<std::uint32_t>(state.cylinderGate)))return false;
        const auto slot=Slot(player);if(!slot)return false;
        const auto& client=clients_[slot];
        const bool same=client.active&&client.weapon.Get<CBasePlayerWeapon>()==&weapon&&
            client.ownerSerial==lease.ownerSerial&&client.key.sessionEpoch==binding.map&&
            client.key.lifeEpoch==binding.life&&client.model==binding.model;
        if(!same)return false; // Actual Deploy/Aim must establish this snapshot first.
        return Presentation(lease,client.aimEnabled,client.fovMode,client.zoomSpeedEighths*.125F,true);
    }
    default:break;
    }
    // These original events have identical field meanings. Distinct shell,
    // cylinder, first-draw and reload handling above never enter this mapping.
    constexpr std::array mapping{GlockEffect::DeployPrelude,GlockEffect::ReserveHud,GlockEffect::InventoryChanged,
        GlockEffect::ViewAnimation,GlockEffect::EmptyClick,GlockEffect::Shell,GlockEffect::Gunsmoke,
        GlockEffect::FireEvent,GlockEffect::Recoil,GlockEffect::Shake,GlockEffect::Aim,GlockEffect::FireSound,
        GlockEffect::ShotState,GlockEffect::ShotLatch,GlockEffect::PickupSound,GlockEffect::DeployReset,
        GlockEffect::Sleeve,GlockEffect::AmmoPickupSound,GlockEffect::Body,GlockEffect::FirstDraw};
    static_assert(mapping.size()==static_cast<unsigned>(RevolverEffect::ReloadStage));
    if(index>=mapping.size())return false;
    const GlockEvent translated{mapping[index],event.first,event.second,event.a,event.b,event.c,
        event.value,event.other,event.enabled,event.modelString};
    return DispatchWeapon({resources.generation,resources.viewModelString,{resources.events[0],resources.events[1]},
        resources.models[unsigned(RevolverModel::Shell)]},binding,player,weapon,translated,21,
        RevolverLocalWaves[0].original,capabilities,state.cylinderAngle);
}
bool GlockEffects::DispatchWeapon(const WeaponResources& resources,const OwnerBinding& binding,
    CBasePlayer& player,CBasePlayerWeapon& weapon,const GlockEvent& event,unsigned originalWeapon,
    const char* fireSound,std::uint32_t capabilities,std::optional<float> revolverAngle) noexcept
{
    if (resources.generation != authority_.generation || resources.events!=resources_.events || !player.pev || !weapon.pev ||
        OriginalWeapon(weapon)!=originalWeapon ||
        static_cast<unsigned>(event.kind) >= static_cast<unsigned>(GlockEffect::Count) ||
        !(capabilities & Bit(event.kind))) return false;
    auto* owner = player.edict(); auto* item = weapon.edict();
    if (!owner || !item) return false;
    Lease lease{*this, binding, player, weapon, owner, item, owner->serialnumber, item->serialnumber,revolverAngle};
    if (!Active(&lease)) return false;
    const PlaySfxCall call{&lease, Active};
    const ControlCall control{&lease, Active};
    switch (event.kind)
    {
    case GlockEffect::DeployPrelude:
        // Original empty CustomSlv/CustomFng/CustomGlv branch. Host clothing
        // overrides are expressly unsupported, not reported as applied.
        clients_[binding.slot].clothingInitialized = true;
        return Presentation(lease, false, 0, 0);
    case GlockEffect::FirstDraw:
        if (!policy_.nativeMultiplayer || !g_pGameRules || !g_pGameRules->IsMultiplayer() || !Active(&lease)) return false;
        return binding.runtime->FirstDraw(binding, true) && Active(&lease);
    case GlockEffect::Tutorial:
        // Original CHalfLifeMultiplay v20 returns true before tutorial history
        // or messages. This is the proven multiplayer branch only.
        return policy_.nativeMultiplayer && g_pGameRules && g_pGameRules->IsMultiplayer() && Active(&lease);
    case GlockEffect::ReserveHud:
        if (weapon.m_iPrimaryAmmoType < 0 || weapon.m_iPrimaryAmmoType >= MAX_AMMO_SLOTS ||
            event.value != player.m_rgAmmo[weapon.m_iPrimaryAmmoType]) return false;
        [[fallthrough]];
    case GlockEffect::InventoryChanged:
        player.TabulateAmmo();
        if (!Active(&lease)) return false;
        // CoF's independent ammo is absent from native CS's ten legacy caches.
        // Refresh the actual expanded HUD at this original callback stage.
        CSOL_UpdateWeaponInventory(&player);
        return Active(&lease);
    case GlockEffect::ViewAnimation:
        if (event.value < 0 || event.value > (originalWeapon==21?20:originalWeapon==7?33:42)) return false;
        weapon.SendWeaponAnim(event.value, 0);
        return Active(&lease);
    case GlockEffect::DeployReset:
        return SendWeaponDeploy(player, control_, control) == ControlStatus::Sent && Active(&lease);
    case GlockEffect::Body:
        return event.value >= 0 && event.value <= 255 &&
            SendWeaponBody(player, static_cast<std::uint8_t>(event.value), control_, control) == ControlStatus::Sent && Active(&lease);
    case GlockEffect::Aim:
        return Presentation(lease, event.enabled, event.value, event.a);
    case GlockEffect::Shell:
        return originalWeapon==7&&event.value==resources.shellModel&&event.other==1&&
            Sent(EjectWeaponShell({event.first,event.second,event.a,event.value,event.other},call));
    case GlockEffect::Gunsmoke:
        if (!event.enabled || event.other != 0 || event.modelString != resources.viewModelString ||
            event.value <= 0 || event.value > INT16_MAX || event.value != ENTINDEX(item) || !Active(&lease)) return false;
        return Sent(SendLocalGunsmoke(player, static_cast<std::uint8_t>(originalWeapon), static_cast<std::int16_t>(event.value), particle_, call));
    case GlockEffect::FireEvent:
    {
        if (event.value != static_cast<int>(originalWeapon) || (event.other != 0 && event.other != 1)) return false;
        const int entity = ENTINDEX(item);
        if (!Active(&lease) || entity <= 0 || entity > INT16_MAX) return false;
        // Original client ignores iparam2. Namespaced host transport supplies
        // the real weapon entity; CS curstate.weaponmodel stays a model index.
        Vector origin = event.first, angles = event.second;
        PLAYBACK_EVENT_FULL(event.other, owner, resources.events[0], 0,
            origin, angles, 0, 0, originalWeapon, entity, 0, 0);
        return Active(&lease);
    }
    case GlockEffect::Recoil:
        return Sent(CooperativeViewRecoil(player, event.first, resources.events[1], call));
    case GlockEffect::Shake:
        return Sent(ShakePlayers({player.pev->origin, event.a, event.b, event.c, float(event.value)}, shake_, call).status);
    case GlockEffect::ShotState:
        return event.a == (originalWeapon==21?4.0F:3.0F) &&
            SendShotState(player,static_cast<std::uint8_t>(event.a),control_,control)==ControlStatus::Sent;
    case GlockEffect::FireSound:
    {
        const PlaySfxRequest request{fireSound, {}, 0, 1, true, PlaySfxRules::Cooperative};
        return SendPlaySfx(player, request, sound_, call).status == PlaySfxStatus::Sent;
    }
    default: return false;
    }
}
}
