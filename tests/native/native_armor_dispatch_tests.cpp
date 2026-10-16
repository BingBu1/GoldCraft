#include "goldcraft/native_armor.hpp"
#include "goldcraft/player_gameplay.hpp"
#include "../../native/server/bridge.h"
#include <array>
#include <functional>
#include <iostream>
#include <map>
#include <string>
using namespace goldcraft;

// Boundary fixture around the actual pending-request implementation. This
// checks dispatch/fencing, not a substitute for real ReGameDLL death or FML.
struct Vector { float x=0,y=0,z=0; };
struct entvars_t { Vector origin;float health=100; };
struct Weapon { int m_iId=0; };
constexpr int WEAPON_KNIFE=29,TRUE=1,FALSE=0;
constexpr int DMG_BULLET=2,DMG_SLASH=4,DMG_BURN=8,DMG_FALL=32,DMG_BLAST=64,DMG_CLUB=128;
constexpr int DMG_DROWN=16384,DMG_NERVEGAS=65536,DMG_POISON=131072,DMG_ACID=1048576,DMG_SLOWBURN=2097152,DMG_EXPLOSION=16777216;
struct Commit { int calls=0,hitgroup=0,armor=0;bool team=false,explosion=false;float amount=0;std::string weapon;entvars_t* attacker=nullptr; } commit;
std::function<void()> commit_callback;
struct CBasePlayer {
    entvars_t* pev=nullptr;bool alive=true;int m_LastHitGroup=0;Weapon item;Weapon* m_pActiveItem=&item;
    std::string weapon="ak47";Vector m_vBlastVector;
    bool IsAlive() const { return alive; }
    const char* GetKillerWeaponName(entvars_t*,entvars_t*) const;
    void GoldCraft_CommitEquipmentDamage(entvars_t*,entvars_t*,float,int,CBasePlayer*,int,int,bool);
};
struct edict_s { bool free=false;int serialnumber=1;entvars_t v;CBasePlayer player; };
using edict_t=edict_s;
std::array<edict_t,3> storage;
edict_t* edicts=storage.data();int max_clients=2;
int ENTINDEX(edict_t* entity){return static_cast<int>(entity-edicts);}
template<class T>T* GET_PRIVATE(edict_t* entity){return &entity->player;}
const char* CBasePlayer::GetKillerWeaponName(entvars_t*,entvars_t* attacker)const{
    for(auto& entity:storage)if(&entity.v==attacker)return entity.player.weapon.c_str();
    return "world";
}
struct Pair {unsigned serial=3;bool paired=true;Key uuid{};};
struct {
    std::array<Pair,3> pairs;std::uint64_t epoch=1;
    Pair* get(unsigned slot){return slot>0&&slot<=2?&pairs[slot]:nullptr;}
    std::uint64_t world()const{return epoch;}
} registry;
std::array<unsigned,3> player_births,player_lives;
std::array<bool,3> minecraft_forms;
struct Control {bool active=false;};std::array<Control,3>controlled;
struct {float time=1;} globals;auto* gpGlobals=&globals;
float next_snapshot=1;
struct {
    bool capable=true,online=true,queue=true;std::uint64_t serial=1;std::vector<Bytes> packets;
    bool native_armor()const{return capable&&online;}
    bool status_attributes()const{return capable&&online;}
    bool connected()const{return online;}
    std::uint64_t generation()const{return serial;}
    bool send(Type type,const Bytes& bytes){if(!queue)return false;if(type!=Type::native_armor_request)throw ProtocolError("Unexpected fixture message");packets.push_back(bytes);return true;}
} link;
struct Identity {edict_t* table;CBasePlayer* player;std::uint64_t epoch;unsigned slot,serial,spawn,life;};
Identity CaptureNativePlayer(unsigned slot){return {edicts,&edicts[slot].player,registry.world(),slot,registry.get(slot)->serial,player_births[slot],player_lives[slot]};}
bool CurrentNativePlayer(const Identity& value){return edicts==value.table&&registry.world()==value.epoch&&!edicts[value.slot].free
    &&registry.get(value.slot)->serial==value.serial&&player_births[value.slot]==value.spawn&&player_lives[value.slot]==value.life;}
