#include "precompiled.h"
#include "bridge.h"
#include "objects.h"
#include "goldcraft/fluid_volumes.hpp"
#include "map_mining.h"
#include "goldcraft/endpoint.hpp"
#include "goldcraft/pairing.hpp"
#include "interface.h"
#include "goldcraft/host_entity_api.hpp"
#include "goldcraft/server_api.hpp"
#include "goldcraft/player_gameplay.hpp"
#include "goldcraft/combat.hpp"
#include "goldcraft/status_hud.hpp"
#include "goldcraft/status_attributes.hpp"
#include "goldcraft/status_environment.hpp"
#include "goldcraft/status_motion.hpp"
#include "goldcraft/status_fall.hpp"
#include "goldcraft/status_health.hpp"
#include "goldcraft/status_vision.hpp"
#include "goldcraft/status_visibility.hpp"
#include "goldcraft/status_glow.hpp"
#include "pm_movevars.h"
#include "goldcraft/native_armor.hpp"
#include "goldcraft/control_diagnostics.hpp"
#include "goldcraft/map_edit_delivery.hpp"
#include "goldcraft/host_delivery_api.hpp"
#include "goldcraft/transport_diagnostics.hpp"
#include "goldcraft/authoritative_dimensions.hpp"
#include "goldcraft/gameplay_rules.hpp"
#include "goldcraft/effect_grant.hpp"
#include "goldcraft/host_explosion.hpp"
#include "effect_service_provider.hpp"
#include <exception>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <map>

