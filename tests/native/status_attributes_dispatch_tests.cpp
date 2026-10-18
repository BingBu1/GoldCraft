#include "goldcraft/status_attributes.hpp"
#include "../../native/server/bridge.h"
#include <array>
#include <cmath>
#include <iostream>
#include <map>
#include <functional>
using namespace goldcraft;
struct Vector {float x=0,y=0,z=0;Vector operator-(const Vector& r)const{return{x-r.x,y-r.y,z-r.z};}};
constexpr int DAMAGE_NO=0,DEAD_NO=0;
struct entvars_t {int takedamage=1,deadflag=0,iuser1=0;Vector origin,v_angle;float health=100;};
edict_s* fixture_entity(entvars_t*);
struct CBaseMonster {
    entvars_t* pev=nullptr;bool alive=true;int calls=0,bits=0;float damage=0;
    entvars_t* inflictor=nullptr;entvars_t* attacker=nullptr;
    void TakeDamage(entvars_t* source,entvars_t* owner,float amount,int flags){
        ++calls;inflictor=source;attacker=owner;bits=flags;damage+=amount;pev->health-=amount;GoldCraft_NativeDamageCommitted(fixture_entity(pev),amount);
        if(pev->health<=0)alive=false;
    }
};
std::function<void()> alive_hook;
struct CBasePlayer:CBaseMonster {bool IsAlive()const{if(alive_hook)alive_hook();return alive;}};
struct edict_s {bool free=false;int serialnumber=1;entvars_t v;CBasePlayer player;};
using edict_t=edict_s;
std::array<edict_t,65> storage;
edict_s* fixture_entity(entvars_t* v){for(auto& e:storage)if(&e.v==v)return &e;return nullptr;}
edict_t* edicts=storage.data();int max_clients=2;
int ENTINDEX(edict_t* e){return static_cast<int>(e-edicts);}
template<class T>T* GET_PRIVATE(edict_t* e){return &e->player;}
struct Binding{unsigned serial=3;};
struct{Binding pair,source;unsigned worldValue=1;const Binding* get(unsigned slot){return slot==1?&pair:slot==2?&source:nullptr;}unsigned world(){return worldValue;}}registry;
struct{bool capable=true,queue=true,hit_capable=true;std::uint64_t gen=1;std::vector<Bytes> packets,hits;bool native_hit(){return capable&&hit_capable;}std::uint64_t generation(){return gen;}bool status_attributes(){return capable;}
    bool send(Type type,std::span<const std::uint8_t> b){if(!queue)return false;if(type==Type::native_hit){hits.emplace_back(b.begin(),b.end());return true;}packets.emplace_back(b.begin(),b.end());return type==Type::status_spend;}}link;
struct{float time=1;}globals;auto* gpGlobals=&globals;
std::array<unsigned,65> player_births,player_lives;
std::array<bool,65> minecraft_forms;
struct Control{bool active=false;};std::array<Control,65>controlled;
edict_t* final_minecraft_damage=nullptr;edict_t* minecraft_attack=nullptr;
float next_snapshot=1;
struct NativeIdentity {CBasePlayer* player;unsigned slot,serial,spawn,life,world;int engineSerial;bool form;};
NativeIdentity CaptureNativePlayer(unsigned slot){
    return {&edicts[slot].player,slot,registry.get(slot)->serial,player_births[slot],player_lives[slot],registry.world(),edicts[slot].serialnumber,minecraft_forms[slot]};
}
bool CurrentNativePlayer(const NativeIdentity& i){return !edicts[i.slot].free&&i.world==registry.world()&&i.serial==registry.get(i.slot)->serial
    &&i.spawn==player_births[i.slot]&&i.life==player_lives[i.slot]&&i.engineSerial==edicts[i.slot].serialnumber&&i.form==minecraft_forms[i.slot];}
