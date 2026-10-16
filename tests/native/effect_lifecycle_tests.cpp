// Compile only in a separately authorized window, after extracting the exact
// frozen bridge blocks. Engine/Pawn callbacks below are boundary substitutes.
#include <array>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <string>
#include <utility>

namespace {
using Key=std::array<std::uint8_t,16>;
struct Vector {
    float x=0,y=0,z=0;
    Vector()=default;
    Vector(float a,float b,float c):x(a),y(b),z(c){}
    Vector operator-(const Vector& other)const{return {x-other.x,y-other.y,z-other.z};}
    float LengthSquared()const{return x*x+y*y+z*z;}
};
struct Vec3 {float x=0,y=0,z=0;};
Vec3 to_minecraft(Vec3 v){return {v.x/32,v.z/32+64,-v.y/32};}
constexpr int DEAD_NO=0,FL_KILLME=1,FL_SPECTATOR=2,FL_DUCKING=4,IN_USE=32,MOVETYPE_WALK=3;
const Vector g_vecZero{},VEC_DUCK_HULL_MIN{-16,-16,-18},VEC_DUCK_HULL_MAX{16,16,18},
    VEC_HULL_MIN{-16,-16,-36},VEC_HULL_MAX{16,16,36},VEC_DUCK_VIEW{0,0,12},VEC_VIEW{0,0,28};
std::function<void()> on_alive,on_use,on_size,on_origin,on_retire,on_birth,on_form,on_avatar;
void once(std::function<void()>& hook){auto callback=std::move(hook);hook={};if(callback)callback();}
struct CBasePlayer {
    int m_iTeam=2,m_afButtonPressed=0,m_afButtonReleased=0;
    float m_flFallVelocity=40;
    bool IsAlive(){once(on_alive);return true;}
    void PlayerUse(){once(on_use);}
} player,replacement_player;
struct Entvars {
    Vector origin{64,-96,164},mins{0,0,-36},maxs{16,16,36},size{32,32,72},velocity{1,2,3},view_ofs{};
    int flags=0,deadflag=DEAD_NO,iuser1=0,movetype=0,button=8,oldbuttons=9;
    float health=100;
};
struct edict_t {bool free=false;int serialnumber=0;void* pvPrivateData=&player;Entvars v;};
std::array<edict_t,3> first_entities,second_entities;
edict_t* edicts=first_entities.data();int max_clients=1;
#define GET_PRIVATE(entity) ((entity)->pvPrivateData)
struct Directory {
    struct Pair {std::uint32_t serial=5;bool paired=true;Key uuid{1};};
    std::optional<Pair> binding=Pair{};
    std::uint64_t epoch=7;
    std::uint64_t world()const{return epoch;}
    const Pair* get(unsigned slot)const{return slot==1&&binding?&*binding:nullptr;}
    void disconnect(unsigned slot){if(slot==1)binding.reset();}
} registry;
struct ControlState {
    std::uint64_t sequence=0,use_dispatch_token=0;
    bool active=false,use_requested=false,use_applied=false,use_dispatching=false;
    int saved_movetype=MOVETYPE_WALK;
    float last_update=0,last_use=0,next_use=0;
};
std::array<ControlState,65> controlled;
std::array<std::uint32_t,65> player_lives;
std::array<bool,65> minecraft_forms;
std::array<Vec3,65> player_spawns;
std::uint64_t use_presses=0,use_releases=0,use_calls=0,host_relocations=0,form_changes=0,connection_generation=11;
float next_snapshot=10;
struct Link {
    bool online=true;std::uint64_t current=11;
    bool connected()const{return online;}
    bool effect_position_hold()const{return true;}
    std::uint64_t local_qpc_frequency()const{return 1000000;}
    std::uint64_t generation()const{return current;}
} link;
enum class ControlCause {relocation,form_change,form_amxx,death,disconnect};
int retire_calls=0,birth_calls=0,size_calls=0,origin_calls=0,form_calls=0,avatar_calls=0,log_calls=0,listener_calls=0;
struct HostEffects {
    void Retire(unsigned){++retire_calls;once(on_retire);}
    void Birth(unsigned){++birth_calls;once(on_birth);}
} host_effects;
struct Publisher {void Retire(unsigned){}} effect_publisher;
void RecordControl(unsigned,ControlCause,std::optional<int> =std::nullopt,int =0){}
void BridgeLog(const std::string&){++log_calls;}
void SendForm(unsigned){++form_calls;once(on_form);}
void SendAvatar(unsigned,bool){++avatar_calls;once(on_avatar);}
void (*form_listener)(int,int,int)=nullptr;
int (*form_guard)(int,int)=nullptr;
std::array<bool,65> form_guard_busy{};
void SET_SIZE(edict_t* e,const Vector& mins,const Vector& maxs){++size_calls;e->v.mins=mins;e->v.maxs=maxs;e->v.size=maxs-mins;once(on_size);}
void SET_ORIGIN(edict_t* e,const Vector& origin){++origin_calls;e->v.origin=origin;once(on_origin);}
void AcceptNativeRelocation(unsigned slot,std::uint64_t authority=0);
// The include is generated from the frozen bridge, not a fixture rewrite.
#include "effect_lifecycle_bridge.inc"

int checks=0,failures=0;
void expect(bool value,const char* expression,int line){++checks;if(!value){++failures;std::cerr<<line<<": "<<expression<<'\n';}}
#define CHECK(expression) expect(static_cast<bool>(expression),#expression,__LINE__)
void reset(){
    form_guard=nullptr;form_guard_busy={};
    player={};replacement_player={};first_entities={};second_entities={};edicts=first_entities.data();max_clients=1;
    registry={};link={};connection_generation=11;controlled={};player_lives={};minecraft_forms={};player_spawns={};
    native_release_leases={};
    controlled[1].active=controlled[1].use_applied=controlled[1].use_requested=true;controlled[1].saved_movetype=3;
    player_lives[1]=10;minecraft_forms[1]=true;player_spawns[1]={-777,-777,-777};next_snapshot=10;
    use_presses=use_releases=use_calls=host_relocations=form_changes=0;
    retire_calls=birth_calls=size_calls=origin_calls=form_calls=avatar_calls=log_calls=listener_calls=0;
    on_alive=on_use=on_size=on_origin=on_retire=on_birth=on_form=on_avatar={};form_listener=nullptr;
}
void replacement(bool change_world){
    if(change_world){edicts=second_entities.data();++registry.epoch;}
    ++registry.binding->serial;edicts[1].pvPrivateData=&replacement_player;
    player_lives[1]=50;controlled[1]={};controlled[1].active=controlled[1].use_requested=true;
    controlled[1].sequence=88;controlled[1].next_use=99;
    controlled[1].use_dispatching=true;controlled[1].use_dispatch_token=++native_use_dispatch_generation;
    player_spawns[1]={-888,-888,-888};
}
void unchanged_relocation_restores_native_hull_and_advances_once(){
    reset();AcceptNativeRelocation(1,11);
    CHECK(player_lives[1]==11&&host_relocations==1&&birth_calls==1&&form_calls==1&&log_calls==1);
    CHECK(!controlled[1].active&&!controlled[1].use_requested&&!controlled[1].use_dispatching);
    CHECK(controlled[1].use_dispatch_token==0&&first_entities[1].v.movetype==3);
    CHECK(size_calls==1&&origin_calls==1&&use_calls==1&&use_releases==1);
    CHECK(first_entities[1].v.button==8&&player.m_afButtonPressed==0&&player.m_afButtonReleased==0);
}
void player_use_cannot_continue_into_dead_respawned_or_replaced_identity(){
    for(unsigned kind=0;kind<7;++kind){
        reset();on_use=[kind]{
            if(kind==0){edicts=nullptr;registry.epoch=0;}
            if(kind==1)replacement(true);
            if(kind==2)replacement(false);
            if(kind==3){++player_lives[1];controlled[1]={};}
            if(kind==4){first_entities[1].v.deadflag=1;first_entities[1].v.health=0;}
            if(kind==5)player.m_iTeam=1;
            if(kind==6){registry.disconnect(1);controlled[1]={};}
        };
        AcceptNativeRelocation(1,11);
        CHECK(host_relocations==0&&birth_calls==0&&size_calls==0&&origin_calls==0&&form_calls==0);
        if(kind==1||kind==2){
            CHECK(player_lives[1]==50&&controlled[1].sequence==88&&controlled[1].next_use==99);
            CHECK(controlled[1].use_requested&&controlled[1].use_dispatching&&player_spawns[1].x==-888);
        }else CHECK(player_lives[1]==(kind==3?11u:10u));
        if(kind==4||kind==5)CHECK(!controlled[1].use_dispatching&&controlled[1].use_dispatch_token==0);
    }
}
void virtual_alive_override_is_also_a_callback_boundary(){
    reset();on_alive=[]{replacement(true);};ApplyNativeUse(1,false);
    CHECK(use_calls==0&&size_calls==0&&origin_calls==0&&birth_calls==0);
    CHECK(controlled[1].sequence==88&&controlled[1].use_requested&&controlled[1].use_dispatching);
}
void release_engine_hooks_cannot_resume_old_physical_writes(){
    reset();on_size=[]{replacement(true);};AcceptNativeRelocation(1,11);
    CHECK(size_calls==1&&origin_calls==0&&host_relocations==0&&birth_calls==0);
    CHECK(controlled[1].sequence==88&&player_lives[1]==50);
    reset();on_origin=[]{++player_lives[1];controlled[1]={};};AcceptNativeRelocation(1,11);
    CHECK(size_calls==1&&origin_calls==1&&player_lives[1]==11&&host_relocations==0&&birth_calls==0);
    reset();on_size=[]{first_entities[1].v.origin.x+=32;};AcceptNativeRelocation(1,11);
    CHECK(size_calls==1&&origin_calls==0&&host_relocations==1&&birth_calls==1);
    CHECK(first_entities[1].v.origin.x==96&&player_spawns[1].x==3);
}
void nested_form_change_is_not_overwritten_by_outer_relocation(){
    for(const bool ducked:{false,true}){
        reset();int result=0;
        first_entities[1].v.mins={-12,-12,-36};first_entities[1].v.maxs={12,12,20};
        if(ducked)first_entities[1].v.flags|=FL_DUCKING;
        on_use=[&]{
            result=SetForm(1,0,ControlCause::form_amxx);
            // Native work performed after the nested form transition belongs
            // to its new life; the old Release must not zero or reposition it.
            first_entities[1].v.velocity={7,8,9};first_entities[1].v.origin.x+=32;
        };
        AcceptNativeRelocation(1,11);
        const auto& v=first_entities[1].v;
        CHECK(result==1&&!minecraft_forms[1]&&player_lives[1]==11&&form_changes==1);
        CHECK(host_relocations==0&&birth_calls==1&&form_calls==1&&avatar_calls==1);
        CHECK(!controlled[1].active&&!controlled[1].use_dispatching&&!controlled[1].use_applied&&controlled[1].use_dispatch_token==0);
        CHECK(v.movetype==MOVETYPE_WALK&&native_release_leases[1].token==0&&size_calls==1&&origin_calls==1);
        CHECK((v.mins-(ducked?VEC_DUCK_HULL_MIN:VEC_HULL_MIN)).LengthSquared()==0);
        CHECK((v.maxs-(ducked?VEC_DUCK_HULL_MAX:VEC_HULL_MAX)).LengthSquared()==0);
        CHECK((v.size-(v.maxs-v.mins)).LengthSquared()==0&&(v.view_ofs-(ducked?VEC_DUCK_VIEW:VEC_VIEW)).LengthSquared()==0);
        CHECK(v.origin.x==96&&v.origin.y==-96&&v.origin.z+v.mins.z==128);
        CHECK((v.velocity-Vector{7,8,9}).LengthSquared()==0&&player.m_flFallVelocity==0&&v.button==0);
        CHECK(player_spawns[1].x==2&&player_spawns[1].y==68&&player_spawns[1].z==3);
    }
    reset();on_use=[]{replacement(false);};
    CHECK(SetForm(1,0,ControlCause::form_amxx)==-1);
    CHECK(minecraft_forms[1]&&player_lives[1]==50&&form_changes==0&&birth_calls==0);
}
void pending_restore_cannot_write_a_new_body_or_advance_a_partly_restored_life(){
    for(const bool replace:{false,true}){
        reset();on_use=[=]{
            if(replace)replacement(false);
            else{++player_lives[1];controlled[1]={};}
            edicts[1].v.movetype=9;edicts[1].v.origin={101,102,103};
            edicts[1].v.mins={-5,-6,-7};edicts[1].v.maxs={5,6,7};edicts[1].v.view_ofs={1,2,3};
        };
        AcceptNativeRelocation(1,11);
        CHECK(size_calls==0&&origin_calls==0&&host_relocations==0&&birth_calls==0&&native_release_leases[1].token==0);
        CHECK(edicts[1].v.movetype==9&&(edicts[1].v.origin-Vector{101,102,103}).LengthSquared()==0);
        CHECK((edicts[1].v.mins-Vector{-5,-6,-7}).LengthSquared()==0&&(edicts[1].v.view_ofs-Vector{1,2,3}).LengthSquared()==0);
    }
    for(const bool in_origin:{false,true}){
        reset();int result=0;
        auto nested=[&]{result=SetForm(1,0,ControlCause::form_amxx);CHECK(player_lives[1]==10);};
        if(in_origin)on_origin=nested;else on_size=nested;
        AcceptNativeRelocation(1,11);
        CHECK(result==-1&&minecraft_forms[1]&&player_lives[1]==11&&form_changes==0&&host_relocations==1);
        CHECK(size_calls==1&&origin_calls==1&&first_entities[1].v.movetype==MOVETYPE_WALK&&native_release_leases[1].token==0);
        CHECK((first_entities[1].v.mins-VEC_HULL_MIN).LengthSquared()==0&&first_entities[1].v.origin.z+first_entities[1].v.mins.z==128);
        reset();result=0;
        auto during_release=[&]{
            result=SetForm(1,0,ControlCause::form_amxx);
            AcceptNativeRelocation(1,11);
            CHECK(player_lives[1]==10&&retire_calls==0);
        };
        if(in_origin)on_origin=during_release;else on_size=during_release;
        CHECK(ReleaseControl(1,ControlCause::disconnect));
        CHECK(result==-1&&minecraft_forms[1]&&player_lives[1]==10&&retire_calls==0&&birth_calls==0&&host_relocations==0);
        CHECK(first_entities[1].v.movetype==MOVETYPE_WALK&&size_calls==1&&origin_calls==1&&native_release_leases[1].token==0);
    }
}
void provider_failure_and_notifications_fence_the_remaining_stack(){
    reset();on_retire=[]{replacement(true);};AcceptNativeRelocation(1,11);
    CHECK(use_calls==0&&size_calls==0&&birth_calls==0&&host_relocations==0);
    reset();on_birth=[]{replacement(true);};AcceptNativeRelocation(1,11);
    CHECK(host_relocations==1&&birth_calls==1&&form_calls==0&&log_calls==0&&player_spawns[1].x==-888);
    reset();on_form=[]{replacement(true);};AcceptNativeRelocation(1,11);
    CHECK(form_calls==1&&log_calls==0);
    reset();form_listener=[](int,int,int){++listener_calls;};on_avatar=[]{replacement(true);};
    CHECK(SetForm(1,0,ControlCause::form_amxx)==1);
    CHECK(form_changes==1&&avatar_calls==1&&listener_calls==0);
}
void proof_authority_fences_post_pose_without_blocking_native_release(){
    reset();on_use=[]{link.online=false;};AcceptNativeRelocation(1,11);
    CHECK(!controlled[1].active&&size_calls==1&&origin_calls==1);
    CHECK(player_lives[1]==10&&host_relocations==0&&birth_calls==0);
    reset();on_use=[]{++link.current;connection_generation=link.current;};AcceptNativeRelocation(1,11);
    CHECK(!controlled[1].active&&host_relocations==0&&birth_calls==0);
    reset();link.online=false;ReleaseControl(1,ControlCause::disconnect);
    CHECK(!controlled[1].active&&size_calls==1&&origin_calls==1&&first_entities[1].v.movetype==3);
    CHECK(CurrentPoseAuthority(0)&&!CurrentPoseAuthority(11));
}
void freed_slot_ledger_guard_does_not_require_a_live_player(){
    reset();first_entities[1].free=true;
    const auto slot=CaptureNativeSlot(1);CHECK(CurrentNativeSlot(slot));
    CHECK(CurrentNativePlayer(CaptureNativePlayer(1))==nullptr);
    ++registry.binding->serial;CHECK(!CurrentNativeSlot(slot));
    reset();const auto world_slot=CaptureNativeSlot(1);replacement(true);CHECK(!CurrentNativeSlot(world_slot));
}
int guard_calls=0,nested_guard_result=0;
void form_policy_veto_reentrancy_and_identity_are_fenced(){
    reset();minecraft_forms[1]=false;guard_calls=0;
    form_guard=[](int,int){++guard_calls;return 1;};
    CHECK(SetForm(1,1,ControlCause::form_amxx)==-5);
    CHECK(guard_calls==1&&!minecraft_forms[1]&&player_lives[1]==10&&retire_calls==0&&!form_guard_busy[1]);
    // Exiting MC remains available even while the infection guard denies entry.
    minecraft_forms[1]=true;
    CHECK(SetForm(1,0,ControlCause::form_amxx)==1);
    CHECK(guard_calls==1&&!minecraft_forms[1]);

    reset();minecraft_forms[1]=false;guard_calls=0;nested_guard_result=0;
    form_guard=[](int slot,int form){++guard_calls;nested_guard_result=SetForm(slot,form,ControlCause::form_amxx);return 0;};
    CHECK(SetForm(1,1,ControlCause::form_amxx)==1);
    CHECK(nested_guard_result==-5&&guard_calls==1&&minecraft_forms[1]&&player_lives[1]==11&&!form_guard_busy[1]);

    for(bool world:{false,true}){
        reset();minecraft_forms[1]=false;
        form_guard=world?+[](int,int){replacement(true);return 0;}:+[](int,int){replacement(false);return 0;};
        CHECK(SetForm(1,1,ControlCause::form_amxx)==-1);
        CHECK(!minecraft_forms[1]&&player_lives[1]==50&&controlled[1].sequence==88&&retire_calls==0&&!form_guard_busy[1]);
    }
    reset();minecraft_forms[1]=false;
    form_guard=[](int,int){link.online=false;return 0;};
    CHECK(SetForm(1,1,ControlCause::form_amxx)==-3);
    CHECK(!minecraft_forms[1]&&player_lives[1]==10);
}
} // namespace
int main(){
    unchanged_relocation_restores_native_hull_and_advances_once();
    player_use_cannot_continue_into_dead_respawned_or_replaced_identity();
    virtual_alive_override_is_also_a_callback_boundary();
    release_engine_hooks_cannot_resume_old_physical_writes();
    nested_form_change_is_not_overwritten_by_outer_relocation();
    pending_restore_cannot_write_a_new_body_or_advance_a_partly_restored_life();
    provider_failure_and_notifications_fence_the_remaining_stack();
    proof_authority_fences_post_pose_without_blocking_native_release();
    freed_slot_ledger_guard_does_not_require_a_live_player();
    form_policy_veto_reentrancy_and_identity_are_fenced();
    std::cout<<"exact bridge lifecycle blocks with engine/Pawn substitutes: "<<checks<<" assertions, "<<failures
             <<" failures; no live ReHLDS or Pawn VM acceptance\n";
    return failures?1:0;
}
