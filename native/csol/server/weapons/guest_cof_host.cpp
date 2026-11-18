// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "guest_cof_host.hpp"
#include "guest_cof_effects.hpp"
#include "guest_cof_p345_native_effects.hpp"
#include "guest_cof_revolver_native_effects.hpp"
#include "guest_cof_vp70_native_effects.hpp"
#include "guest_cof_tmp_native_effects.hpp"
#include "guest_cof_mp5_native_effects.hpp"
#include "guest_cof_m16_native_effects.hpp"
#include "guest_cof_g43_native_effects.hpp"
#include "guest_cof_shotgun_native_effects.hpp"
#include "guest_cof_pushback.hpp"
#include "native/guest/cof/first_draw_lifecycle.hpp"
#include "native/guest/cof/server/inventory_recovery.hpp"
#include "guest_cof_sound_field.hpp"
#include "primary_purchase.h"
#include "../characters.h"
#include "../scenario/core/game_rules_gate.h"
#include "../scenario/core/scenario_runtime.h"
#include "../scenario/core/scenario_session.h"
#include "../custom/action_stamina.h"
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
constexpr auto TMPId = TMPHostId;
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
        Map* actionMap{};
        PlayerActionBinding action;
        std::shared_ptr<CCSOLScenarioRuntime> actionRuntime;
        std::uint64_t actionLife{};
        std::uint64_t actionRound{};
        int actionSpawn{};
        NativeOwnerPersistentState persistent{};
        ShotgunOwnerState shotgun{};
        RevolverLifecycle revolver;
        FirstDrawLifecycle vp70FirstDraw,tmpFirstDraw;
        bool persistentCaptured{};
        bool shotgunCaptured{};
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
    PlayerActionRuntime actions;
    bool m16Enabled{},g43Enabled{},shotgunEnabled{};
    std::uint32_t configuredWeapons{};
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
std::shared_ptr<CCSOLScenarioRuntime> Scenario(Map& map) noexcept
{
    if (!Current(&map)) return {};
    CSOLScenarioSdkScope scope(map.rules);
    if (!scope.Current()) return {};
    return CSOL_ScenarioRuntimeForAccess(scope.EntityAccess().get());
}
bool ActionCurrent(void* context,CBasePlayer& player,std::uint64_t map,std::uint64_t life) noexcept
{
    auto& row=*static_cast<Map::Player*>(context);auto* owner=row.actionMap;
    return owner&&Current(owner)&&owner->epoch==map&&row.actionLife==life&&Entity(player,row)&&
        row.actionRuntime&&!row.actionRuntime->Closing()&&row.actionRuntime->session&&
        row.actionRuntime->session->Map().RoundGeneration()==row.actionRound&&
        row.actionSpawn==player.m_iNumSpawns&&player.m_iTeam==CT&&!row.spawning&&
        player.pev->deadflag==DEAD_NO&&player.pev->health>0;
}
bool ActionCallCurrent(void* context) noexcept
{
    auto& row=*static_cast<Map::Player*>(context);
    return row.object&&row.actionMap&&ActionCurrent(context,*row.object,row.actionMap->epoch,row.actionLife);
}
bool SharedStamina(void* context,float& amount,bool& infinite) noexcept
{
    auto& row=*static_cast<Map::Player*>(context);
    if(!ActionCallCurrent(context))return false;
    return CSOL_ReadGuestCofPushbackStamina(row.actionRuntime,*row.object,amount,infinite)&&ActionCallCurrent(context);
}
bool SharedStaminaDebit(void* context,float amount) noexcept
{
    auto& row=*static_cast<Map::Player*>(context);
    if(!ActionCallCurrent(context))return false;
    return CSOL_DebitGuestCofPushbackStamina(row.actionRuntime,*row.object,amount)&&ActionCallCurrent(context);
}
bool DispatchAction(void* context,CBasePlayer& player,PlayerThinkKind kind) noexcept
{
    auto& row=*static_cast<Map::Player*>(context);
    if(kind!=PlayerThinkKind::Pushback||!ActionCallCurrent(context))return false;
    auto& map=*row.actionMap;MapCall call(map);
    const auto runtime=row.actionRuntime;auto* task=row.action.Think();
    const auto* definition=task?FindFirearm(task->SourceHostId()):nullptr;
    const auto* rules=definition?map.damage.Find(definition->identity):nullptr;
    if(!rules)return false;
    std::array<const char*,2> sounds{};unsigned short event{};
    if(definition->identity==Firearm::M16)
    {
        const auto* resources=host.runtime.PreparedM16Resources();
        if(!resources||resources->generation!=map.epoch)return false;
        sounds={M16Sounds[unsigned(M16Sound::PushSwing)].host,M16Sounds[unsigned(M16Sound::PushHit)].host};
        event=resources->events[1];
    }
    else if(definition->identity==Firearm::G43)
    {
        const auto* resources=host.runtime.PreparedG43Resources();
        if(!resources||resources->generation!=map.epoch)return false;
        sounds={G43Sounds[unsigned(G43Sound::PushSwing)].host,G43Sounds[unsigned(G43Sound::PushHit)].host};
        event=resources->events[1];
    }
    else if(definition->identity==Firearm::Shotgun)
    {
        const auto* resources=host.runtime.PreparedShotgunResources();
        if(!resources||resources->generation!=map.epoch)return false;
        sounds={ShotgunSounds[unsigned(ShotgunSound::PushSwing)].host,ShotgunSounds[unsigned(ShotgunSound::PushHit)].host};
        event=resources->events[1];
    }
    else return false;
    const float damage=rules->melee;
    struct Lease
    {
        Map::Player& row;std::uint64_t life;
        static bool Current(void* p) noexcept
        {auto& c=*static_cast<Lease*>(p);return c.row.actionLife==c.life&&ActionCallCurrent(&c.row);}
        static bool Stamina(void* p,float& amount,bool& infinite) noexcept
        {auto& c=*static_cast<Lease*>(p);return Current(p)&&SharedStamina(&c.row,amount,infinite)&&Current(p);}
        static bool Debit(void* p,float amount) noexcept
        {auto& c=*static_cast<Lease*>(p);return Current(p)&&SharedStaminaDebit(&c.row,amount)&&Current(p);}
    } lease{row,row.actionLife};
    const NativePushbackHost push{{&lease,Lease::Current},Lease::Stamina,Lease::Debit,
        {sounds[0],sounds[1]},event,damage,false};
    const auto result=NativeLongGunPushback(player,runtime,push);
    return ActionCallCurrent(context)&&(result==PushbackResult::Applied||result==PushbackResult::Ineligible);
}
bool PrepareActions(void* context,CBasePlayer& player,std::uint64_t map,std::uint64_t life,
    PlayerActionBinding& output) noexcept
{
    auto& owner=*static_cast<Map*>(context);const auto slot=Slot(player);
    if(!slot||!Current(&owner)||owner.epoch!=map||!owner.actions.Current())return false;
    auto& row=owner.players[slot];
    if(!Entity(player,row)||row.spawning)return false;
    if(!owner.m16Enabled&&!owner.g43Enabled&&!owner.shotgunEnabled)return false;
    const auto runtime=Scenario(owner);
    if(!runtime||!runtime->session)return false;
    row.actionRuntime=runtime;row.actionRound=runtime->session->Map().RoundGeneration();
    row.actionMap=&owner;row.actionLife=life;row.actionSpawn=player.m_iNumSpawns;
    output=owner.actions.PrepareShared(slot,player,life,
        {&row,ActionCurrent,DispatchAction,SharedStamina,PlayerThinkBit(PlayerThinkKind::Pushback)});
    if(!output.Current())return false;
    row.action=output;return true;
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
    if(TMPBackends backend{};host.runtime.Backend(player,backend))
    {output={backend.context,backend.current};return true;}
    if(TMPBackends backend{};host.runtime.MP5Backend(player,backend))
    {output={backend.context,backend.current};return true;}
    if(M16Backends backend{};host.runtime.Backend(player,backend))
    {output={backend.context,backend.current};return true;}
    if(G43Backends backend{};host.runtime.Backend(player,backend))
    {output={backend.context,backend.current};return true;}
    if(ShotgunBackends backend{};host.runtime.Backend(player,backend))
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
bool PrepareTMP(void* context,const TMPResources& resources,TMPPrepared& output) noexcept
{
    auto& map=*static_cast<Map*>(context);
    return Current(&map)&&map.ready&&map.effects.FillPrepared(resources,output);
}
bool PrepareMP5(void* context,const MP5Resources& resources,TMPPrepared& output) noexcept
{
    auto& map=*static_cast<Map*>(context);
    return Current(&map)&&map.ready&&map.effects.FillPrepared(resources,output);
}
bool PrepareM16(void* context,const M16Resources& resources,M16Prepared& output) noexcept
{
    auto& map=*static_cast<Map*>(context);
    if(!Current(&map)||!map.ready||!map.actions.Current()||!map.effects.FillPrepared(resources,output))return false;
    const auto* rules=map.damage.Find(Firearm::M16);if(!rules)return false;
    output.csPenetration=rules->penetration;
    output.pushbackReady=g_engfuncs.pfnFindEntityInSphere&&g_engfuncs.pfnMakeVectors&&
        g_engfuncs.pfnPlaybackEvent&&g_engfuncs.pfnEmitSound;
    return output.pushbackReady;
}
bool PrepareG43(void* context,const G43Resources& resources,G43Prepared& output) noexcept
{
    auto& map=*static_cast<Map*>(context);
    if(!Current(&map)||!map.ready||!map.actions.Current()||!map.effects.FillPrepared(resources,output))return false;
    const auto* rules=map.damage.Find(Firearm::G43);if(!rules)return false;
    output.csPenetration=rules->penetration;
    output.pushbackReady=g_engfuncs.pfnFindEntityInSphere&&g_engfuncs.pfnMakeVectors&&
        g_engfuncs.pfnPlaybackEvent&&g_engfuncs.pfnEmitSound;
    return output.pushbackReady;
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
bool PrepareShotgun(void* context,const ShotgunResources& resources,ShotgunPrepared& output) noexcept
{
    auto& map=*static_cast<Map*>(context);
    if(!Current(&map)||!map.ready||!map.actions.Current()||!map.effects.FillPrepared(resources,output))return false;
    output.pushbackReady=g_engfuncs.pfnFindEntityInSphere&&g_engfuncs.pfnMakeVectors&&
        g_engfuncs.pfnPlaybackEvent&&g_engfuncs.pfnEmitSound;
    return output.pushbackReady;
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
bool EffectTMP(void* context,const TMPResources& resources,const OwnerBinding& binding,
    CBasePlayer& player,CBasePlayerWeapon& weapon,const TMPEvent& event) noexcept
{
    auto& map=*static_cast<Map*>(context);MapCall call(map);
    const auto slot=Slot(player);
    if(!slot||!Current(&map)||!binding.runtime||!binding.runtime->Current(binding)||
        !Entity(player,map.players[slot]))return false;
    bool firstDrawn{};
    if(event.kind==TMPEffect::FirstDraw)
    {
        const auto key=Connection(map,slot,player);
        if(!host.runtime.PersistentTMPFirstDraw(player,firstDrawn)||!firstDrawn||!Current(&map)||
            !map.players[slot].tmpFirstDraw.MarkDrawn({key.map,key.slot,key.serial,key.userid,key.player}))return false;
    }
    if(event.kind==TMPEffect::ShotLatch)
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
bool EffectMP5(void* context,const MP5Resources& resources,const OwnerBinding& binding,
    CBasePlayer& player,CBasePlayerWeapon& weapon,const TMPEvent& event) noexcept
{
    auto& map=*static_cast<Map*>(context);MapCall call(map);
    const auto slot=Slot(player);
    if(!slot||!Current(&map)||!binding.runtime||!binding.runtime->Current(binding)||
        !Entity(player,map.players[slot]))return false;
    if(event.kind==TMPEffect::ShotLatch)
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
bool EffectM16(void* context,const M16Resources& resources,const OwnerBinding& binding,
    CBasePlayer& player,CBasePlayerWeapon& weapon,const M16Event& event) noexcept
{
    auto& map=*static_cast<Map*>(context);MapCall call(map);
    const auto slot=Slot(player);
    if(!slot||!Current(&map)||!binding.runtime||!binding.runtime->Current(binding)||!Entity(player,map.players[slot]))return false;
    if(event.kind==M16Effect::ShotLatch)
    {
        const EHANDLE item(&weapon);
        struct Call{Map& map;OwnerBinding binding;CBasePlayerWeapon& weapon;const EHANDLE& item;CBasePlayer& player;};
        Call state{map,binding,weapon,item,player};
        return map.sounds.ShotLatch(player,event.value,event.a,{&state,[](void* p) noexcept {
            auto& c=*static_cast<Call*>(p);
            return Current(&c.map)&&c.binding.runtime->Current(c.binding)&&c.item.Get<CBasePlayerWeapon>()==&c.weapon&&
                c.weapon.m_pPlayer==&c.player&&c.player.m_pActiveItem==&c.weapon;
        }});
    }
    return map.effects.Dispatch(resources,binding,player,weapon,event);
}
bool EffectG43(void* context,const G43Resources& resources,const OwnerBinding& binding,
    CBasePlayer& player,CBasePlayerWeapon& weapon,const G43Event& event) noexcept
{
    auto& map=*static_cast<Map*>(context);MapCall call(map);
    const auto slot=Slot(player);
    if(!slot||!Current(&map)||!binding.runtime||!binding.runtime->Current(binding)||!Entity(player,map.players[slot]))return false;
    if(event.kind==G43Effect::ShotLatch)
    {
        const EHANDLE item(&weapon);
        struct Call{Map& map;OwnerBinding binding;CBasePlayerWeapon& weapon;const EHANDLE& item;CBasePlayer& player;};
        Call state{map,binding,weapon,item,player};
        return map.sounds.ShotLatch(player,event.value,event.a,{&state,[](void* p) noexcept {
            auto& c=*static_cast<Call*>(p);
            return Current(&c.map)&&c.binding.runtime->Current(c.binding)&&c.item.Get<CBasePlayerWeapon>()==&c.weapon&&
                c.weapon.m_pPlayer==&c.player&&c.player.m_pActiveItem==&c.weapon;
        }});
    }
    return map.effects.Dispatch(resources,binding,player,weapon,event);
}
bool EffectShotgun(void* context,const ShotgunResources& resources,const OwnerBinding& binding,
    CBasePlayer& player,CBasePlayerWeapon& weapon,const ShotgunEvent& event) noexcept
{
    auto& map=*static_cast<Map*>(context);MapCall call(map);const auto slot=Slot(player);
    return slot&&Current(&map)&&binding.runtime&&binding.runtime->Current(binding)&&
        Entity(player,map.players[slot])&&map.effects.Dispatch(resources,binding,player,weapon,event);
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
FirearmAdmission Capability(CBasePlayer& player,const Map& map) noexcept
{
    if (!CSOL_CanUseExtendedWeapons(&player) || !g_engfuncs.pfnInfoKeyValue ||
        !g_engfuncs.pfnGetInfoKeyBuffer) return {};
    auto* info=GET_INFO_BUFFER(player.edict());if(!info)return {};
    const auto* protocol=GET_KEY_VALUE(info,"cof_protocol");
    if(!protocol||Q_strcmp(protocol,"1"))return {};
    const auto* weapons=GET_KEY_VALUE(info,"cof_weapons");
    auto result=ParseFirearmAdmission("1",weapons?weapons:"");
    result.weapons&=map.configuredWeapons;
    return result;
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
    ShotgunOwnerState shotgun{};
    bool shotgunCaptured=spawn;
    if(!spawn)
    {
        if(row.shotgunCaptured){shotgun=row.shotgun;shotgunCaptured=true;}
        else shotgunCaptured=host.runtime.PersistentState(player,shotgun);
    }
    const auto admission=Capability(player,*map);
    const bool capable = admission.weapons!=0;
    if (!Current(map) || owner.Get<CBasePlayer>() != &player || row.revision != previousRevision) return;
    const int userid = GETPLAYERUSERID(player.edict());
    if (!Current(map) || owner.Get<CBasePlayer>() != &player || row.revision != previousRevision) return;
    if (!spawn && row.prepared && Entity(player, row) && row.userid == userid &&
        row.modelIndex == player.pev->modelindex && row.modelString == static_cast<int>(player.pev->model) &&
        row.capable == capable&&row.weapons==admission.weapons) return;
    if (row.revision == UINT64_MAX) return;
    // A previously admitted same-life 870 must retain its actual reload clock.
    // New lives and a newly admitted weapon have no earlier clock to restore.
    if(!spawn&&(row.weapons&FirearmCapability(Firearm::Shotgun))&&
        (admission.weapons&FirearmCapability(Firearm::Shotgun))&&!shotgunCaptured)return;
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
    row.shotgun = shotgun; row.shotgunCaptured = shotgunCaptured;
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
        if(admission.weapons&FirearmCapability(Firearm::TMP))
        {
            const auto* tmp=map->damage.Find(Firearm::TMP);bool firstDrawn{};
            const auto key=Connection(*map,slot,player);
            if(!tmp||tmp->shot!=std::trunc(tmp->shot)||tmp->melee!=std::trunc(tmp->melee)||
                !row.tmpFirstDraw.Bind({key.map,key.slot,key.serial,key.userid,key.player},firstDrawn))return;
            request.tmp=GlockPlayerPreparation::TMPPreparation{{true,int(tmp->shot),int(tmp->melee),90},firstDrawn};
        }
        if(admission.weapons&FirearmCapability(Firearm::MP5))
        {
            const auto* mp5=map->damage.Find(Firearm::MP5);
            if(!mp5||mp5->shot!=std::trunc(mp5->shot)||mp5->melee!=std::trunc(mp5->melee))return;
            request.mp5Rules=NativeOwnerRules{true,int(mp5->shot),int(mp5->melee),90};
        }
        if(admission.weapons&FirearmCapability(Firearm::M16))
        {
            const auto* m16=map->damage.Find(Firearm::M16);
            if(!m16||m16->shot!=std::trunc(m16->shot)||m16->melee!=std::trunc(m16->melee))return;
            request.m16Rules=NativeOwnerRules{true,int(m16->shot),int(m16->melee),60};
        }
        if(admission.weapons&FirearmCapability(Firearm::G43))
        {
            const auto* g43=map->damage.Find(Firearm::G43);
            if(!g43||g43->shot!=std::trunc(g43->shot)||g43->melee!=std::trunc(g43->melee))return;
            request.g43Rules=NativeOwnerRules{true,int(g43->shot),int(g43->melee),30};
        }
        if(admission.weapons&FirearmCapability(Firearm::Shotgun))
        {
            const auto* rules=map->damage.Find(Firearm::Shotgun);
            if(!rules||rules->shot!=std::trunc(rules->shot)||rules->melee!=std::trunc(rules->melee))return;
            request.shotgun=GlockPlayerPreparation::ShotgunPreparation{
                {true,int(rules->shot),int(rules->melee),15},shotgun};
        }
        std::string error;
        const bool prepared = spawn ? host.runtime.PlayerSpawned(player, request, error) :
            host.runtime.PlayerModelChanged(player, request, error);
        if (!current()) return;
        row.prepared = prepared;
        if(prepared)
        {
            row.shotgunCaptured=false;
            const auto recovery=RecoverRetainedGuestInventory(player,current,
                [](const CBasePlayerWeapon& item) noexcept {return CSOL_IsGuestCof(item);});
            if (!recovery.current) return;
            if (recovery.deferred)
                ALERT(at_warning,"[CoF] %u retained weapon bindings deferred; other guest weapons remain available\n",
                    recovery.deferred);
            if (!recovery.complete)
                ALERT(at_warning,"[CoF] Retained inventory traversal exceeded the entity bound\n");
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
    if(!modelChanging){row.action.Retire();row.action={};row.actionRuntime.reset();}
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
    map->actions.Reset();
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
        if(map->damage.Find(Firearm::M16)||map->damage.Find(Firearm::G43)||map->damage.Find(Firearm::Shotgun))
        {
            const auto factory=Sys_GetFactory("swds.dll");
            auto* stage=factory?static_cast<goldcraft::IPlayerThinkStage*>(factory(goldcraft::PlayerThinkStageVersion,nullptr)):nullptr;
            const bool installed=stage&&map->actions.Install(*stage,map->epoch);
            map->m16Enabled=installed&&map->damage.Find(Firearm::M16);
            map->g43Enabled=installed&&map->damage.Find(Firearm::G43);
            map->shotgunEnabled=installed&&map->damage.Find(Firearm::Shotgun);
            if(!installed)ALERT(at_error,"[CoF] Long guns disabled: actual player Think stage is unavailable\n");
        }
        for(const auto& definition:Firearms)
            if((definition.identity!=Firearm::M16||map->m16Enabled)&&
                (definition.identity!=Firearm::G43||map->g43Enabled)&&
                (definition.identity!=Firearm::Shotgun||map->shotgunEnabled)&&map->damage.Find(definition.identity))
                map->configuredWeapons|=FirearmCapability(definition.identity);
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
        const bool tmpEnabled=current.damage.Find(Firearm::TMP)!=nullptr;
        if(tmpEnabled)callbacks.tmp={PrepareTMP,EffectTMP,TMPRequiredEffects&~TMPNativeSoundEffects};
        const bool mp5Enabled=current.damage.Find(Firearm::MP5)!=nullptr;
        if(mp5Enabled)callbacks.mp5={PrepareMP5,EffectMP5,MP5RequiredEffects&~MP5NativeSoundEffects};
        if(current.m16Enabled)callbacks.m16={PrepareM16,EffectM16,PrepareActions,
            M16RequiredEffects&~(M16NativeSoundEffects|(1u<<unsigned(M16Effect::FireSound))),PlaySfxRules::Cooperative};
        if(current.g43Enabled)callbacks.g43={PrepareG43,EffectG43,PrepareActions,
            G43RequiredEffects&~(G43NativeSoundEffects|(1u<<unsigned(G43Effect::FireSound))),PlaySfxRules::Cooperative};
        constexpr auto shotgunRuntimeEffects=ShotgunNativeSoundEffects|(1u<<unsigned(ShotgunEffect::FireSound))|
            (1u<<unsigned(ShotgunEffect::ReloadStage))|(1u<<unsigned(ShotgunEffect::AmmoWarning));
        if(current.shotgunEnabled)callbacks.shotgun={PrepareShotgun,EffectShotgun,PrepareActions,
            ShotgunRequiredEffects&~shotgunRuntimeEffects,PlaySfxRules::Cooperative};
        callbacks.weaponCurrent=[](void* p,CBasePlayer& player,int id) noexcept {
            auto& map=*static_cast<Map*>(p);const auto index=Slot(player);const auto* definition=FindFirearm(id);
            return definition&&index&&Current(&map)&&Entity(player,map.players[index])&&
                (map.players[index].weapons&FirearmCapability(definition->identity));
        };
        const auto prepared = host.runtime.BeginMap(callbacks);
        current.runtimeIssued = host.runtime.LastIssuedMapEpoch() == current.epoch;
        if (!prepared || !Current(&current))
        { CSOL_EndGuestCofMap(); ALERT(at_error, "[CoF] Glock resource preparation failed: %s\n", prepared.path ? prepared.path : "map authority"); return; }
        const auto* resources = host.runtime.Resources();
        const ResourceAuthority authority{&current, Current, current.epoch};
        const auto* revolverResources=host.runtime.PreparedRevolverResources();
        const auto* tmpResources=host.runtime.PreparedTMPResources();
        const auto* mp5Resources=host.runtime.PreparedMP5Resources();
        const auto* m16Resources=host.runtime.PreparedM16Resources();
        const auto* g43Resources=host.runtime.PreparedG43Resources();
        const auto* shotgunResources=host.runtime.PreparedShotgunResources();
        if (!resources || !revolverResources || !current.effects.Prepare(authority, *resources, {true, true, true}) ||
            !current.sounds.BeginMap(authority) ||
            ((current.effects.Capabilities() | EffectBit(GlockEffect::ShotLatch)) & HostEffects) != HostEffects||
            ((current.effects.P345Capabilities()|P345NativeSoundEffects|(1u<<unsigned(P345Effect::ShotLatch)))&P345RequiredEffects)!=P345RequiredEffects||
            ((current.effects.VP70Capabilities()|VP70NativeSoundEffects|(1u<<unsigned(VP70Effect::ShotLatch)))&VP70RequiredEffects)!=VP70RequiredEffects||
            (tmpEnabled&&(!tmpResources||((current.effects.TMPCapabilities()|TMPNativeSoundEffects|(1u<<unsigned(TMPEffect::ShotLatch)))&TMPRequiredEffects)!=TMPRequiredEffects))||
            (mp5Enabled&&(!mp5Resources||((current.effects.MP5Capabilities()|MP5NativeSoundEffects|(1u<<unsigned(TMPEffect::ShotLatch)))&MP5RequiredEffects)!=MP5RequiredEffects))||
            (current.m16Enabled&&(!m16Resources||((current.effects.M16Capabilities()|M16NativeSoundEffects|
                (1u<<unsigned(M16Effect::FireSound))|(1u<<unsigned(M16Effect::ShotLatch)))&M16RequiredEffects)!=M16RequiredEffects))||
            (current.g43Enabled&&(!g43Resources||((current.effects.G43Capabilities()|G43NativeSoundEffects|
                (1u<<unsigned(G43Effect::FireSound))|(1u<<unsigned(G43Effect::ShotLatch)))&G43RequiredEffects)!=G43RequiredEffects))||
            (current.shotgunEnabled&&(!shotgunResources||
                ((current.effects.ShotgunCapabilities()|shotgunRuntimeEffects)&ShotgunRequiredEffects)!=ShotgunRequiredEffects))||
            ((current.effects.RevolverCapabilities(*revolverResources)|RevolverNativeSoundEffects|(1u<<unsigned(RevolverEffect::ShotLatch)))&RevolverRequiredEffects)!=RevolverRequiredEffects)
        { CSOL_EndGuestCofMap(); ALERT(at_error, "[CoF] Actual Glock consumers were not prepared\n"); return; }
        for(const auto& definition:Firearms)
        {
            if(!(current.configuredWeapons&FirearmCapability(definition.identity)))continue;
            if(definition.identity==Firearm::MP5&&!host.runtime.PreparedMP5Resources())continue;
            if(definition.identity==Firearm::TMP&&!tmpEnabled)continue;
            const auto hud=PrepareFirearmPresentationResources(authority,definition.identity);
            if(!hud||!Current(&current))
            {CSOL_EndGuestCofMap();ALERT(at_error,"[CoF] Guest HUD resource preparation failed: %s\n",hud.path?hud.path:"map authority");return;}
        }
        current.ready = true;
        for(const auto& definition:Firearms)
        {
            if(!(current.configuredWeapons&FirearmCapability(definition.identity)))continue;
            if(definition.identity==Firearm::MP5&&!host.runtime.PreparedMP5Resources())continue;
            if(definition.identity==Firearm::TMP&&!tmpEnabled)continue;
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
    row.shotgun = {}; row.shotgunCaptured = false;
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
    ShotgunOwnerState shotgun{};
    const bool shotgunCaptured=host.runtime.PersistentState(player,shotgun);
    if(!Current(map)||entity.Get<CBasePlayer>()!=&player)return;
    if(shotgunCaptured){row.shotgun=shotgun;row.shotgunCaptured=true;}
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
    const auto slot=Slot(player);auto* map=host.active;
    if(slot&&map&&Current(map)&&map->players[slot].action.Current())map->players[slot].action.BeginCommand();
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
    const auto slot=Slot(player);
    if(slot&&map->players[slot].action.Current())map->players[slot].action.EndSharedCommand();
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
    if (!FindFirearm(id) || !host.active || !Current(host.active)) return false;
    const auto slot = Slot(player);
    if (!slot || !host.active->players[slot].prepared) return false;
    if(id==P345Id){P345Backends backend{};return host.runtime.Backend(player,backend);}
    if(id==RevolverId){RevolverBackends backend{};return host.runtime.Backend(player,backend);}
    if(id==VP70Id){VP70Backends backend{};return host.runtime.Backend(player,backend);}
    if(id==TMPId){TMPBackends backend{};return host.runtime.Backend(player,backend);}
    if(id==MP5HostId){TMPBackends backend{};return host.runtime.MP5Backend(player,backend);}
    if(id==M16HostId){M16Backends backend{};return host.runtime.Backend(player,backend);}
    if(id==G43HostId){G43Backends backend{};return host.runtime.Backend(player,backend);}
    if(id==ShotgunHostId){ShotgunBackends backend{};return host.runtime.Backend(player,backend);}
    GlockBackends backend{};return host.runtime.Backend(player,backend);
}
bool CSOL_GuestCofPreserveReload(CBasePlayer& player,int playerAnimation,int& desiredSequence) noexcept
{
    auto* map=host.active;
    if(!map||!Current(map)||!map->ready)return false;
    MapCall call(*map);
    return host.runtime.PreserveRevolverReload(player,playerAnimation,desiredSequence)||
        host.runtime.PreserveShotgunReload(player,playerAnimation,desiredSequence);
}
bool CSOL_IsGuestCof(const CBasePlayerWeapon& weapon) noexcept
{
    if(!weapon.pev)return false;
    return (weapon.m_iId==GlockId&&dynamic_cast<const GlockWeapon*>(&weapon)&&FClassnameIs(weapon.pev,Pistols[0].classname.data()))||
        (weapon.m_iId==P345Id&&dynamic_cast<const P345Weapon*>(&weapon)&&FClassnameIs(weapon.pev,Pistols[1].classname.data()))||
        (weapon.m_iId==RevolverId&&dynamic_cast<const RevolverWeapon*>(&weapon)&&FClassnameIs(weapon.pev,Pistols[2].classname.data()))||
        (weapon.m_iId==VP70Id&&dynamic_cast<const VP70Weapon*>(&weapon)&&FClassnameIs(weapon.pev,Pistols[3].classname.data()))||
        (weapon.m_iId==TMPId&&dynamic_cast<const TMPWeapon*>(&weapon)&&FClassnameIs(weapon.pev,FindFirearm(Firearm::TMP)->classname.data()))||
        (weapon.m_iId==MP5HostId&&dynamic_cast<const MP5Weapon*>(&weapon)&&FClassnameIs(weapon.pev,FindFirearm(Firearm::MP5)->classname.data()))||
        (weapon.m_iId==M16HostId&&dynamic_cast<const M16Weapon*>(&weapon)&&FClassnameIs(weapon.pev,FindFirearm(Firearm::M16)->classname.data()))||
        (weapon.m_iId==G43HostId&&dynamic_cast<const G43Weapon*>(&weapon)&&FClassnameIs(weapon.pev,FindFirearm(Firearm::G43)->classname.data()))||
        (weapon.m_iId==ShotgunHostId&&dynamic_cast<const ShotgunWeapon*>(&weapon)&&FClassnameIs(weapon.pev,FindFirearm(Firearm::Shotgun)->classname.data()));
}
const char* CSOL_GuestCofWorldModel(const CBasePlayerItem& item) noexcept
{
    const auto* weapon = dynamic_cast<const CBasePlayerWeapon*>(&item);
    const auto* definition=weapon?FindFirearm(weapon->m_iId):nullptr;
    return definition&&CSOL_IsGuestCof(*weapon)?definition->worldModel.data():nullptr;
}
bool CSOL_DeliverGuestCof(CBasePlayer& player,int id)
{
    const auto* definition=FindFirearm(id);
    if(!definition || !CSOL_GuestCofCanUse(player,id))return false;
    const bool primary=definition->identity==Firearm::TMP||definition->identity==Firearm::MP5||
        definition->identity==Firearm::M16||definition->identity==Firearm::G43||definition->identity==Firearm::Shotgun;
    const CSOLGunPurchaseAdapter adapter{id,primary?PRIMARY_WEAPON_SLOT:PISTOL_SLOT,definition->classname.data(),CSOL_GuestCofCanUse,
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
            const auto* tmp=dynamic_cast<const TMPWeapon*>(&weapon);
            const auto* mp5=dynamic_cast<const MP5Weapon*>(&weapon);
            const auto* m16=dynamic_cast<const M16Weapon*>(&weapon);
            const auto* g43=dynamic_cast<const G43Weapon*>(&weapon);
            const auto* shotgun=dynamic_cast<const ShotgunWeapon*>(&weapon);
            return weapon.m_pPlayer==owner&&owner->m_pActiveItem==&weapon&&
                ((glock&&glock->Status()==GlockStatus::Applied)||(p345&&p345->Status()==P345Status::Applied)||
                    (revolver&&revolver->Status()==RevolverStatus::Applied)||(vp70&&vp70->Status()==VP70Status::Applied)||
                    (tmp&&tmp->Status()==TMPStatus::Applied)||(mp5&&mp5->Status()==TMPStatus::Applied)||
                    (m16&&m16->Status()==M16Status::Applied)||(g43&&g43->Status()==G43Status::Applied)||
                    (shotgun&&shotgun->Status()==ShotgunStatus::Applied));
        },true};
    return CSOL_DeliverAdaptedGun(player,adapter);
}