namespace {
using namespace goldcraft;
Endpoint link;
PairRegistry registry;
edict_t* edicts=nullptr;
int max_clients=0, binding_message=0, avatar_message=0,form_message=0,object_message=0;
float next_snapshot=0, next_binding=0;
std::uint64_t tick=0, connection_generation=0;
std::string previous_error;
std::ofstream server_log;
Bytes bsp_bytes;
std::uint32_t bsp_crc=0;
unsigned trace_queries=0;
std::uint64_t edit_revision_sent=0,edit_transfer=0;
float next_edit_query=0;
int edit_message=0,edit_client_cursor=0;
struct EditDelivery {
    bool enabled=false;
    std::uint64_t revision=0,transfer=0;
    float next_query=0;
    Message message{Type::map_edit_snapshot,{}};
    std::size_t offset=0;
};
std::array<EditDelivery,65> edit_deliveries;
IHostEntityPhysics* entity_physics=nullptr;
IHostDelivery* host_delivery=nullptr;
struct ControlState {
    std::uint64_t sequence=0,use_dispatch_token=0;
    bool active=false,use_requested=false,use_applied=false,use_dispatching=false;
    int saved_movetype=MOVETYPE_WALK;
    float last_update=0,last_use=0,next_use=0;
};
std::array<ControlState,65> controlled;
std::array<std::uint32_t,65> client_incarnations{};
std::array<std::uint32_t,65> player_lives{};
std::array<std::uint32_t,65> player_births{};
std::array<std::uint64_t,65> vitals_ack{};
std::uint64_t vitals_accepted=0,vitals_rejected=0;
edict_t* final_minecraft_damage=nullptr;
edict_t* minecraft_attack=nullptr;
std::array<Vec3,65> player_spawns{};
std::array<bool,65> minecraft_forms{};
std::array<float,65> next_form_change{};
FormListener form_listener=nullptr;
FormGuard form_guard=nullptr;
PlayerAttackListener player_attack_listener=nullptr;
ActivePlayerAttack active_player_attack;
unsigned native_damage_depth=0;
std::array<bool,65> form_guard_busy{};
std::array<PlayerGameplayLease,65> player_gameplay;
bool player_gameplay_supported=false;
int PlayerGameplayFlags(int slot){
    if(!edicts||slot<1||slot>max_clients||edicts[slot].free)return -1;
    const auto* pair=registry.get(slot);if(!pair||!pair->paired)return -1;
    if(!player_gameplay_supported)return -2;
    // Read native death state without invoking a reentrant Pawn/Ham virtual.
    const bool alive=edicts[slot].v.deadflag==DEAD_NO&&edicts[slot].v.health>0;
    return player_gameplay[slot].flags(registry.world(),pair->serial,player_births[slot],player_lives[slot],pair->uuid,gpGlobals->time,link.connected(),alive);
}
cvar_t default_form={"mc_default_form","1",FCVAR_SERVER,1,nullptr};
cvar_t allow_switch={"mc_allow_switch","1",FCVAR_SERVER,1,nullptr};
cvar_t round_clear_blocks={"mc_round_clear_blocks","1",FCVAR_SERVER,1,nullptr};
cvar_t death_drop_items={"mc_death_drop_items","0",FCVAR_SERVER,0,nullptr};
GameplayRules gameplay_rules;
std::uint64_t gameplay_rules_sent=0;
std::uint64_t form_changes=0,wrong_form_poses=0;
std::uint64_t object_revision=0,object_actions=0,object_damage_actions=0,object_use_actions=0;
std::uint64_t collision_sequence=0;
std::uint64_t mob_damage_event=0,mob_damage_accepted=0,mob_damage_rejected=0;
SkillDamageSnapshots skill_damage_snapshots;
std::uint64_t mining_policy_sent=0,mining_applied=0,mining_rejected=0;
bool mining_materials_sent=false;
std::array<std::uint64_t,65> mining_events{};
std::array<float,65> next_mining{}, next_mining_sample{};
float next_colliders=0;
std::array<std::map<std::uint64_t,Bytes>,65> client_colliders;
std::array<bool,65> colliders_initialized{};
std::uint64_t stale_life_poses=0;
std::array<bool,65> client_ready{};
std::array<Bytes,65> avatar_payloads;
std::uint64_t touch_dispatches=0,use_calls=0,use_presses=0,use_releases=0,host_relocations=0,stale_use_commands=0;
struct TouchCount {int serial=0;std::uint64_t count=0;};
std::map<int,TouchCount> entity_touches;
std::string UuidText(const Key& uuid) {
    static constexpr char hex[]="0123456789abcdef";
    std::string text;text.reserve(32);
    for(auto byte:uuid){text+=hex[byte>>4];text+=hex[byte&15];}
    return text;
}

void BridgeLog(const std::string& message) {
    ALERT(at_console,"[GoldCraft] %s\n",message.c_str());
    if(server_log){server_log<<"[GoldCraft] "<<message<<'\n';server_log.flush();}
}
#include "effect_host.inc"
#include "effect_publisher.inc"
#include "fluids.inc"

void AcceptNativeRelocation(unsigned slot,std::uint64_t authority=0);
void SendForm(unsigned slot);
void SendAvatar(unsigned slot,bool refresh=false);
void SendColliders(){
    std::map<std::uint64_t,Bytes> current;
    for(const auto& [slot,o]:GoldCraft_ObjectColliders()){
        Writer w;w.u32(slot);for(float v:o.min)w.f32(v);for(float v:o.max)w.f32(v);current[o.key]=std::move(w.data);
    }
    for(int viewer=1;viewer<=max_clients;++viewer){
        auto* receiver=edicts+viewer;
        if(!object_message||!client_ready[viewer]||!registry.get(viewer)||receiver->free||(receiver->v.flags&FL_FAKECLIENT))continue;
        auto send=[&](unsigned op,std::uint64_t key,const Bytes& data){
            Writer w;w.u64(registry.world());w.u64(++collision_sequence);w.u32(op);w.u64(key);
            if(data.empty()){for(int i=0;i<7;++i)w.u32(0);}else w.bytes(data);
            MESSAGE_BEGIN(MSG_ONE,object_message,nullptr,receiver);for(auto byte:w.data)WRITE_BYTE(byte);MESSAGE_END();
        };
        auto& previous=client_colliders[viewer];unsigned sent=0;
        if(!colliders_initialized[viewer]){send(0,0,{});previous.clear();colliders_initialized[viewer]=true;}
        for(auto it=previous.begin();it!=previous.end()&&sent<32;)if(!current.contains(it->first)){
            send(2,it->first,{});it=previous.erase(it);++sent;
        }else ++it;
        for(const auto& [key,data]:current)if(sent<32&&(!previous.contains(key)||previous.at(key)!=data)){
            send(1,key,data);previous[key]=data;++sent;
        }
        send(3,0,{}); // Lease is refreshed even when every static box is unchanged.
    }
}
// GOLDCRAFT_LIFECYCLE_FIXTURE_BEGIN identity
// A callback may changelevel, recycle a player slot, kill/respawn or switch form.
// Store values, never an edict reference to dereference after that callback.
struct NativePlayerIdentity {
    edict_t* table=nullptr;
    CBasePlayer* player=nullptr;
    std::uint64_t world=0;
    unsigned slot=0;
    std::uint32_t serial=0,life=0;
    int entity_serial=0,deadflag=0,team=0,observer=0,terminal_flags=0;
    bool bound=false,paired=false,positive_health=false,form=false;
    Key uuid{};
};
NativePlayerIdentity CaptureNativePlayer(unsigned slot) {
    if(!edicts||!registry.world()||!slot||slot>static_cast<unsigned>(max_clients)||slot>=controlled.size())return {};
    auto* current=edicts+slot;if(current->free)return {};
    const auto* pair=registry.get(slot);
    auto* player=static_cast<CBasePlayer*>(GET_PRIVATE(current));
    NativePlayerIdentity result;
    result.table=edicts;result.player=player;result.world=registry.world();result.slot=slot;
    result.serial=pair?pair->serial:0;result.life=player_lives[slot];result.entity_serial=current->serialnumber;
    result.deadflag=current->v.deadflag;result.team=player?player->m_iTeam:0;result.observer=current->v.iuser1;
    result.terminal_flags=current->v.flags&(FL_KILLME|FL_SPECTATOR);
    result.bound=pair!=nullptr;result.paired=pair&&pair->paired;result.uuid=pair?pair->uuid:Key{};
    result.positive_health=current->v.health>0;result.form=minecraft_forms[slot];
    return result;
}
edict_t* CurrentNativePlayer(const NativePlayerIdentity& identity) {
    // Check the currently owned table/world before deriving or reading an edict.
    // GoldSrc edict serialnumber alone is insufficient for reused player slots.
    if(!identity.table||edicts!=identity.table||registry.world()!=identity.world||!identity.slot||
       identity.slot>static_cast<unsigned>(max_clients)||identity.slot>=controlled.size())return nullptr;
    const auto* pair=registry.get(identity.slot);
    if((pair!=nullptr)!=identity.bound||(pair&&(pair->serial!=identity.serial||pair->paired!=identity.paired||pair->uuid!=identity.uuid))||
       player_lives[identity.slot]!=identity.life||minecraft_forms[identity.slot]!=identity.form)return nullptr;
    auto* current=edicts+identity.slot;
    if(current->free||current->serialnumber!=identity.entity_serial||GET_PRIVATE(current)!=identity.player||
       current->v.deadflag!=identity.deadflag||(current->v.health>0)!=identity.positive_health||
       current->v.iuser1!=identity.observer||(current->v.flags&(FL_KILLME|FL_SPECTATOR))!=identity.terminal_flags||
       (identity.player&&identity.player->m_iTeam!=identity.team))return nullptr;
    return current;
}
bool CurrentPoseAuthority(std::uint64_t authority) {
    // Native release cleanup is valid after disconnect. Only proof-owned
    // post-pose work must stay on its authenticated authority connection.
    return !authority||(link.connected()&&link.effect_position_hold()&&link.local_qpc_frequency()&&
        connection_generation==authority&&link.generation()==authority);
}
// Disconnect must clean its logical slot even if the engine already freed its
// edict. This guard deliberately does not require a live private player object.
struct NativeSlotIdentity {
    edict_t* table=nullptr;
    std::uint64_t world=0;
    unsigned slot=0;
    std::uint32_t serial=0,life=0;
    bool bound=false,paired=false;
    Key uuid{};
};
NativeSlotIdentity CaptureNativeSlot(unsigned slot) {
    if(!edicts||!registry.world()||!slot||slot>static_cast<unsigned>(max_clients)||slot>=controlled.size())return {};
    const auto* pair=registry.get(slot);
    return {edicts,registry.world(),slot,pair?pair->serial:0,player_lives[slot],pair!=nullptr,pair&&pair->paired,pair?pair->uuid:Key{}};
}
bool CurrentNativeSlot(const NativeSlotIdentity& identity) {
    if(!identity.table||edicts!=identity.table||registry.world()!=identity.world||!identity.slot||
       identity.slot>static_cast<unsigned>(max_clients)||identity.slot>=controlled.size()||player_lives[identity.slot]!=identity.life)return false;
    const auto* pair=registry.get(identity.slot);
    return (pair!=nullptr)==identity.bound&&(!pair||(pair->serial==identity.serial&&pair->paired==identity.paired&&pair->uuid==identity.uuid));
}
// GOLDCRAFT_LIFECYCLE_FIXTURE_END identity
// GOLDCRAFT_LIFECYCLE_FIXTURE_BEGIN use
std::uint64_t native_use_dispatch_generation=0; // Process lifetime; never reset with slots/worlds.
void ApplyNativeUse(unsigned slot,bool pressed) {
    if(!slot||slot>=controlled.size())return;
    auto& state=controlled[slot];
    if(!pressed&&!state.use_applied)return;
    const auto identity=CaptureNativePlayer(slot);
    auto* e=CurrentNativePlayer(identity);if(!e)return;
    auto* p=identity.player;
    if(!p||state.use_dispatching){state.use_applied=false;return;}
    const bool alive=p->IsAlive(); // May be a Ham/Pawn virtual override.
    e=CurrentNativePlayer(identity);if(!e)return;
    if(!alive){state.use_applied=false;return;}
    if(native_use_dispatch_generation==UINT64_MAX)return;
    const auto dispatch=++native_use_dispatch_generation;
    const Vector origin=e->v.origin;
    const int buttons=e->v.button,old_pressed=p->m_afButtonPressed,old_released=p->m_afButtonReleased;
    e->v.button=(buttons&~IN_USE)|(pressed?IN_USE:0);
    p->m_afButtonPressed=(old_pressed&~IN_USE)|(pressed&&!state.use_applied?IN_USE:0);
    p->m_afButtonReleased=(old_released&~IN_USE)|(!pressed&&state.use_applied?IN_USE:0);
    if(p->m_afButtonPressed&IN_USE)++use_presses;
    if(p->m_afButtonReleased&IN_USE)++use_releases;
    state.use_applied=pressed;state.use_dispatching=true;state.use_dispatch_token=dispatch;++use_calls;
    p->PlayerUse();
    // This C++ call owns only its token. A callback-created player/world or a
    // nested newer dispatch must not have its input flags cleared by this stack.
    if(controlled[slot].use_dispatch_token==dispatch){
        controlled[slot].use_dispatching=false;controlled[slot].use_dispatch_token=0;
    }
    e=CurrentNativePlayer(identity);if(!e)return;
    e->v.button=buttons;p->m_afButtonPressed=old_pressed;p->m_afButtonReleased=old_released;
    if(!state.active)return;
    const bool still_alive=p->IsAlive();
    e=CurrentNativePlayer(identity);if(!e)return;
    if(state.active&&still_alive&&(e->v.origin-origin).LengthSquared()>0.0001f)AcceptNativeRelocation(slot);
}
// GOLDCRAFT_LIFECYCLE_FIXTURE_END use
ServerControlTrace control_trace;
AuthoritativeDimensionTrace dimension_trace;
void RecordControl(unsigned slot,ControlCause cause,std::optional<int> requested=std::nullopt,int result=0) {
    ServerControlEvent e;e.world=registry.world();e.connection=link.generation();e.slot=slot;e.cause=cause;
    e.seconds=gpGlobals?gpGlobals->time:0;e.result=result;
    if(slot>0&&slot<controlled.size()){
        e.life=player_lives[slot];e.previous_form=minecraft_forms[slot]?1:0;e.active=controlled[slot].active;
        if(controlled[slot].sequence)e.pose_ms=(e.seconds-controlled[slot].last_update)*1000;
    }
    e.requested_form=requested.value_or(e.previous_form);control_trace.record(e);
}
// GOLDCRAFT_LIFECYCLE_FIXTURE_BEGIN release
struct NativeReleaseLease {
    NativePlayerIdentity identity;
    std::uint64_t token=0;
    int saved_movetype=MOVETYPE_WALK;
    bool restoring=false;
};
std::array<NativeReleaseLease,65> native_release_leases{};
std::uint64_t native_release_generation=0; // Process lifetime; never reset with slots/worlds.
bool NativeRestoreInProgress(unsigned slot) {
    return slot<native_release_leases.size()&&native_release_leases[slot].restoring&&
        CurrentNativePlayer(native_release_leases[slot].identity);
}
bool ReleaseControl(unsigned slot,ControlCause cause) {
    effect_publisher.Retire(slot); // Close before native Use/Pawn callbacks.
    if(!slot||slot>=controlled.size())return false;
    const bool releasing=controlled[slot].active;
    if(releasing){
        if(native_release_generation==UINT64_MAX)return false;
        RecordControl(slot,cause);
        native_release_leases[slot]={CaptureNativePlayer(slot),++native_release_generation,controlled[slot].saved_movetype,false};
        // Retire movement before Use, but retain the exact body's native-restore
        // debt. A nested form change must finish it before advancing that life.
        controlled[slot].active=false;controlled[slot].use_requested=false;
    }
    const auto lease=native_release_leases[slot];
    if(!lease.token)return true;
    const auto abandon=[&]{if(native_release_leases[slot].token==lease.token)native_release_leases[slot]={};};
    if(!CurrentNativePlayer(lease.identity)){abandon();return !releasing;}
    if(lease.restoring)return false; // An engine-hook reentry cannot advance a partly restored life.
    if(releasing)ApplyNativeUse(slot,false);
    auto* e=CurrentNativePlayer(lease.identity);
    if(!e||controlled[slot].active){abandon();return false;}
    // A nested Release may already have paid this same body's debt. Never
    // overwrite a newer lease or repeat its hull/origin engine notifications.
    if(native_release_leases[slot].token!=lease.token)return native_release_leases[slot].token==0;
    native_release_leases[slot].restoring=true;
    const auto& identity=lease.identity;
    if(identity.player)identity.player->m_flFallVelocity=0;
    const Vector origin=e->v.origin;Vector feet=origin;feet.z+=e->v.mins.z;
    e->v.movetype=lease.saved_movetype;e->v.velocity=g_vecZero;
    e->v.view_ofs=(e->v.flags&FL_DUCKING)?VEC_DUCK_VIEW:VEC_VIEW;
    SET_SIZE(e,(e->v.flags&FL_DUCKING)?VEC_DUCK_HULL_MIN:VEC_HULL_MIN,(e->v.flags&FL_DUCKING)?VEC_DUCK_HULL_MAX:VEC_HULL_MAX);
    e=CurrentNativePlayer(identity);
    if(!e||controlled[slot].active||native_release_leases[slot].token!=lease.token){abandon();return false;}
    // A hooked SetSize may itself relocate this same life. Preserve its position.
    if((e->v.origin-origin).LengthSquared()<=0.0001f){feet.z-=e->v.mins.z;SET_ORIGIN(e,feet);}
    const bool restored=CurrentNativePlayer(identity)&&!controlled[slot].active&&native_release_leases[slot].token==lease.token;
    abandon();return restored;
}
// GOLDCRAFT_LIFECYCLE_FIXTURE_END release
// GOLDCRAFT_LIFECYCLE_FIXTURE_BEGIN relocation
void AcceptNativeRelocation(unsigned slot,std::uint64_t authority) {
    const auto identity=CaptureNativePlayer(slot);
    auto* e=CurrentNativePlayer(identity);
    if(!e||e->v.deadflag!=DEAD_NO||e->v.health<=0||!CurrentPoseAuthority(authority)||NativeRestoreInProgress(slot))return;
    host_effects.Retire(slot);
    if(!CurrentNativePlayer(identity)||!CurrentPoseAuthority(authority))return;
    if(!ReleaseControl(slot,ControlCause::relocation))return;
    e=CurrentNativePlayer(identity);if(!e||!CurrentPoseAuthority(authority))return;
    if(player_lives[slot]==UINT32_MAX){registry.disconnect(slot);return;}
    // The existing life generation is also a pose barrier: queued pre-teleport
    // MC updates cannot undo a real native trigger's relocation. HostSession
    // will place/acknowledge the same living MC player at the new host position.
    ++player_lives[slot];++host_relocations;next_snapshot=0;
    controlled[slot].use_dispatching=controlled[slot].use_applied=controlled[slot].use_requested=false;
    controlled[slot].use_dispatch_token=0;
    player_spawns[slot]=to_minecraft({e->v.origin.x,e->v.origin.y,e->v.origin.z+e->v.mins.z});
    const auto relocated=CaptureNativePlayer(slot);
    host_effects.Birth(slot);
    if(!CurrentNativePlayer(relocated)||!CurrentPoseAuthority(authority))return;
    SendForm(slot);
    if(!CurrentNativePlayer(relocated)||!CurrentPoseAuthority(authority))return;
    BridgeLog("Native entity relocated paired slot "+std::to_string(slot)+"; new movement generation "+std::to_string(player_lives[slot]));
}
// GOLDCRAFT_LIFECYCLE_FIXTURE_END relocation
void HandlePose(Reader& r,const effects::PoseProof* proof=nullptr) {
    // Negotiation is a connection latch. Missing clock proof must never enable
    // fallback to unproved legacy movement on an effect-capable connection.
    if(link.effect_position_hold()!=(proof!=nullptr))return;
    const auto epoch=r.u64(),sequence=r.u64();const auto slot=r.u32(),serial=r.u32(),life=r.u32();const auto uuid=r.key();const auto flags=r.u32();
    Vec3 feet{r.f32(),r.f32(),r.f32()},velocity{r.f32(),r.f32(),r.f32()};const float pitch=r.f32(),yaw=r.f32(),eye=r.f32(),width=r.f32(),height=r.f32();r.finish();
    const auto* pair=registry.get(slot);
    if(!edicts||epoch!=registry.world()||!pair||!pair->paired||pair->serial!=serial||pair->uuid!=uuid||slot>=controlled.size())return;
    // A new Spawn can occur between two actor snapshots, even on one connection.
    // Reject its predecessor before it can replace the new native spawn position.
    if(!life||life!=player_lives[slot]){++stale_life_poses;return;}
    auto& state=controlled[slot];if(sequence<=state.sequence)return;
    if(!proof)state.sequence=sequence; // Preserve the legacy producer timeline.
    if(!(flags&1)){
        if(proof&&flags!=0)return;
        state.sequence=sequence;
        ReleaseControl(slot,ControlCause::authority_not_ready);return;
    }
    if(!minecraft_forms[slot]){++wrong_form_poses;return;}
    auto* e=edicts+slot;auto* player=static_cast<CBasePlayer*>(GET_PRIVATE(e));
    if(e->free||!player||(proof?(e->v.deadflag!=DEAD_NO||e->v.health<=0):!player->IsAlive())||(e->v.flags&FL_SPECTATOR))return;
    const auto dimensions=validate_authoritative_dimensions(flags,eye,width,height);
    dimension_trace.observe(dimensions,slot,life,flags,eye,width,height);
    // Reject this pose without aborting the rest of this frame's authority
    // messages/snapshots, and without logging every repeated invalid packet.
    if(dimensions!=DimensionRejection::none)return;
    auto origin=to_goldsrc(feet);origin.z+=36;
    if(std::abs(origin.x)>16384||std::abs(origin.y)>16384||std::abs(origin.z)>16384)throw ProtocolError("Authoritative position outside supported map bounds");
    const bool acquired=!state.active;
    const float half=width*units_per_block*0.5f;
    const Vector hull_min(-half,-half,-36),hull_max(half,half,height*units_per_block-36);
    const Vector native_velocity(velocity.x*units_per_block*20,-velocity.z*units_per_block*20,velocity.y*units_per_block*20);
    if(proof){
        if(!std::isfinite(native_velocity.x)||!std::isfinite(native_velocity.y)||!std::isfinite(native_velocity.z))return;
        // Last callback-free admission barrier. HostEffects reads actual pair,
        // lifecycle, form, clock, snapshot and held anchors here. No IsAlive,
        // logs or hooked engine function may run until every field is written.
        if(!entity_physics||!effect_publisher.Admit(slot,*proof,{origin.x,origin.y,origin.z-36}))return;
        state.sequence=sequence;
    }
    if(acquired){
        state.saved_movetype=e->v.movetype;state.active=true;
        if(!proof){RecordControl(slot,ControlCause::acquire);BridgeLog("Minecraft server owns movement for paired CS slot "+std::to_string(slot));}
    }
    state.last_update=gpGlobals->time;e->v.movetype=MOVETYPE_NONE;
    e->v.velocity=native_velocity;
    if(proof){
        // PF_setsize_I/SetMinMaxSize and PF_setorigin_I assign these fields;
        // their SET_* entry points can be hooked by Metamod/Pawn. The existing
        // TouchPlayerContacts below performs SV_LinkEdict(TRUE) after commit.
        e->v.mins=hull_min;e->v.maxs=hull_max;e->v.size=hull_max-hull_min;
        e->v.origin=Vector(origin.x,origin.y,origin.z);
    }else{
        SET_SIZE(e,hull_min,hull_max);SET_ORIGIN(e,Vector(origin.x,origin.y,origin.z));
    }
    e->v.view_ofs=Vector(0,0,eye*units_per_block-36);
    e->v.v_angle=Vector(pitch,-yaw-90,0);e->v.angles=Vector(-pitch/3,-yaw-90,0);
    if(flags&2)e->v.flags|=FL_ONGROUND;else e->v.flags&=~FL_ONGROUND;
    if(flags&4)e->v.flags|=FL_DUCKING;else e->v.flags&=~FL_DUCKING;
    const auto committed=CaptureNativePlayer(slot);
    if(entity_physics){
        const Vector submitted=e->v.origin;
        entity_physics->TouchPlayerContacts(e);
        // A native Touch may kill, respawn, switch form or replace this slot.
        // Do not dispatch a virtual method through the pre-Touch player.
        if(proof&&!CurrentPoseAuthority(proof->nativeConnection))return;
        e=CurrentNativePlayer(committed);if(!e)return;
        const bool alive=proof?(e->v.deadflag==DEAD_NO&&e->v.health>0):player->IsAlive();
        e=CurrentNativePlayer(committed);if(!e)return;
        if(state.active&&alive&&(e->v.origin-submitted).LengthSquared()>0.0001f)
            AcceptNativeRelocation(slot,proof?proof->nativeConnection:0);
    }
    if(proof&&acquired&&CurrentPoseAuthority(proof->nativeConnection)&&CurrentNativePlayer(committed)&&state.active){RecordControl(slot,ControlCause::acquire);BridgeLog("Minecraft server owns movement for paired CS slot "+std::to_string(slot));}
}
void SendBinding(edict_t* player) {
    if(!binding_message || (player->v.flags&FL_FAKECLIENT)) return;
    const auto slot=ENTINDEX(player);
    if(slot<1||slot>=static_cast<int>(client_ready.size())||!client_ready[slot])return;
    const auto* identity=registry.get(slot);
    if(!identity) return;
    Writer w; w.u64(registry.world()); w.u32(identity->slot); w.u32(identity->serial); w.bytes(identity->token);
    MESSAGE_BEGIN(MSG_ONE,binding_message,nullptr,player);
    for(auto byte : w.data) WRITE_BYTE(byte);
    MESSAGE_END();
    SendForm(slot);
}
#ifdef GOLDCRAFT_HEADLESS_FIXTURE
// Only the separately built headless test DLL contains this local delivery
// substitute for GCBind. Real pairing still validates the actual registry token.
// Never compiled into the normal server DLL, and never writes identities to logs.
void WriteHeadlessBindings(){
    const char* path=std::getenv("GOLDCRAFT_HEADLESS_BINDINGS");if(!path||!*path)return;
    Writer records;unsigned count=0;
    for(int slot=1;slot<=max_clients;++slot){
        const auto* identity=registry.get(slot);
        if(!identity||edicts[slot].free||!(edicts[slot].v.flags&FL_FAKECLIENT))continue;
        records.u32(slot);records.u32(identity->serial);records.bytes(identity->token);++count;
    }
    Writer data;data.u64(registry.world());data.u32(count);data.bytes(records.data);
    const std::string temporary=std::string(path)+".tmp";
    {std::ofstream out(temporary,std::ios::binary|std::ios::trunc);
        out.write(reinterpret_cast<const char*>(data.data.data()),data.data.size());if(!out)return;}
    MoveFileExA(temporary.c_str(),path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
}
#endif
void SendForm(unsigned slot){
    if(!edicts||!form_message||slot<1||slot>static_cast<unsigned>(max_clients)||!client_ready[slot]||!registry.get(slot))return;
    auto* e=edicts+slot;if(e->free||(e->v.flags&FL_FAKECLIENT))return;
    Writer w;w.u64(registry.world());w.u32(player_lives[slot]);w.u32(minecraft_forms[slot]?1:0);
    MESSAGE_BEGIN(MSG_ONE,form_message,nullptr,e);for(auto byte:w.data)WRITE_BYTE(byte);MESSAGE_END();
}
// GOLDCRAFT_LIFECYCLE_FIXTURE_BEGIN form
int SetForm(int slot,int form,ControlCause source){
    const auto result=[&](int value){RecordControl(static_cast<unsigned>(slot),source,form,value);return value;};
    if(!edicts||slot<1||slot>max_clients||(form!=0&&form!=1)||!registry.get(slot)||edicts[slot].free)return result(-1);
    if(form&&!registry.get(slot)->paired)return result(-2);
    if(form&&!link.connected())return result(-3);
    if(form&&form_guard){
        if(form_guard_busy[slot])return result(-5);
        form_guard_busy[slot]=true;
        struct Guard {bool& busy;~Guard(){busy=false;}} guard{form_guard_busy[slot]};
        const auto requesting=CaptureNativePlayer(static_cast<unsigned>(slot));
        const bool denied=form_guard(slot,form)!=0;
        if(!CurrentNativePlayer(requesting))return result(-1);
        if(denied)return result(-5);
    }
    if(minecraft_forms[slot]==(form!=0))return result(0);
    if(player_lives[slot]==UINT32_MAX)return result(-4);
    const auto identity=CaptureNativePlayer(static_cast<unsigned>(slot));
    if(!CurrentNativePlayer(identity)||NativeRestoreInProgress(static_cast<unsigned>(slot)))return result(-1);
    const int previous=minecraft_forms[slot]?1:0;
    result(1);
    host_effects.Retire(slot);
    if(!CurrentNativePlayer(identity))return result(-1);
    if(!ReleaseControl(slot,ControlCause::form_change))return result(-1);
    auto* e=CurrentNativePlayer(identity);if(!e)return result(-1);
    if(form&&!link.connected())return result(-3);
    minecraft_forms[slot]=form!=0;++player_lives[slot];++form_changes;next_snapshot=0;
    controlled[slot].use_dispatching=controlled[slot].use_applied=controlled[slot].use_requested=false;
    controlled[slot].use_dispatch_token=0;
    e->v.button=e->v.oldbuttons=0;
    player_spawns[slot]=to_minecraft({e->v.origin.x,e->v.origin.y,e->v.origin.z+e->v.mins.z});
    const auto changed=CaptureNativePlayer(static_cast<unsigned>(slot));
    host_effects.Birth(slot);
    if(!CurrentNativePlayer(changed))return 1;
    SendForm(slot);if(!CurrentNativePlayer(changed))return 1;
    SendAvatar(slot,true);if(!CurrentNativePlayer(changed))return 1;
    if(form_listener)form_listener(slot,previous,form);
    return 1;
}
// GOLDCRAFT_LIFECYCLE_FIXTURE_END form
class ServerControl final:public IServerControl {
public:
    int GetForm(int slot) override{return edicts&&slot>=1&&slot<=max_clients&&registry.get(slot)&&!edicts[slot].free?(minecraft_forms[slot]?1:0):-1;}
    int IsPaired(int slot) override{const auto* pair=slot>0?registry.get(slot):nullptr;return pair&&pair->paired?1:0;}
    int SetForm(int slot,int form) override{return ::SetForm(slot,form,ControlCause::form_amxx);}
    void SetFormListener(FormListener listener) override{form_listener=listener;}
};
EXPOSE_SINGLE_INTERFACE(ServerControl,IServerControl,server_control_api_version);
class ServerGameplay final:public IServerGameplay {
public:
    int GetPlayerFlags(int slot) override{return PlayerGameplayFlags(slot);}
    int GetPlayerAttack(int victim,int attacker) override{
        if(victim<1||victim>max_clients||attacker<1||attacker>max_clients)return 0;
        if(!minecraft_forms[attacker])return 0;
        const auto* target=registry.get(victim);const auto* source=registry.get(attacker);
        if(!target||!source||!active_player_attack.current(target->serial,player_births[victim],player_lives[victim],
            source->serial,player_births[attacker],player_lives[attacker]))return 0;
        return active_player_attack.match(victim,attacker,native_damage_depth,registry.world(),connection_generation);
    }
    void SetFormGuard(FormGuard guard) override{form_guard=guard;}
    void SetPlayerAttackListener(PlayerAttackListener listener) override{player_attack_listener=listener;}
};
EXPOSE_SINGLE_INTERFACE(ServerGameplay,IServerGameplay,server_gameplay_api_version);
void SendWorld() {
    Writer w; w.u64(registry.world()); w.string(STRING(gpGlobals->mapname)); link.send(Type::world,w.data);
    if(!bsp_bytes.empty()) {
        Writer b;b.u64(registry.world());b.u32(bsp_crc);b.u32(static_cast<std::uint32_t>(bsp_bytes.size()));b.bytes(bsp_bytes);link.send(Type::bsp,b.data);
    }
}
void LoadBsp() {
    bsp_bytes.clear();bsp_crc=0;
    const std::string map=STRING(gpGlobals->mapname);
    if(map.empty()||map.size()>64||map.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos)throw ProtocolError("Unsupported map filename");
    std::string path="maps/"+map+".bsp";int length=0;auto* bytes=LOAD_FILE_FOR_ME(path.data(),&length);
    if(!bytes)throw ProtocolError("Engine did not return the active map BSP");
    if(length<124||length>static_cast<int>(max_payload-16)){FREE_FILE(bytes);throw ProtocolError("BSP file exceeds bridge bounds");}
    bsp_bytes.assign(bytes,bytes+length);FREE_FILE(bytes);
    Reader header(bsp_bytes);if(header.u32()!=30)throw ProtocolError("Only BSP30 is supported");
    bsp_crc=crc32(bsp_bytes);BridgeLog("loaded authoritative BSP "+path+", bytes="+std::to_string(length)+", crc32="+std::to_string(bsp_crc));
}
void HandleTrace(Reader& r) {
    const auto epoch=r.u64(),request=r.u64();const auto hull=r.u32(),slot=r.u32();
    float from[3]{},to[3]{};for(auto& v:from)v=r.f32();for(auto& v:to)v=r.f32();r.finish();
    if(epoch!=registry.world()||hull>3||slot>=static_cast<unsigned>(gpGlobals->maxEntities)||++trace_queries>128)return;
    for(int i=0;i<3;++i)if(std::abs(from[i])>131072||std::abs(to[i])>131072)throw ProtocolError("Trace coordinate bounds");
    edict_t* entity=edicts+slot;
    if(entity->free||!entity->v.modelindex||(slot&&STRING(entity->v.model)[0]!='*'))return;
    TraceResult trace{};TRACE_MODEL(from,to,hull,entity,&trace);
    Writer w;w.u64(epoch);w.u64(request);w.u32(hull);w.u32(slot);w.u32(entity->serialnumber);
    w.f32(trace.flFraction);w.u32((trace.fStartSolid?1u:0u)|(trace.fAllSolid?2u:0u)|(trace.fInOpen?4u:0u)|(trace.fInWater?8u:0u));
    for(int i=0;i<3;++i)w.f32(trace.vecEndPos[i]);for(int i=0;i<3;++i)w.f32(trace.vecPlaneNormal[i]);w.f32(trace.flPlaneDist);
    link.send(Type::trace_result,w.data);
}
void SendBrushes() {
    std::vector<int> slots;
    for(int i=max_clients+1;i<gpGlobals->maxEntities;++i) {
        auto* e=edicts+i;
        if(!e->free&&e->v.modelindex&&STRING(e->v.model)[0]=='*'&&
           (e->v.solid==SOLID_BSP||e->v.skin==CONTENTS_LADDER))slots.push_back(i);
    }
    if(slots.size()>4096)throw ProtocolError("Brush entity budget exceeded");
    Writer w;w.u64(registry.world());w.u64(tick);w.u32(static_cast<unsigned>(slots.size()));
    for(int slot:slots) {
        auto* e=edicts+slot;const char* model=STRING(e->v.model);
        w.u32(slot);w.u32(e->serialnumber);w.u32(static_cast<unsigned>(std::strtoul(model+1,nullptr,10)));w.u32(e->v.movetype);
        w.u32((e->v.solid==SOLID_BSP?1u:0u)|(e->v.skin==CONTENTS_LADDER?2u:0u));
        for(int i=0;i<3;++i)w.f32(e->v.origin[i]);for(int i=0;i<3;++i)w.f32(e->v.angles[i]);
        for(int i=0;i<3;++i)w.f32(e->v.absmin[i]);for(int i=0;i<3;++i)w.f32(e->v.absmax[i]);
        for(int i=0;i<3;++i)w.f32(e->v.velocity[i]);
    }
    link.send(Type::brushes,w.data);
}
void HandlePair(Reader& r) {
    auto world=r.u64(); auto slot=r.u32(); auto serial=r.u32(); auto token=r.key(); auto uuid=r.key(); r.finish();
    const auto* previous=registry.get(slot);const bool already_paired=previous&&previous->paired;
    auto status=registry.bind(world,slot,serial,token,uuid);
    Writer w; w.u64(registry.world()); w.u32(slot); w.u32(serial); w.u32(static_cast<std::uint32_t>(status)); w.bytes(uuid);
    link.send(Type::pair_result,w.data);
    if(status==PairStatus::ok&&!already_paired&&default_form.value!=0)SetForm(slot,1,ControlCause::form_default);
}
void SendAvatar(unsigned slot,bool refresh) {
    if(!avatar_message||!edicts||slot<1||slot>static_cast<unsigned>(max_clients))return;
    const auto* identity=registry.get(slot);auto* e=edicts+slot;auto* p=static_cast<CBasePlayer*>(GET_PRIVATE(e));
    unsigned flags=0;
    if(identity&&!e->free&&p)flags=(p->IsAlive()?1u:0u)|((e->v.flags&FL_DUCKING)?2u:0u)|((e->v.flags&FL_SPECTATOR)?4u:0u)|((e->v.flags&FL_FAKECLIENT)?8u:0u)|(identity->paired?16u:0u)|(controlled[slot].active?32u:0u)|(minecraft_forms[slot]?64u:0u);
    Writer w;w.u64(registry.world());w.u32(slot);w.u32(identity?identity->serial:0);w.u32(identity?identity->userid:0);w.u32(flags);w.bytes(identity?identity->uuid:Key{});
    if(!refresh&&w.data==avatar_payloads[slot])return;
    avatar_payloads[slot]=w.data;
    // The roster is a custom user message. A newly connecting client may not
    // have its definitions yet; only clients that reached PutInServer receive it.
    for(int viewer=1;viewer<=max_clients;++viewer){
        auto* receiver=edicts+viewer;
        if(!client_ready[viewer]||!registry.get(viewer)||receiver->free||!receiver->pvPrivateData||(receiver->v.flags&FL_FAKECLIENT))continue;
        MESSAGE_BEGIN(MSG_ONE,avatar_message,nullptr,receiver);
        for(auto byte:w.data)WRITE_BYTE(byte);
        MESSAGE_END();
    }
}
#include "observer_view.inc"
void SendHealthLimits(const std::vector<int>& slots);
void SendActors() {
    std::vector<int> slots;
    for(int i=1;i<=max_clients;++i) {
        SendAvatar(i);
        auto* e=&edicts[i];
        if(!e->free && e->pvPrivateData && (e->v.flags&(FL_CLIENT|FL_FAKECLIENT)) && registry.get(i)) slots.push_back(i);
    }
    Writer w; w.u64(registry.world()); w.u64(++tick); w.f32(gpGlobals->time);
    skill_damage_snapshots.begin(registry.world(),tick,gpGlobals->time);
    w.u32(g_pGameRules && g_pGameRules->IsFreezePeriod()?1:0); w.u32(static_cast<std::uint32_t>(slots.size()));
    for(int slot : slots) {
        auto* e=&edicts[slot]; auto* p=static_cast<CBasePlayer*>(GET_PRIVATE(e)); const auto* binding=registry.get(slot);
        std::uint32_t flags=(p->IsAlive()?1u:0u)|((e->v.flags&FL_DUCKING)?2u:0u)|((e->v.flags&FL_SPECTATOR)?4u:0u)|((e->v.flags&FL_FAKECLIENT)?8u:0u)|(binding->paired?16u:0u)|(controlled[slot].active?32u:0u)|(minecraft_forms[slot]?64u:0u);
        w.u32(slot); w.u32(binding->serial); w.u32(binding->userid); w.u32(p->m_iTeam); w.u32(flags); w.u32(player_lives[slot]);
        w.f32(e->v.health); w.f32(e->v.armorvalue);
        const auto* view=ActorPresentationView(p);
        for(int j=0;j<3;++j) w.f32(view->origin[j]);
        for(int j=0;j<3;++j) w.f32(e->v.velocity[j]);
        w.f32(view->v_angle.x); w.f32(view->v_angle.y);
        for(int j=0;j<3;++j) w.f32(view->mins[j]);
        for(int j=0;j<3;++j) w.f32(view->maxs[j]);
        w.bytes(binding->uuid);
        w.u32(player_births[slot]);w.u64(vitals_ack[slot]);
        if(p->IsAlive()){
            const auto center=e->v.origin+(e->v.mins+e->v.maxs)*0.5f;
            skill_damage_snapshots.actor(tick,slot,binding->serial,player_births[slot],{center.x,center.y,center.z});
        }
    }
    link.send(Type::actors,w.data);
    SendHealthLimits(slots);
    if(tick%10==0)if(const char* path=std::getenv("GOLDCRAFT_SERVER_STATUS")) {
        std::ofstream out(path);out<<"{\"world\":\""<<registry.world()<<"\",\"map\":\""<<STRING(gpGlobals->mapname)<<"\",\"tick\":"<<tick<<",\"bridgeGeneration\":"<<connection_generation
            <<",\"dedicated\":"<<(IS_DEDICATED_SERVER()?"true":"false")<<",\"staleLifePoses\":"<<stale_life_poses
            <<",\"entityPhysics\":"<<(entity_physics?"true":"false")<<",\"touchDispatches\":"<<touch_dispatches
            <<",\"useCalls\":"<<use_calls<<",\"usePresses\":"<<use_presses<<",\"useReleases\":"<<use_releases
            <<",\"hostRelocations\":"<<host_relocations<<",\"staleUseCommands\":"<<stale_use_commands
            <<",\"fluidVolumes\":"<<fluid_field.state().volumes.size()<<",\"fluidRevision\":"<<fluid_field.state().revision<<",\"fluidActive\":"<<(ActiveFluids()?"true":"false")
            <<",\"minecraftObjects\":"<<GoldCraft_ObjectCount()<<",\"objectRevision\":"<<object_revision
            <<",\"objectActions\":"<<object_actions<<",\"objectDamageActions\":"<<object_damage_actions<<",\"objectUseActions\":"<<object_use_actions
            <<",\"vitalsAccepted\":"<<vitals_accepted<<",\"vitalsRejected\":"<<vitals_rejected
            <<",\"mapMiningMode\":"<<static_cast<unsigned>(GoldCraft_MapMiningPolicy().mode)<<",\"mapMiningRevision\":"<<GoldCraft_MapMiningPolicy().revision
            <<",\"mapEditRevision\":"<<GoldCraft_MapEdits().state().revision<<",\"mapEditCount\":"<<GoldCraft_MapEdits().state().cuts.size()
            <<",\"mapMiningApplied\":"<<mining_applied<<",\"mapMiningRejected\":"<<mining_rejected
            <<",\"mobDamageAccepted\":"<<mob_damage_accepted<<",\"mobDamageRejected\":"<<mob_damage_rejected<<",\"actors\":[";
        bool first=true;for(int slot:slots){
            auto* e=edicts+slot;const auto* pair=registry.get(slot);auto* player=static_cast<CBasePlayer*>(GET_PRIVATE(e));
            if(!first)out<<',';first=false;
            out<<"{\"slot\":"<<slot<<",\"serial\":"<<pair->serial<<",\"engineSerial\":"<<e->serialnumber<<",\"userid\":"<<pair->userid<<",\"uuid\":\""<<UuidText(pair->uuid)
               <<"\",\"team\":"<<player->m_iTeam<<",\"health\":"<<e->v.health<<",\"armor\":"<<e->v.armorvalue
               <<",\"alive\":"<<(player->IsAlive()?"true":"false")<<",\"life\":"<<player_lives[slot]<<",\"spawn\":"<<player_births[slot]<<",\"vitalsAck\":"<<vitals_ack[slot]<<",\"poseSequence\":"<<controlled[slot].sequence
               <<",\"controlled\":"<<(controlled[slot].active?"true":"false")<<",\"minecraftFallAuthority\":"<<(GoldCraft_MinecraftFallAuthority(e)?"true":"false")
               <<",\"baseMaxSpeed\":"<<e->v.maxspeed<<",\"effectiveMaxSpeed\":"<<GoldCraft_StatusMaxSpeed(e,e->v.maxspeed)
               <<",\"waterLevel\":"<<e->v.waterlevel<<",\"waterType\":"<<e->v.watertype
               <<",\"nativeFallVelocity\":"<<player->m_flFallVelocity<<",\"spawnFeet\":["<<player_spawns[slot].x<<','<<player_spawns[slot].y<<','<<player_spawns[slot].z
               <<"],\"minecraftForm\":"<<(minecraft_forms[slot]?"true":"false")<<",\"moveType\":"<<e->v.movetype<<",\"buttons\":"<<e->v.button
               <<",\"origin\":["<<e->v.origin.x<<','<<e->v.origin.y<<','<<e->v.origin.z<<"],\"mins\":["<<e->v.mins.x<<','<<e->v.mins.y<<','<<e->v.mins.z<<"]}";
        }out<<"],\"hostEntities\":[";first=true;
        for(int i=max_clients+1;i<gpGlobals->maxEntities;++i){
            auto* e=edicts+i;auto* p=GET_PRIVATE<CBaseEntity>(e);
            const auto touch=entity_touches.find(i);
            const auto touches=touch!=entity_touches.end()&&touch->second.serial==e->serialnumber?touch->second.count:0;
            if(e->free||!p||(e->v.flags&FL_KILLME)||(!e->v.solid&&e->v.takedamage==DAMAGE_NO&&!touches))continue;
            if(!first)out<<',';first=false;
            out<<"{\"slot\":"<<i<<",\"serial\":"<<e->serialnumber<<",\"class\":"<<std::quoted(STRING(e->v.classname))
                <<",\"model\":"<<std::quoted(STRING(e->v.model))<<",\"solid\":"<<e->v.solid<<",\"moveType\":"<<e->v.movetype
                <<",\"health\":"<<e->v.health<<",\"takeDamage\":"<<e->v.takedamage<<",\"useCaps\":"<<p->ObjectCaps()<<",\"touches\":"<<touches<<",\"minecraftKey\":"<<GoldCraft_ObjectKey(e)
                <<",\"origin\":["<<e->v.origin.x<<','<<e->v.origin.y<<','<<e->v.origin.z<<"],\"angles\":["<<e->v.angles.x<<','<<e->v.angles.y<<','<<e->v.angles.z
                <<"],\"min\":["<<e->v.absmin.x<<','<<e->v.absmin.y<<','<<e->v.absmin.z<<"],\"max\":["<<e->v.absmax.x<<','<<e->v.absmax.y<<','<<e->v.absmax.z<<']';
            if(FClassnameIs(e,"hostage_entity")){auto* leader=static_cast<CHostage*>(p)->GetLeader();out<<",\"leader\":"<<(leader?leader->entindex():0);}
            out<<'}';
        }out<<"],\"formChanges\":"<<form_changes<<",\"wrongFormPoses\":"<<wrong_form_poses<<',';
        control_trace.write(out);out<<',';dimension_trace.write(out);out<<',';write_transport_stats(out,link.transport_stats());out<<'}';
    }
}
}

namespace {
#include "status_attributes.inc"
#include "status_environment.inc"
#include "status_motion.inc"
#include "status_fall.inc"
#include "status_health.inc"
#include "status_visibility.inc"
#include "status_glow.inc"
#include "status_vision.inc"
#include "armor_defense.inc"
#include "status_effects.inc"
#include "tnt_damage.inc"
void HandleVitals(std::span<const std::uint8_t> bytes){
    const auto change=read_vitals_delta(bytes);
    const auto* pair=change.slot<=static_cast<unsigned>(max_clients)?registry.get(change.slot):nullptr;
    if(!pair||!pair->paired||!current_vitals_delta(change,registry.world(),vitals_ack[change.slot],pair->serial,player_births[change.slot],pair->uuid)){
        ++vitals_rejected;return;
    }
    // A rejected event is acknowledged too. Otherwise speculative MC health
    // would remain deducted forever or retry a damage hook a second time.
    vitals_ack[change.slot]=change.event;next_snapshot=0;
    auto* victim=edicts+change.slot;auto* player=GET_PRIVATE<CBasePlayer>(victim);
    if(victim->free||!player||!player->IsAlive()){++vitals_rejected;return;}
    bool accepted=false;
    if(change.delta>0){
        accepted=player->TakeHealth(change.delta,DMG_GENERIC)!=FALSE;
    }else{
        auto* attacker=edicts;
        if(change.attacker){
            const auto* source_pair=registry.get(change.attacker);
            if(change.attacker>static_cast<unsigned>(max_clients)||!source_pair||source_pair->serial!=change.attacker_serial
                ||player_births[change.attacker]!=change.attacker_spawn||edicts[change.attacker].free){++vitals_rejected;return;}
            attacker=edicts+change.attacker;
        }else if(auto* mob=GoldCraft_MobObject(change.source_key))attacker=mob;
        auto* source=GET_PRIVATE<CBaseEntity>(attacker);
        if(!g_pGameRules->FPlayerCanTakeDamage(player,source)){++vitals_rejected;return;}
        const int bits=change.kind==3?DMG_BLAST:change.kind==2?DMG_BULLET:change.kind==1?DMG_CLUB:DMG_GENERIC;
        const auto hitgroup=player->m_LastHitGroup;player->m_LastHitGroup=HITGROUP_GENERIC;
        const float before=victim->v.health;
        final_minecraft_damage=victim;
        accepted=player->TakeDamage(&attacker->v,&attacker->v,-change.delta,bits)!=FALSE;
        final_minecraft_damage=nullptr;
        accepted=accepted||victim->v.health<before;
        player->m_LastHitGroup=hitgroup;
    }
    if(accepted)++vitals_accepted;else ++vitals_rejected;
}
void HandlePlayerGameplay(std::span<const std::uint8_t> bytes){
    if(!link.player_gameplay())throw ProtocolError("Unnegotiated player gameplay");
    const auto state=read_player_gameplay(bytes,link.skill_guard());
    const auto* pair=state.slot<=static_cast<unsigned>(max_clients)?registry.get(state.slot):nullptr;
    if(!pair||!pair->paired||state.epoch!=registry.world()||state.serial!=pair->serial||state.uuid!=pair->uuid
        ||state.spawn!=player_births[state.slot]||state.life!=player_lives[state.slot]||edicts[state.slot].free)return;
    player_gameplay[state.slot].update(state,gpGlobals->time);
}
void HandleMobDamage(std::span<const std::uint8_t> bytes,bool active=false){
    if(active&&!link.player_gameplay())throw ProtocolError("Unnegotiated player attack");
    const auto damage=read_mob_damage(bytes);
    if(active&&(!damage.attacker||damage.source_key||
        (damage.kind!=MobDamageKind::melee&&damage.kind!=MobDamageKind::projectile&&damage.kind!=MobDamageKind::skill)))
        throw ProtocolError("Invalid active player attack");
    if(damage.kind==MobDamageKind::status){HandleStatusDamage(damage);return;}
    if(damage.kind==MobDamageKind::tnt){HandleTntDamage(damage);return;}
    if(damage.epoch!=registry.world()||damage.event<=mob_damage_event){++mob_damage_rejected;return;}
    const auto previous_event=mob_damage_event;
    mob_damage_event=damage.event;
    unsigned status=1;float health=0,armor=0;
    const auto* binding=damage.slot<=static_cast<unsigned>(max_clients)?registry.get(damage.slot):nullptr;
    if(binding&&current_mob_damage(damage,registry.world(),previous_event,binding->serial,player_births[damage.slot])){
        auto* victim=edicts+damage.slot;auto* player=GET_PRIVATE<CBasePlayer>(victim);
        auto* source=damage.attacker?nullptr:GoldCraft_MobObject(damage.source_key);
        if(damage.attacker&&damage.attacker<=static_cast<unsigned>(max_clients)){
            const auto* source_pair=registry.get(damage.attacker);
            if(source_pair&&source_pair->paired&&source_pair->serial==damage.attacker_serial&&player_births[damage.attacker]==damage.attacker_spawn
                &&!edicts[damage.attacker].free)source=edicts+damage.attacker;
        }
        const bool skill=damage.kind==MobDamageKind::skill;
        if(player&&!victim->free&&player->IsAlive()&&source&&(source!=victim||(damage.kind!=MobDamageKind::melee&&!skill))){
            const Vector point(damage.source.x,damage.source.y,damage.source.z);
            const Vector destination=victim->v.origin+(victim->v.mins+victim->v.maxs)*0.5f;
            const float radius=damage.kind==MobDamageKind::explosion?384.0f:128.0f;
            const bool near_target=skill?skill_damage_snapshots.matches(damage,gpGlobals->time,{destination.x,destination.y,destination.z})
                :(point-destination).LengthSquared()<=radius*radius;
            const bool near_source=(damage.kind!=MobDamageKind::melee&&!skill)||(point-source->v.origin).LengthSquared()<=128.0f*128.0f;
            if(near_target&&near_source){
                TraceResult trace;UTIL_TraceLine(point,destination,dont_ignore_monsters,source,&trace);
                if(!trace.fStartSolid&&(trace.flFraction>=0.999f||trace.pHit==victim)&&g_pGameRules->FPlayerCanTakeDamage(player,GET_PRIVATE<CBaseEntity>(source))){
                    const int bits=damage.kind==MobDamageKind::explosion?DMG_BLAST:damage.kind==MobDamageKind::projectile?DMG_BULLET:DMG_CLUB;
                    // ReGameDLL/ReAPI retain armor, hooks and Killed handling.
                    // Health is never assigned by this bridge; no invented headshot.
                    const auto previous_hitgroup=player->m_LastHitGroup;player->m_LastHitGroup=HITGROUP_GENERIC;
                    const float before_health=victim->v.health,before_armor=victim->v.armorvalue;
                    const auto target_slot=CaptureNativeSlot(damage.slot);
                    const auto target_identity=CaptureNativePlayer(damage.slot),source_identity=CaptureNativePlayer(damage.attacker);
                    active=active&&minecraft_forms[damage.attacker];
                    const auto previous_attack=active_player_attack;
                    auto* previous_minecraft_attack=minecraft_attack;
                    minecraft_attack=victim;
                    active_player_attack=active?ActivePlayerAttack{static_cast<int>(damage.slot),static_cast<int>(damage.attacker),static_cast<int>(damage.kind),native_damage_depth+1,registry.world(),connection_generation,
                        damage.serial,player_births[damage.slot],player_lives[damage.slot],damage.attacker_serial,player_births[damage.attacker],player_lives[damage.attacker]}:ActivePlayerAttack{};
                    const bool accepted=player->TakeDamage(&source->v,&source->v,damage.amount,bits)!=FALSE;
                    active_player_attack=previous_attack;minecraft_attack=previous_minecraft_attack;
                    // Callbacks can retire a map, player life or connection. Never
                    // apply a follow-up impulse to a replacement occupant.
                    if(!CurrentNativeSlot(target_slot)||victim->free||victim->serialnumber!=target_identity.entity_serial||GET_PRIVATE(victim)!=player)return;
                    // CBaseMonster::TakeDamage returns FALSE after KilledInflicted.
                    // Confirm the actual native outcome, including lethal hits and
                    // armor consumed by an integer-rounded zero-health-damage hit.
                    status=accepted||victim->v.health<before_health||victim->v.armorvalue<before_armor?0u:5u;
                    player->m_LastHitGroup=previous_hitgroup;
                    if(active&&player_attack_listener&&CurrentNativePlayer(target_identity)&&CurrentNativePlayer(source_identity)&&status==0&&(victim->v.health<before_health||victim->v.armorvalue<before_armor))
                        player_attack_listener(damage.slot,damage.attacker,static_cast<int>(damage.kind),
                            std::max(0.0f,before_health-victim->v.health),std::max(0.0f,before_armor-victim->v.armorvalue));
                    if(!CurrentNativeSlot(target_slot)||victim->free||victim->serialnumber!=target_identity.entity_serial||GET_PRIVATE(victim)!=player)return;
                }else status=4;
            }else status=skill&&!near_target?6:3;
        }else status=2;
        if(player&&!victim->free){health=victim->v.health;armor=victim->v.armorvalue;}
    }
    if(status==0)++mob_damage_accepted;else ++mob_damage_rejected;
    Writer reply;reply.u64(registry.world());reply.u64(damage.event);reply.u32(damage.slot);reply.u32(damage.serial);reply.u32(damage.life);reply.u32(status);reply.f32(health);reply.f32(armor);
    link.send(Type::damage_result,reply.data);
}
void SendMiningPolicy(){
    const auto policy=GoldCraft_MapMiningPolicy();
    if(link.host_explosions()&&!mining_materials_sent&&!bsp_bytes.empty())
        mining_materials_sent=link.send(Type::host_materials,GoldCraft_MapMiningMaterials(bsp_bytes));
    if(link.connected()&&policy.epoch&&policy.revision!=mining_policy_sent&&link.send(Type::map_mining_policy,mining::encode_policy(policy)))
        mining_policy_sent=policy.revision;
}
void UpdateGameplayRules() {
    gameplay_rules.configure(round_clear_blocks.value==1,death_drop_items.value!=0);
}
void SendGameplayRules() {
    if(link.gameplay_rules()&&gameplay_rules.world&&gameplay_rules_sent!=gameplay_rules.revision&&
       link.send(Type::gameplay_rules,gameplay_rules.encode()))gameplay_rules_sent=gameplay_rules.revision;
}
std::uint64_t EditRevision(const Message& message){
    Reader r(message.payload);r.u64();const auto revision=r.u64();
    return message.type==Type::map_edit_delta?r.u64():revision;
}
void SendEdits(){
    const auto& ledger=GoldCraft_MapEdits();
    if(link.connected())for(const auto& message:ledger.since(edit_revision_sent)){
        if(!link.send(message.type,message.payload))break;
        edit_revision_sent=EditRevision(message);
    }
    edits::pump_fragments(max_clients,edit_client_cursor,[&](int slot){
        auto& delivery=edit_deliveries[slot];
        if(!delivery.enabled||edicts[slot].free)return false;
        if(delivery.message.payload.empty()){
            auto changes=ledger.since(delivery.revision);if(changes.empty())return false;
            if(edit_transfer==UINT64_MAX)throw ProtocolError("Map edit transfer exhausted");
            delivery.message=std::move(changes.front());delivery.offset=0;delivery.transfer=++edit_transfer;
        }
        const auto bytes=edits::fragment(registry.world(),delivery.transfer,delivery.message,delivery.offset);
        if(host_delivery&&host_delivery->ReliableRoom(slot)<bytes.size()+2)return false;
        MESSAGE_BEGIN(MSG_ONE,edit_message,nullptr,edicts+slot);
        for(auto byte:bytes)WRITE_BYTE(byte);
        MESSAGE_END();
        delivery.offset+=std::min(edits::fragment_bytes,delivery.message.payload.size()-delivery.offset);
        if(delivery.offset==delivery.message.payload.size()){
            delivery.revision=EditRevision(delivery.message);delivery.message.payload.clear();delivery.offset=0;
        }
        return true;
    },host_delivery?8:1);
}
void HandleMining(std::span<const std::uint8_t> bytes, bool sample=false){
    const auto request=mining::decode_request(bytes);
    const auto policy=GoldCraft_MapMiningPolicy();
    mining::Result result{registry.world(),request.event,policy.revision,request.slot,request.serial,request.life,request.target,request.target_serial};
    const auto* pair=request.slot<=static_cast<unsigned>(max_clients)?registry.get(request.slot):nullptr;
    const bool current=pair&&pair->paired&&request.epoch==registry.world()&&pair->serial==request.serial&&pair->uuid==request.uuid
        &&request.life==player_lives[request.slot]&&minecraft_forms[request.slot]&&controlled[request.slot].active
        &&gpGlobals->time-controlled[request.slot].last_update<=1.0f;
    mining::Surface surface;
    if(current){
        if(request.event<=mining_events[request.slot])result.status=mining::Status::replay;
        else{
            mining_events[request.slot]=request.event;
            if(request.revision!=policy.revision)result.status=mining::Status::stale_policy;
            else if(policy.mode==mining::Mode::disabled)result.status=mining::Status::disabled;
            else if(sample){
                if(gpGlobals->time<next_mining_sample[request.slot])result.status=mining::Status::cooldown;
                else{
                    next_mining_sample[request.slot]=gpGlobals->time+0.09f;
                    surface=GoldCraft_MapMiningSample(request,edicts+request.slot);result=surface.result;
                }
            }
            else if(gpGlobals->time<next_mining[request.slot])result.status=mining::Status::cooldown;
            else{
                next_mining[request.slot]=gpGlobals->time+0.19f;
                result=GoldCraft_MapMiningApply(request,edicts+request.slot);
            }
        }
    }
    if(sample){
        surface.result=result;
        link.send(Type::map_mining_surface,mining::encode_surface(surface));
    }else{
        if(result.status==mining::Status::applied)++mining_applied;else ++mining_rejected;
        link.send(Type::map_mining_result,mining::encode_result(result));
    }
    SendMiningPolicy();
}
}

void GoldCraft_InitCvars(){
    static bool registered=false;
    if(!registered){CVAR_REGISTER(&default_form);CVAR_REGISTER(&allow_switch);CVAR_REGISTER(&round_clear_blocks);CVAR_REGISTER(&death_drop_items);registered=true;}
    GoldCraft_MapMiningInit();
}
void GoldCraft_RoundCleanup() {
    UpdateGameplayRules();gameplay_rules.cleanup_round();
}
void GoldCraft_ServerActivate(edict_t* entities,int client_max) {
    try {
        if(!server_log.is_open()) if(const char* path=std::getenv("GOLDCRAFT_SERVER_LOG"))server_log.open(path,std::ios::app);
        host_effects.End();
        effect_publisher.Reset();
        edicts=entities; max_clients=std::clamp(client_max,0,64); registry.new_world(random_epoch());
        ClearNativeArmor();
        player_gameplay={};player_gameplay_supported=false;active_player_attack={};form_guard_busy={};
        gameplay_rules.reset(registry.world());gameplay_rules_sent=0;
        tick=connection_generation=0; next_snapshot=next_binding=0; previous_error.clear();
        skill_damage_snapshots.clear();
        controlled={};dimension_trace={};client_incarnations={};player_lives={};player_births={};vitals_ack={};player_spawns={};stale_life_poses=0;client_ready={};avatar_payloads={};
        host_effects.Begin(registry.world());
        vitals_accepted=vitals_rejected=0;final_minecraft_damage=minecraft_attack=nullptr;
        minecraft_forms={};next_form_change={};form_changes=wrong_form_poses=0;
        ResetFluids();
        GoldCraft_ObjectsReset(false);object_revision=object_actions=object_damage_actions=object_use_actions=0;
        collision_sequence=0;next_colliders=0;client_colliders={};colliders_initialized={};
        mob_damage_event=mob_damage_accepted=mob_damage_rejected=0;
        GoldCraft_MapMiningReset(registry.world());mining_policy_sent=mining_applied=mining_rejected=0;mining_materials_sent=false;mining_events={};next_mining={};next_mining_sample={};
        edit_deliveries={};edit_revision_sent=edit_transfer=0;next_edit_query=0;edit_client_cursor=0;
        GoldCraft_InitCvars();
        touch_dispatches=use_calls=use_presses=use_releases=host_relocations=stale_use_commands=0;entity_touches.clear();entity_physics=nullptr;
        ClearStatusMotion();ClearStatusFall();ClearStatusHealth();ClearStatusVisibility();ClearStatusVision();ClearStatusGlow(true);
        vision_message=REG_USER_MSG(status_vision::message_name,status_vision::wire_bytes);
        glow_message=REG_USER_MSG(status_glow::message_name,status_glow::wire_bytes);
        motion_message=REG_USER_MSG(status_motion::message_name,44);
        binding_message=REG_USER_MSG("GCBind",32);
        avatar_message=REG_USER_MSG("GCAvatar",40);
        form_message=REG_USER_MSG("GCForm",16);
        object_message=REG_USER_MSG("GCObj",56);
        fluid_message=REG_USER_MSG(fluids::message_name,-1);
        edit_message=REG_USER_MSG("GCEdit",-1);
        REG_USER_MSG("GCBrush",-1);
        // The world edict selects serverinfo; nullptr selects localinfo in
        // ReHLDS. Ordinary servers therefore never trigger the client handshake.
        SET_KEY_VALUE(GET_INFO_BUFFER(edicts),"mc_protocol",std::to_string(protocol_version).c_str());
        BridgeLog("registered GCBind="+std::to_string(binding_message)+", GCAvatar="+std::to_string(avatar_message));
        if(auto config=environment_config("GOLDCRAFT_SERVER",Role::host_server,Role::fabric_server)) {
            auto factory=Sys_GetFactory("swds.dll");
            entity_physics=factory?static_cast<IHostEntityPhysics*>(factory(host_entity_api_version,nullptr)):nullptr;
            host_delivery=factory?static_cast<IHostDelivery*>(factory(host_delivery_api_version,nullptr)):nullptr;
            if(!entity_physics)throw ProtocolError("GoldCraft host entity API unavailable; build/deploy the workspace ReHLDS engine");
            LoadBsp();
            link.start(*config); BridgeLog("server bridge listening on loopback port "+std::to_string(link.port())+", map "+STRING(gpGlobals->mapname));
        } else BridgeLog("server bridge loaded; no instance environment, transport disabled");
    } catch(const std::exception& e) { link.stop(); BridgeLog(e.what()); }
}
void GoldCraft_ServerDeactivate() {
    auto* const deactivating_table=edicts;const auto deactivating_world=registry.world();
    effect_publisher.Reset();
    host_effects.End();
    if(edicts!=deactivating_table||registry.world()!=deactivating_world)return;
    // ReHLDS calls ClientDisconnect after ServerDeactivate during changelevel.
    // Restore live edicts before dropping the table, then leave no active leases.
    for(int slot=1;slot<=max_clients;++slot){
        ReleaseControl(slot,ControlCause::map_deactivate);
        if(edicts!=deactivating_table||registry.world()!=deactivating_world)return;
    }
    ResetFluids();
    GoldCraft_ObjectsReset(true);
    GoldCraft_MapMiningReset(0);
    edit_deliveries={};edit_revision_sent=0;
    controlled={};client_ready={};avatar_payloads={};
    ClearNativeArmor();
    player_gameplay={};player_gameplay_supported=false;active_player_attack={};form_guard_busy={};
    link.stop(); registry.new_world(0); bsp_bytes.clear();edicts=nullptr; max_clients=0; BridgeLog("server map deactivated; all bindings invalidated");
}
void GoldCraft_ClientConnect(edict_t* player) {GoldCraft_ClientDisconnect(player);}
void GoldCraft_ClientPutInServer(edict_t* player) {
    try {
        const unsigned slot=ENTINDEX(player);
        if(!slot||slot>=client_incarnations.size()||client_incarnations[slot]==UINT32_MAX)throw ProtocolError("Player connection generation exhausted");
        host_effects.ResetSlot(slot);
        effect_publisher.Retire(slot);
        // The copied GoldSrc engine keeps player edict serialnumber == 0 across reconnects.
        // Use a server-owned connection generation so old poses cannot target a reused slot.
        registry.connect(slot,++client_incarnations[slot],GETPLAYERUSERID(player));
        player_gameplay[slot]={};
        client_ready[slot]=false;player_lives[slot]=player_births[slot]=0;vitals_ack[slot]=0;
        host_effects.Connected(slot);
    }
    catch(const std::exception& e) { BridgeLog(e.what()); }
}
void GoldCraft_ClientDisconnect(edict_t* player) {
    const auto slot=ENTINDEX(player);if(slot<1||slot>=static_cast<int>(controlled.size()))return;
    if(!edicts||slot>max_clients||player!=edicts+slot)return;
    const auto disconnecting=CaptureNativeSlot(static_cast<unsigned>(slot));
    host_effects.ResetSlot(slot);
    if(!CurrentNativeSlot(disconnecting))return;
    effect_publisher.Retire(slot);
    edit_deliveries[slot]={};
    if(!CurrentNativeSlot(disconnecting))return;
    ReleaseControl(slot,ControlCause::disconnect);
    if(!CurrentNativeSlot(disconnecting))return;
    player_gameplay[slot]={};
    controlled[slot]={};minecraft_forms[slot]=false;next_form_change[slot]=0;mining_events[slot]=0;next_mining[slot]=0;next_mining_sample[slot]=0;client_ready[slot]=false;client_colliders[slot].clear();colliders_initialized[slot]=false;fluid_deliveries[slot]={};registry.disconnect(slot);SendAvatar(slot);
}
GoldCraft_PlayerEffectTransition::GoldCraft_PlayerEffectTransition(edict_t* player, bool terminal) noexcept
    : terminal_(terminal), exceptions_(std::uncaught_exceptions()) {
    if(!edicts||!player)return;
    const int slot=ENTINDEX(player);
    if(slot<1||slot>max_clients)return;
    slot_=static_cast<unsigned>(slot);
    effect_publisher.Retire(slot_);
    const auto token=host_effects.StartTransition(slot_,terminal_);
    world_=token.context.world;generation_=token.context.generation;
    slot_epoch_=token.slot_epoch;serial_=token.serial;
}
GoldCraft_PlayerEffectTransition::~GoldCraft_PlayerEffectTransition() noexcept {
    host_effects.FinishTransition({{world_,generation_},slot_epoch_,serial_},slot_,terminal_,
        std::uncaught_exceptions()==exceptions_);
}

void GoldCraft_PlayerSpawn(edict_t* player) {
    const auto slot=ENTINDEX(player);
    if(!edicts||slot<1||slot>max_clients||!registry.get(slot))return;
    if(player!=edicts+slot)return;
    const auto spawning=CaptureNativePlayer(static_cast<unsigned>(slot));
    host_effects.Forget(slot);
    if(!CurrentNativePlayer(spawning))return;
    effect_publisher.Retire(slot);
    // Spawn has already restored the native hull/movetype. Never restore an old
    // movement lease over those freshly initialized values.
    if(registry.get(slot)->paired)RecordControl(slot,ControlCause::spawn);
    controlled[slot]={};
    if(player_lives[slot]==UINT32_MAX||player_births[slot]==UINT32_MAX){registry.disconnect(slot);BridgeLog("Player life generation exhausted");return;}
    ++player_lives[slot];++player_births[slot];vitals_ack[slot]=0;next_snapshot=0;
    native_health_limits[slot]={};
    player_spawns[slot]=to_minecraft({player->v.origin.x,player->v.origin.y,player->v.origin.z+player->v.mins.z});
    const auto spawned=CaptureNativePlayer(static_cast<unsigned>(slot));
    host_effects.Spawn(slot);
    if(!CurrentNativePlayer(spawned))return;
    SendForm(slot);
    if(!CurrentNativePlayer(spawned))return;
    BridgeLog("CS player spawned: slot="+std::to_string(slot)+" life="+std::to_string(player_lives[slot]));
}
void GoldCraft_PlayerKilled(edict_t* player) {
    const auto slot=ENTINDEX(player);
    if(!edicts||slot<1||slot>max_clients)return;
    if(player!=edicts+slot)return;
    // Killed enters before ReGameDLL updates deadflag. Preserve presence, but
    // revoke equipment/creative/skill privileges before its callbacks execute.
    player_gameplay[slot].revoke_privileges();
    native_health_limits[slot]={};
    const auto killed=CaptureNativePlayer(static_cast<unsigned>(slot));
    host_effects.Forget(slot);
    if(!CurrentNativePlayer(killed))return;
    ReleaseControl(slot,ControlCause::death);
    if(CurrentNativePlayer(killed))next_snapshot=0;
}
bool GoldCraft_StatusGlowVisible(edict_t* entity,edict_t* viewer){return StatusGlowVisible(entity,viewer);}
void GoldCraft_StatusVisibility(entity_state_t* state,edict_t* entity){
    if(!state||!StatusInvisible(entity))return;
    state->rendermode=kRenderTransTexture;state->renderamt=0;state->renderfx=kRenderFxNone;
    state->effects&=~(EF_BRIGHTFIELD|EF_MUZZLEFLASH|EF_BRIGHTLIGHT|EF_DIMLIGHT|EF_LIGHT);
}
bool GoldCraft_StatusBreathing(edict_t* player){
    const auto* state=CurrentEnvironment(player);
    return state&&(state->flags&status_environment::water_breathing)!=0;
}
bool GoldCraft_StatusImmunity(edict_t* player,int bits){
    // The MC hurt pipeline has already applied its own tag rules and callbacks.
    if(player==final_minecraft_damage||player==minecraft_attack)return false;
    const auto* state=CurrentEnvironment(player);
    return state&&status_environment::immune(state->flags,bits,DMG_BURN|DMG_SLOWBURN,DMG_DROWN,DMG_FALL,DMG_ALWAYSGIB|DMG_NEVERGIB);
}
float GoldCraft_StatusMaxSpeed(edict_t* player,float base){
    auto* state=CurrentAttributes(player);
    if(!state||minecraft_forms[ENTINDEX(player)]||base<=1||player->v.iuser1)return base;
    return std::clamp(base*state->budget.state.speed,0.001f,10000.0f);
}
bool GoldCraft_StatusShield(edict_t* player,edict_t* inflictor,float damage,int bits){
    if(player==final_minecraft_damage||player==minecraft_attack||!std::isfinite(damage)||damage<=0
        ||!(bits&(DMG_BULLET|DMG_CLUB|DMG_SLASH))||!inflictor||inflictor==player||inflictor->free)return false;
    auto* state=CurrentAttributes(player);
    if(!state||!minecraft_forms[ENTINDEX(player)]||!controlled[ENTINDEX(player)].active
        ||!(state->budget.state.flags&1))return false;
    const bool creative=(state->budget.state.flags&2)!=0;
    if(!creative&&state->budget.durability()<=0)return false;
    const Vector toward=inflictor->v.origin-player->v.origin;
    const float angle=player->v.v_angle.y*0.017453292519943295f;
    if(toward.x*std::cos(angle)+toward.y*std::sin(angle)<=0)return false;
    const float mcDamage=damage/5.0f;
    const float durability=creative||mcDamage<3?0:1+std::floor(mcDamage);
    // A block is committed only after its durability notification is queued.
    return SpendStatus(*state,0,durability,mcDamage);
}
float GoldCraft_StatusDefense(edict_t* player,float damage,edict_t* inflictor,edict_t* attacker,int bits){
    if(player==final_minecraft_damage||player==minecraft_attack||!std::isfinite(damage)||damage<=0)return damage;
    const float enchantment=player==settling_native_armor?settling_native_enchantment:1;
    auto* state=CurrentAttributes(player);if(!state)return damage*enchantment;
    damage*=state->budget.state.resistance;
    damage*=enchantment;
    const float absorbed=std::min(damage/5.0f,state->budget.absorption());
    const float immediate=std::max(0.0f,damage-absorbed*5.0f);
    const StatusDamageOrigin origin{CaptureStatusSource(inflictor),CaptureStatusSource(attacker),bits,damage,immediate};
    if(absorbed>0&&SpendStatus(*state,absorbed,0,0,origin))damage=immediate;
    return damage;
}
bool GoldCraft_QueueNativeArmor(edict_t* player,float damage,edict_t* inflictor,edict_t* attacker,int bits,edict_t* logged_attacker,bool team_attack,int armor_hit){
    if(player==final_minecraft_damage||player==minecraft_attack)return false;
    return QueueNativeArmor(player,damage,inflictor,attacker,bits,logged_attacker,team_attack,armor_hit);
}
const char* GoldCraft_EquipmentWeapon(edict_t* player){return player==settling_native_armor?settling_native_weapon:nullptr;}
GoldCraft_EquipmentDamageScope::GoldCraft_EquipmentDamageScope() noexcept
    :player_(settling_native_armor),factor_(settling_native_enchantment),weapon_(settling_native_weapon){
    ++native_damage_depth;
    settling_native_armor=nullptr;settling_native_enchantment=1;settling_native_weapon=nullptr;
}
GoldCraft_EquipmentDamageScope::~GoldCraft_EquipmentDamageScope() noexcept{
    --native_damage_depth;
    settling_native_armor=player_;settling_native_enchantment=factor_;settling_native_weapon=weapon_;
}
bool GoldCraft_FinalMinecraftDamage(edict_t* player){return player&&player==final_minecraft_damage;}
bool GoldCraft_MinecraftAttack(edict_t* player){return player&&(player==final_minecraft_damage||player==minecraft_attack);}
bool GoldCraft_MinecraftFallAuthority(edict_t* player){
    if(!player||!edicts||!link.connected()||player==final_minecraft_damage)return false;
    const int slot=ENTINDEX(player);
    return slot>=1&&slot<=max_clients&&minecraft_forms[slot]&&controlled[slot].active
        &&gpGlobals->time-controlled[slot].last_update<=1.0f;
}
bool GoldCraft_ClientCommand(edict_t* player,const char* command) {
    if(std::strcmp(command,"goldcraft_edits")==0){
        const int slot=ENTINDEX(player);
        if(!edicts||slot<1||slot>max_clients||!edit_deliveries[slot].enabled||CMD_ARGC()!=2)return true;
        char* end=nullptr;const auto epoch=std::strtoull(CMD_ARGV(1),&end,10);
        auto& delivery=edit_deliveries[slot];
        if(!*CMD_ARGV(1)||*end||epoch!=registry.world()||gpGlobals->time<delivery.next_query)return true;
        delivery.revision=0;delivery.message.payload.clear();delivery.offset=0;delivery.next_query=gpGlobals->time+1;
        return true;
    }
    if(std::strcmp(command,"goldcraft_form")==0){
        const auto slot=ENTINDEX(player);
        if(!edicts||slot<1||slot>max_clients||CMD_ARGC()!=2||allow_switch.value==0)return true;
        int form=-1;
        if(std::strcmp(CMD_ARGV(1),"toggle")==0)form=minecraft_forms[slot]?0:1;
        else if(std::strcmp(CMD_ARGV(1),"0")==0)form=0;
        else if(std::strcmp(CMD_ARGV(1),"1")==0)form=1;
        if(form>=0&&gpGlobals->time>=next_form_change[slot]){
            next_form_change[slot]=gpGlobals->time+0.25f;SetForm(slot,form,ControlCause::form_command);
        }
        return true;
    }
    if(std::strcmp(command,"goldcraft_use")==0){
        const auto slot=ENTINDEX(player);
        if(!edicts||slot<1||slot>max_clients||!controlled[slot].active||CMD_ARGC()!=4)return true;
        char *end_epoch=nullptr,*end_life=nullptr,*end_value=nullptr;
        auto epoch=std::strtoull(CMD_ARGV(1),&end_epoch,10);auto life=std::strtoul(CMD_ARGV(2),&end_life,10);auto value=std::strtoul(CMD_ARGV(3),&end_value,10);
        if(!*CMD_ARGV(1)||!*CMD_ARGV(2)||!*CMD_ARGV(3)||*end_epoch||*end_life||*end_value||epoch!=registry.world()||life!=player_lives[slot]||value>1){++stale_use_commands;return true;}
        auto& state=controlled[slot];state.use_requested=value!=0;state.last_use=gpGlobals->time;
        // Dispatch the edge now; quick press/release commands can share one frame.
        if(state.use_requested!=state.use_applied){state.next_use=gpGlobals->time+0.05f;ApplyNativeUse(slot,state.use_requested);}
        return true;
    }
    if(std::strcmp(command,"goldcraft_ready")!=0)return false;
    const auto slot=ENTINDEX(player);
    if(edicts&&slot>=1&&slot<=max_clients&&registry.get(slot)&&player->pvPrivateData&&!player->free){
        const bool first=!client_ready[slot];client_ready[slot]=true;
        if(CMD_ARGC()==2&&CMD_ARGV(1)==std::to_string(protocol_version))edit_deliveries[slot].enabled=true;
        if(first)SendBinding(player);
    }
    return true;
}
void GoldCraft_EntityTouched(edict_t* entity,edict_t* other){
    if(!edicts||!entity||!other)return;
    int slot=ENTINDEX(other);
    if(slot>=1&&slot<=max_clients&&controlled[slot].active){
        ++touch_dispatches;auto& touch=entity_touches[ENTINDEX(entity)];
        if(touch.serial!=entity->serialnumber)touch={entity->serialnumber,0};
        ++touch.count;
    }
}
bool GoldCraft_ObjectAction(std::uint64_t key,unsigned action,edict_t* attacker,float amount,unsigned bits,const float* source){
    if(!edicts||!key||!link.connected()||!std::isfinite(amount)||amount<0||amount>1000000||action<1||action>2)return false;
    const int slot=attacker?ENTINDEX(attacker):0;
    const auto* identity=slot>=1&&slot<=max_clients?registry.get(slot):nullptr;
    if(action==2&&(!identity||!identity->paired))return false;
    Writer w;w.u64(registry.world());w.u64(++object_actions);w.u64(key);w.u32(action);
    w.u32(identity?slot:0);w.u32(identity?identity->serial:0);w.u32(identity?player_lives[slot]:0);w.f32(amount);w.u32(bits);
    for(int i=0;i<3;++i)w.f32(source[i]);
    if(action==1)++object_damage_actions;else ++object_use_actions;
    return link.send(Type::object_action,w.data);
}
float GoldCraft_StatusMaxHealth(edict_t* player,float native_maximum){
    if(!player||!edicts)return native_maximum;
    const auto* state=CurrentHealthLimit(static_cast<unsigned>(ENTINDEX(player)));
    return state?state->effective:native_maximum;
}
GoldCraft_FallAdjustment GoldCraft_StatusFall(edict_t* player,float velocity,float gravity){
    const auto* state=player?CurrentFall(static_cast<unsigned>(ENTINDEX(player))):nullptr;
    if(!state)return {velocity,1};
    return {status_fall::velocity(velocity,gravity,*state),state->multiplier};
}
void GoldCraft_StatusCategorize(void (*original)()){goldcraft::status_motion::MovementScope<playermove_t>::categorize(original);}
void GoldCraft_StatusJump(void (*original)()){goldcraft::status_motion::MovementScope<playermove_t>::jump(original);}
void GoldCraft_StatusWater(void (*original)()){goldcraft::status_motion::MovementScope<playermove_t>::water(original);}
void GoldCraft_FluidMove(playermove_s* move,int server,void (*original)(int)) {
    const unsigned slot=static_cast<unsigned>(move->player_index+1);
    const auto* field=slot<controlled.size()&&!controlled[slot].active?ActiveFluids():nullptr;
    fluids::MovementScope scope(*move,field);
    status_motion::MovementScope motion(*move,CurrentMotion(slot));original(server);
}
void GoldCraft_StartFrame() {
    if(!edicts) return;
    host_effects.Frame();
    try {
        GoldCraft_MapMiningFrame();
        UpdateGameplayRules();
        link.poll();
        if(link.error()!=previous_error) { previous_error=link.error(); if(!previous_error.empty()) BridgeLog(previous_error); }
        if(link.connected() && link.generation()!=connection_generation) {
            auto* const retiring_table=edicts;const auto retiring_world=registry.world(),retiring_authority=link.generation();
            effect_publisher.Reset();
            // A new authenticated authoritative connection has its own producer timeline.
            // The old epoch cannot inject poses into this connection, so release stale control
            // and allow a restarted Minecraft server to publish its initial sequences.
            for(int i=1;i<=max_clients;++i){
                const auto retiring=CaptureNativePlayer(static_cast<unsigned>(i));
                ReleaseControl(i,ControlCause::authority_generation);
                if(edicts!=retiring_table||registry.world()!=retiring_world||!link.connected()||link.generation()!=retiring_authority||
                   (retiring.table&&!CurrentNativePlayer(retiring)))return;
                controlled[i].sequence=0;
            }
            ResetFluids();
            GoldCraft_ObjectsReset(true);object_revision=0;
            ClearStatusHud();ClearStatusAttributes();ClearStatusEnvironment();ClearStatusMotion();ClearStatusFall();ClearStatusHealth();ClearStatusVisibility();ClearStatusVision();ClearStatusGlow();player_gameplay={};player_gameplay_supported=link.player_gameplay();RetireNativeArmor();
            if(edicts!=retiring_table||registry.world()!=retiring_world||!link.connected()||link.generation()!=retiring_authority)return;
            mob_damage_event=0;vitals_ack={};mining_events={};next_mining={};next_mining_sample={};mining_policy_sent=0;mining_materials_sent=false;next_snapshot=0;
            skill_damage_snapshots.clear();
            GoldCraft_MapMiningInvalidateSamples();
            edit_revision_sent=0;next_edit_query=0;
            connection_generation=link.generation();gameplay_rules_sent=0; SendWorld(); BridgeLog("Fabric authoritative server connected");
            GoldCraft_MapMiningResetExplosionSequence();
        }
        trace_queries=0;
        SendMiningPolicy();SendGameplayRules();
        auto* const dispatch_table=edicts;
        const auto dispatch_world=registry.world(),dispatch_authority=connection_generation;
        const bool dispatch_connected=link.connected();const auto dispatch_link_generation=link.generation();
        for(auto& message : link.take_messages()) {
            Reader r(message.payload);
            if(message.type==Type::pair_player)HandlePair(r);
            else if(message.type==Type::trace_query)HandleTrace(r);
            else if(message.type==Type::authoritative_pose)HandlePose(r);
            else if(message.type==Type::effect_pose){
                if(!link.effect_position_hold())continue;
                const auto proof=effects::decode_pose_proof(r);HandlePose(r,&proof);
            }
            else if(message.type==Type::effect_reopen){
                if(link.effect_position_hold())effect_publisher.Reopen(effects::decode_reopen(r));
            }
            else if(message.type==Type::native_armor_result)HandleNativeArmorResult(message.payload);
            else if(message.type==Type::status_spend)HandleStatusSpendResult(message.payload);
            else if(message.type==Type::status_attributes)HandleStatusAttributes(message.payload);
            else if(message.type==Type::status_vision)HandleStatusVision(message.payload);
            else if(message.type==Type::status_glow)HandleStatusGlow(message.payload);
            else if(message.type==Type::status_visibility)HandleStatusVisibility(message.payload);
            else if(message.type==Type::status_fall)HandleStatusFall(message.payload);
            else if(message.type==Type::status_health)HandleStatusHealth(message.payload);
            else if(message.type==Type::status_motion)HandleStatusMotion(message.payload);
            else if(message.type==Type::status_environment)HandleStatusEnvironment(message.payload);
            else if(message.type==Type::status_hud)HandleStatusHud(message.payload);
            else if(message.type==Type::damage_request)HandleMobDamage(message.payload);
            else if(message.type==Type::player_attack)HandleMobDamage(message.payload,true);
            else if(message.type==Type::player_gameplay)HandlePlayerGameplay(message.payload);
            else if(message.type==Type::vitals_delta)HandleVitals(message.payload);
            else if(message.type==Type::map_mining_request)HandleMining(message.payload);
            else if(message.type==Type::map_mining_sample)HandleMining(message.payload,true);
            else if(message.type==Type::host_explosion){
                if(!link.host_explosions()) throw ProtocolError("Unnegotiated host explosion");
                GoldCraft_MapHostExplosion(explosion::decode(message.payload));
            }
            else if(message.type==Type::map_edit_query){
                const auto epoch=r.u64();r.finish();
                if(epoch==registry.world()&&gpGlobals->time>=next_edit_query){edit_revision_sent=0;next_edit_query=gpGlobals->time+1;}
            }
            else if(message.type==Type::fluid_volumes)AcceptFluids(message.payload);
            else if(message.type==Type::minecraft_objects){
                auto snapshot=decode_world_objects(message.payload);
                if(snapshot.epoch==registry.world()&&snapshot.revision>object_revision){GoldCraft_ObjectsUpdate(snapshot);object_revision=snapshot.revision;}
            }
            // Touch/Use in a dispatched message may retire the whole world or
            // authority. Its old batch must not continue into that replacement.
            if(edicts!=dispatch_table||registry.world()!=dispatch_world||connection_generation!=dispatch_authority||
               !link.connected()||link.generation()!=dispatch_authority)return;
        }
        ExpireNativeArmor();
        if(edicts!=dispatch_table||registry.world()!=dispatch_world||connection_generation!=dispatch_authority
            ||link.connected()!=dispatch_connected||link.generation()!=dispatch_link_generation)return;
        SendStatusGlow();
        ExpireStatusHud();
        GoldCraft_ObjectsExpire();
        SendEdits();
        if(gpGlobals->time>=next_colliders){next_colliders=gpGlobals->time+0.05f;SendColliders();SendFluids();}
        auto* const controlling_table=edicts;
        const auto controlling_world=registry.world(),controlling_authority=connection_generation,controlling_link=link.generation();
        const bool controlling_connected=link.connected();
        for(int i=1;i<=max_clients;i++)if(controlled[i].active){
            auto& state=controlled[i];
            if(!link.connected()||gpGlobals->time-state.last_update>1.0f){
                ReleaseControl(i,link.connected()?ControlCause::pose_stale:ControlCause::authority_link_down);
                if(edicts!=controlling_table||registry.world()!=controlling_world||connection_generation!=controlling_authority||
                   link.generation()!=controlling_link||link.connected()!=controlling_connected)return;
                continue;
            }
            if(gpGlobals->time-state.last_use>0.35f)state.use_requested=false;
            if(gpGlobals->time>=state.next_use||state.use_requested!=state.use_applied){
                state.next_use=gpGlobals->time+0.05f;ApplyNativeUse(i,state.use_requested);
                if(edicts!=controlling_table||registry.world()!=controlling_world||connection_generation!=controlling_authority||
                   link.generation()!=controlling_link||link.connected()!=controlling_connected)return;
            }
        }
        effect_publisher.Frame();
        if(gpGlobals->time>=next_binding) {
            next_binding=gpGlobals->time+2;
            for(int i=1;i<=max_clients;++i) if(!edicts[i].free && registry.get(i)) SendBinding(&edicts[i]);
            if(link.connected())for(int i=1;i<=max_clients;++i)SendAvatar(i,true);
        }
        if(link.connected() && gpGlobals->time>=next_snapshot) {
            next_snapshot=gpGlobals->time+0.05f; SendActors();SendBrushes();
#ifdef GOLDCRAFT_HEADLESS_FIXTURE
            WriteHeadlessBindings();
#endif
        }
    } catch(const std::exception& e) { BridgeLog(e.what()); }
}