struct StatusSource {edict_t* entity=nullptr;int engine_serial=0;unsigned serial=0,spawn=0;};
StatusSource CaptureStatusSource(edict_t* entity){
    if(!entity||entity->free)return {};
    auto* pair=registry.get(ENTINDEX(entity));
    return {entity,entity->serialnumber,pair?pair->serial:0,pair?player_births[ENTINDEX(entity)]:0};
}
edict_t* CurrentStatusSource(const StatusSource& source){
    if(!source.entity||source.entity->free||source.entity->serialnumber!=source.engine_serial)return edicts;
    const auto slot=ENTINDEX(source.entity);
    if(source.serial&&(!registry.get(slot)||source.serial!=registry.get(slot)->serial||source.spawn!=player_births[slot]))return edicts;
    return source.entity;
}
bool attributes_available=true;int attribute_marker=1;
int* CurrentAttributes(edict_t*){return attributes_available?&attribute_marker:nullptr;}
float GoldCraft_StatusDefense(edict_t*,float,edict_t*,edict_t*,int);
#include "../../native/server/armor_defense.inc"
edict_t* final_minecraft_damage=nullptr;edict_t* minecraft_attack=nullptr;
unsigned native_damage_depth=0;
#include "actual_armor_context.inc"
float GoldCraft_StatusDefense(edict_t*,float amount,edict_t*,edict_t*,int){return amount*settling_native_enchantment;}
void CBasePlayer::GoldCraft_CommitEquipmentDamage(entvars_t*,entvars_t* attacker,float amount,int,CBasePlayer*,int team,int armor,bool explosion){
    ++commit.calls;commit.amount=amount;commit.attacker=attacker;commit.hitgroup=m_LastHitGroup;commit.team=team;commit.armor=armor;
    commit.explosion=explosion;commit.weapon=settling_native_weapon?settling_native_weapon:"";pev->health-=std::trunc(amount);
    if(commit_callback)commit_callback();
}
void reset(){
    ClearNativeArmor();registry.epoch=1;link={};globals.time=1;attributes_available=true;
    for(unsigned slot=0;slot<storage.size();++slot){
        storage[slot]={};storage[slot].player.pev=&storage[slot].v;storage[slot].player.m_pActiveItem=&storage[slot].player.item;
        registry.pairs[slot]={};player_births[slot]=5;player_lives[slot]=6;minecraft_forms[slot]=true;controlled[slot].active=true;
    }
    commit={};commit_callback={};
}
bool queue(float damage=40,int bits=DMG_BULLET){return QueueNativeArmor(edicts+1,damage,edicts+2,edicts+2,bits,edicts+2,true,7);}
Bytes reply(float amount=11.2f,float enchantment=1,unsigned status=0){
    auto q=read_armor_request(link.packets.back());Writer w;
    w.u64(q.epoch);w.u64(q.event);w.u32(q.slot);w.u32(q.serial);w.u32(q.spawn);w.u32(q.life);
    w.u32(status);w.f32(amount);w.f32(enchantment);return w.data;
}
int main(){try{
    int checks=0;auto check=[&](bool value){if(!value)throw std::runtime_error("armor dispatch check "+std::to_string(checks+1));++checks;};
    reset();edicts[1].player.m_LastHitGroup=1;check(queue());check(commit.calls==0&&edicts[1].v.health==100);
    edicts[1].player.m_LastHitGroup=3;edicts[2].player.weapon="knife";
    auto accepted=reply(20,.5f);HandleNativeArmorResult(accepted);
    check(commit.calls==1&&edicts[1].v.health==90&&pending_native_armor.empty());
    check(commit.hitgroup==1&&commit.armor==7&&commit.team&&commit.weapon=="ak47"&&commit.attacker==&edicts[2].v);
    check(edicts[1].player.m_LastHitGroup==3&&settling_native_armor==nullptr&&settling_native_weapon==nullptr);
    HandleNativeArmorResult(accepted);check(commit.calls==1);
    reset();edicts[2].player.item.m_iId=WEAPON_KNIFE;check(queue());check(read_armor_request(link.packets.back()).kind==ArmorDamageKind::melee);
    reset();check(queue());check(read_armor_request(link.packets.back()).kind==ArmorDamageKind::projectile);
    auto wrong=reply();wrong[28]=9;HandleNativeArmorResult(wrong);check(commit.calls==0&&pending_native_armor.size()==1);
    bool rejected=false;try{HandleNativeArmorResult(reply(41));}catch(const ProtocolError&){rejected=true;}check(rejected&&commit.calls==0);
    globals.time=1.74f;ExpireNativeArmor();check(commit.calls==0);
    globals.time=1.75f;ExpireNativeArmor();check(commit.calls==1&&edicts[1].v.health==60&&pending_native_armor.empty());
    HandleNativeArmorResult(reply());check(commit.calls==1);
    reset();check(queue());++player_births[1];HandleNativeArmorResult(reply());check(commit.calls==0);
    reset();check(queue());++registry.pairs[1].serial;HandleNativeArmorResult(reply());check(commit.calls==0);
    reset();check(queue());++player_lives[1];minecraft_forms[1]=false;HandleNativeArmorResult(reply());check(commit.calls==1&&commit.amount==40);
    reset();check(queue());++registry.epoch;HandleNativeArmorResult(reply());check(commit.calls==0);
    reset();check(queue());++player_births[2];HandleNativeArmorResult(reply());check(commit.attacker==&edicts[0].v);
    reset();check(queue());HandleNativeArmorResult(reply(0));check(commit.calls==1&&commit.amount==0&&edicts[1].v.health==100);
    reset();check(queue());HandleNativeArmorResult(reply(1,.1f,1));check(commit.calls==1&&commit.amount==40);
    reset();link.queue=false;check(!queue()&&pending_native_armor.empty()&&native_armor_sequence==0);
    reset();attributes_available=false;check(!queue());
    reset();minecraft_forms[1]=false;check(!queue());
    reset();for(int i=0;i<128;++i)if(!queue())throw std::runtime_error("Queue filled too soon");check(!queue());
    ClearNativeArmor();check(pending_native_armor.empty()&&native_armor_sequence==0&&commit.calls==0);
    reset();check(queue());check(queue());globals.time=2;
    commit_callback=[](){++registry.epoch;ClearNativeArmor();};ExpireNativeArmor();check(commit.calls==1&&pending_native_armor.empty());
    reset();check(queue());check(queue());commit_callback=[](){++registry.epoch;ClearNativeArmor();};RetireNativeArmor();check(commit.calls==1);
    reset();final_minecraft_damage=edicts+1;
    check(!GoldCraft_QueueNativeArmor(edicts+1,40,edicts+2,edicts+2,DMG_BULLET,edicts+2,false,0));final_minecraft_damage=nullptr;
    minecraft_attack=edicts+1;
    check(!GoldCraft_QueueNativeArmor(edicts+1,40,edicts+2,edicts+2,DMG_BULLET,edicts+2,false,0));minecraft_attack=nullptr;
    check(pending_native_armor.empty());
    settling_native_armor=edicts+1;settling_native_enchantment=.5f;settling_native_weapon="ak47";
    const goldcraft::ActivePlayerAttack active{1,2,1,1,10,20};
    {
        GoldCraft_EquipmentDamageScope nested;
        check(active.match(1,2,native_damage_depth,10,20)==1);
        {GoldCraft_EquipmentDamageScope callback;check(active.match(1,2,native_damage_depth,10,20)==0);}
        check(active.match(1,2,native_damage_depth,10,20)==1);
        check(settling_native_armor==nullptr&&settling_native_enchantment==1&&GoldCraft_EquipmentWeapon(edicts+1)==nullptr);
        check(GoldCraft_QueueNativeArmor(edicts+1,40,edicts+2,edicts+2,DMG_BULLET,edicts+2,false,0));
    }
    check(native_damage_depth==0&&active.match(1,2,native_damage_depth,10,20)==0);
    check(settling_native_armor==edicts+1&&settling_native_enchantment==.5f&&std::string(GoldCraft_EquipmentWeapon(edicts+1))=="ak47");
    settling_native_armor=nullptr;settling_native_enchantment=1;settling_native_weapon=nullptr;
    std::cout<<checks<<" actual native armor dispatch checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