constexpr int DMG_BULLET=2,DMG_CLUB=128,DMG_SLASH=4;
std::uint64_t tick=10;
namespace {unsigned NativeArmorKind(int,edict_t*,edict_t*){return 2;}}
#include "../../native/server/native_hit.inc"
#include "../../native/server/status_attributes.inc"
// This fixture has no negotiated equipment peer; preserve the status-only fallback.
edict_t* settling_native_armor=nullptr;float settling_native_enchantment=1;
// Preparation extracts the actual three bridge hook implementations verbatim.
#include "actual_status_attribute_functions.inc"
static Bytes state(std::uint64_t revision,float speed,float resistance,float absorption,float durability,unsigned flags=1,std::uint64_t ack=0){
    Writer w;w.u64(1);w.u64(revision);w.u64(ack);w.u64(8);w.u32(1);w.u32(3);w.u32(player_births[1]);w.u32(player_lives[1]);w.u32(flags);
    w.f32(speed);w.f32(resistance);w.f32(absorption);w.f32(durability);return w.data;
}
static Bytes receipt(std::uint64_t event,float paid,unsigned spawn=5){
    Writer w;w.u64(1);w.u64(event);w.u32(1);w.u32(3);w.u32(spawn);w.f32(paid);return w.data;
}
int main(){try{
    int count=0;auto check=[&](bool value){if(!value)throw std::runtime_error("dispatch check "+std::to_string(count+1));++count;};
    auto* victim=edicts+1;auto* source=edicts+2;
    for(auto& e:storage)e.player.pev=&e.v;
    player_births[1]=5;player_lives[1]=6;player_births[2]=7;
    HandleStatusAttributes(state(1,.85f,.8f,4,10));
    check(std::abs(GoldCraft_StatusMaxSpeed(victim,240)-204)<.001f);
    check(std::abs(GoldCraft_StatusMaxSpeed(victim,240)-204)<.001f); // no cumulative mutation
    check(GoldCraft_StatusMaxSpeed(victim,1)==1); // ZP frost/freeze
    victim->v.iuser1=1;check(GoldCraft_StatusMaxSpeed(victim,240)==240);victim->v.iuser1=0;
    HandleStatusAttributes(state(2,1.2f,.8f,4,10));check(GoldCraft_StatusMaxSpeed(victim,240)==288);
    check(GoldCraft_StatusDefense(victim,20,source,source,DMG_BULLET)==0); // resistance -> 16, pool 20
    check(std::abs(native_attributes[1].budget.absorption()-.8f)<.001f);
    HandleStatusAttributes(state(3,1.2f,.8f,4,10)); // old unacked pool
    check(std::abs(GoldCraft_StatusDefense(victim,20,source,source,DMG_BULLET)-12)<.001f);
    check(native_attributes[1].budget.absorption()==0);
    check(GoldCraft_StatusDefense(victim,20,source,source,DMG_BULLET)==16);
    minecraft_attack=victim;check(GoldCraft_StatusDefense(victim,20,source,source,DMG_BULLET)==20);minecraft_attack=nullptr;
    final_minecraft_damage=victim;check(GoldCraft_StatusDefense(victim,20,source,source,DMG_BULLET)==20);final_minecraft_damage=nullptr;
    minecraft_forms[1]=true;controlled[1].active=true;
    check(GoldCraft_StatusMaxSpeed(victim,240)==240); // actual MC movement already modified
    source->v.origin={100,0,0};check(GoldCraft_StatusShield(victim,source,50,DMG_BULLET));
    check(native_attributes[1].budget.durability()==0);
    check(!GoldCraft_StatusShield(victim,source,10,DMG_BULLET)); // broke on prior block
    ClearStatusAttributes();HandleStatusAttributes(state(4,1,1,4,336));
    source->v.origin={-100,0,0};check(!GoldCraft_StatusShield(victim,source,50,DMG_BULLET));
    source->v.origin={0,100,0};check(!GoldCraft_StatusShield(victim,source,50,DMG_BULLET));
    victim->v.v_angle.y=90;check(GoldCraft_StatusShield(victim,source,50,DMG_BULLET));
    HandleStatusAttributes(state(5,1,1,4,336,0));check(!GoldCraft_StatusShield(victim,source,50,DMG_BULLET));
    HandleStatusAttributes(state(6,1,1,4,336));
    link.queue=false;check(!GoldCraft_StatusShield(victim,source,50,DMG_BULLET));
    check(GoldCraft_StatusDefense(victim,20,source,source,DMG_BULLET)==20);link.queue=true; // never silently consume without receipt
    globals.time+=.36f;check(!GoldCraft_StatusShield(victim,source,50,DMG_BULLET));
    minecraft_forms[1]=false;check(GoldCraft_StatusMaxSpeed(victim,240)==240);
    HandleStatusAttributes(state(7,.85f,.8f,4,336));player_lives[1]++;
    check(GoldCraft_StatusDefense(victim,20,source,source,DMG_BULLET)==20); // stale form lease
    player_lives[1]--;player_births[1]++;check(GoldCraft_StatusDefense(victim,20,source,source,DMG_BULLET)==20);
    player_births[1]--;registry.pair.serial++;check(GoldCraft_StatusDefense(victim,20,source,source,DMG_BULLET)==20);
    registry.pair.serial--;victim->player.alive=false;check(GoldCraft_StatusDefense(victim,20,source,source,DMG_BULLET)==20);
    victim->player.alive=true;link.capable=false;check(GoldCraft_StatusDefense(victim,20,source,source,DMG_BULLET)==20);
    // Test actual result dispatch against an engine health/death sink. These
    // tests do not substitute for ReHLDS Killed/ReAPI/ZP integration evidence.
    auto reset=[&](float pool=4.0f){
        link.capable=link.queue=true;victim->player.alive=true;victim->player.calls=0;victim->player.damage=0;victim->v.health=100;
        victim->player.attacker=victim->player.inflictor=nullptr;victim->free=source->free=false;
        player_births[1]=5;player_lives[1]=6;registry.pair.serial=registry.source.serial=3;
        minecraft_forms[1]=false;alive_hook={};ClearStatusAttributes();link.packets.clear();
        HandleStatusAttributes(state(1,1,1,pool,336));
    };
    auto hit=[&](float amount){return GoldCraft_StatusDefense(victim,amount,source,source,DMG_BULLET);};
    auto rejected=[&](const Bytes& b){bool caught=false;try{HandleStatusSpendResult(b);}catch(const ProtocolError&){caught=true;}check(caught);};
    reset();check(hit(20)==0);HandleStatusSpendResult(receipt(1,4));
    check(victim->player.calls==0&&native_attributes[1].settled==1);
    check(native_attributes[1].budget.absorption()==0); // reservation until ack snapshot
    HandleStatusAttributes(state(2,1,1,0,336,1,1));check(native_attributes[1].budget.pending.empty());
    reset();check(hit(20)==0);
    HandleStatusAttributes(state(2,1,1,4,336,1,1));check(native_attributes[1].budget.state.revision==1); // cannot retire debt before receipt
    rejected(receipt(2,0));rejected(receipt(1,4.1f));
    check(native_attributes[1].settled==0&&victim->player.calls==0);
    HandleStatusSpendResult(receipt(1,1.5f));check(victim->v.health==88&&victim->player.calls==1);
    check(victim->player.attacker==&source->v&&victim->player.inflictor==&source->v&&victim->player.bits==DMG_BULLET);
    HandleStatusSpendResult(receipt(1,0));check(victim->v.health==88&&victim->player.calls==1); // duplicate cannot debit
    HandleStatusAttributes(state(3,1,1,0,336,1,1));check(native_attributes[1].budget.pending.empty()&&native_attributes[1].origins.empty());
    // Compare final integer health against one uninterrupted hit, including the
    // fraction where independently truncating the repayment used to lose 1 HP.
    for(const auto amount:{20.3f,20.9f,21.7f,35.2f})for(const auto paid:{0.0f,.06f,.15f,.33f}){
        reset(.33f);float immediate=hit(amount);victim->v.health-=std::trunc(immediate);
        HandleStatusSpendResult(receipt(1,paid));
        check(victim->v.health==100-std::trunc(std::max(0.0f,amount-paid*5)));
    }
    reset();hit(20);player_lives[1]++;minecraft_forms[1]=true;
    HandleStatusSpendResult(receipt(1,0));check(victim->v.health==80); // same spawn form switch
    reset();hit(20);player_births[1]++;HandleStatusSpendResult(receipt(1,0));check(victim->v.health==100);
    HandleStatusAttributes(state(2,1,1,4,336));check(native_attributes[1].budget.sequence==0);
    HandleStatusSpendResult(receipt(1,0));check(victim->v.health==100); // old spawn after new lease
    reset();hit(20);registry.pair.serial++;HandleStatusSpendResult(receipt(1,0));check(victim->v.health==100);
    reset();hit(20);registry.worldValue++;HandleStatusSpendResult(receipt(1,0));check(victim->v.health==100);registry.worldValue--;
    reset();hit(20);source->serialnumber++;HandleStatusSpendResult(receipt(1,0));check(victim->player.attacker==&edicts[0].v);
    reset();hit(20);registry.source.serial++;HandleStatusSpendResult(receipt(1,0));check(victim->player.attacker==&edicts[0].v);
    reset();hit(20);player_births[2]++;HandleStatusSpendResult(receipt(1,0));check(victim->player.attacker==&edicts[0].v);
    reset();hit(20);source->free=true;HandleStatusSpendResult(receipt(1,0));check(victim->player.attacker==&edicts[0].v);
    reset();hit(20);victim->player.alive=false;HandleStatusSpendResult(receipt(1,0));check(victim->player.calls==0);
    reset();hit(20);victim->v.health=10;HandleStatusSpendResult(receipt(1,0));
    check(!victim->player.alive&&victim->v.health==-10&&victim->player.attacker==&source->v);
    reset();link.queue=false;check(hit(20)==20&&native_attributes[1].budget.sequence==0&&native_attributes[1].origins.empty());
    reset();hit(20);alive_hook=[&]{player_births[1]++;};HandleStatusSpendResult(receipt(1,0));
    check(victim->player.calls==0); // IsAlive callback may replace victim
    reset();alive_hook=[&]{player_lives[1]++;};check(hit(20)==20);alive_hook={};
    reset();auto valid=receipt(1,0);for(std::size_t n=0;n<valid.size();++n)rejected(Bytes(valid.begin(),valid.begin()+n));
    valid.push_back(0);rejected(valid);
    valid=receipt(1,0);valid[31]=0x7f;valid[30]=0xc0;rejected(valid); // NaN
    rejected(receipt(1,-1));
    // Exact production hit scope and absorption dispatch, with a health sink.
    auto apply=[&](float amount){
        GoldCraft_NativeHitScope scope(victim,source,source,DMG_BULLET);
        const float immediate=hit(amount);
        victim->player.TakeDamage(&source->v,&source->v,std::trunc(immediate),DMG_BULLET);
    };
    reset();link.hits.clear();apply(30);
    check(link.hits.size()==1&&victim->v.health==90);
    HandleStatusSpendResult(receipt(1,0));check(link.hits.size()==1&&victim->v.health==70);
    Reader notification(link.hits[0]);check(notification.u64()==1&&notification.u64()!=0&&notification.u64()==tick);
    check(notification.u32()==1&&notification.u32()==3&&notification.u32()==5&&notification.u32()==6);
    check(notification.u32()==2&&notification.u32()==registry.source.serial&&notification.u32()==player_births[2]);
    check(notification.u32()==2&&notification.f32()==30);notification.f32();notification.f32();notification.f32();notification.finish();
    for(float paid:{0.0f,1.0f,4.0f}){
        reset();link.hits.clear();apply(20);check(link.hits.empty());
        HandleStatusSpendResult(receipt(1,paid));check(link.hits.size()==1);
        HandleStatusSpendResult(receipt(1,paid));check(link.hits.size()==1);
    }
    reset();link.hits.clear();apply(20);++player_lives[1];HandleStatusSpendResult(receipt(1,4));check(link.hits.size()==1);
    reset();link.hits.clear();apply(20);++player_births[1];HandleStatusSpendResult(receipt(1,4));check(link.hits.empty());
    reset();link.hits.clear();apply(20);++link.gen;HandleStatusSpendResult(receipt(1,4));check(link.hits.empty());
    reset();link.hits.clear();
    {GoldCraft_NativeHitScope parent(victim,source,source,DMG_BULLET);NativeHitPayment(victim,10);
        {GoldCraft_NativeHitScope validating;GoldCraft_NativeDamageCommitted(victim,10);check(link.hits.empty());}
        {GoldCraft_NativeHitScope child(victim,source,source,DMG_BULLET);NativeHitPayment(victim,5);GoldCraft_NativeDamageCommitted(victim,5);}
        GoldCraft_NativeDamageCommitted(victim,10);GoldCraft_NativeDamageCommitted(victim,10);}
    check(link.hits.size()==2);
    for(int reject=0;reject<7;++reject){
        reset();link.hits.clear();
        if(reject==0)minecraft_attack=victim;
        if(reject==1)final_minecraft_damage=victim;
        if(reject==2)victim->v.takedamage=DAMAGE_NO;
        if(reject==3)victim->v.deadflag=1;
        if(reject==4)victim->v.health=0;
        if(reject==5)link.hit_capable=false;
        {GoldCraft_NativeHitScope scope(victim,source,source,DMG_BULLET);NativeHitPayment(victim,10);
            if(reject==6)++victim->serialnumber;
            GoldCraft_NativeDamageCommitted(victim,10);}
        check(link.hits.empty());
        minecraft_attack=final_minecraft_damage=nullptr;link.hit_capable=true;victim->v.takedamage=1;victim->v.deadflag=0;
    }
    std::cout<<count<<" actual status dispatch checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
