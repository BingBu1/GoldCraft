#include "goldcraft/status_environment.hpp"
#include "regamedll/regamedll_const.h"
#include <array>
#include <cassert>
#include <cmath>
#include <functional>
#include <iostream>
using namespace goldcraft;
// The fixture replaces only engine data/sinks around the actual WaterMove body.
constexpr int MOVETYPE_NONE=0,MOVETYPE_NOCLIP=8,FL_INWATER=512,CONTENT_LAVA=-5,CONTENT_SLIME=-4;
constexpr int CHAN_VOICE=2,CHAN_BODY=4,ITBD_DROWN_RECOVER=4,AIRTIME=12;
constexpr float VOL_NORM=1,ATTN_NORM=.8f;
struct edict_t;
struct entvars_t {int movetype=3,waterlevel=0,watertype=-1,flags=0;float air_finished=0,dmg=2,pain_finished=0,dmgtime=0,health=100;};
bool GoldCraft_StatusImmunity(edict_t*,int);
std::function<void()> alive_hook;
struct CBasePlayer {
    edict_t* entity=nullptr;entvars_t* pev=nullptr;bool alive=true;
    int m_idrowndmg=0,m_idrownrestored=0,m_bitsDamageType=0,m_rgbTimeBasedDamage[8]{},damage_calls=0;
    bool IsAlive()const{if(alive_hook)alive_hook();return alive;}
    edict_t* edict(){return entity;}
    void WaterMove();
    bool TakeDamage(entvars_t*,entvars_t*,float amount,int bits){
        if(GoldCraft_StatusImmunity(entity,bits))return false;
        ++damage_calls;pev->health-=amount;return true;
    }
};
struct edict_t {bool free=false;int serialnumber=1;entvars_t v;CBasePlayer player;};
std::array<edict_t,65> storage;edict_t* edicts=storage.data();int max_clients=2;
int ENTINDEX(edict_t* e){return static_cast<int>(e-edicts);}
struct Binding{unsigned serial=3;};
struct{Binding pair;unsigned epoch=1;bool present=true;const Binding* get(unsigned slot){return present&&slot<=2?&pair:nullptr;}unsigned world(){return epoch;}}registry;
struct{bool capable=true;bool status_environment(){return capable;}}link;
struct{float time=1;}globals;auto* gpGlobals=&globals;
std::array<unsigned,65> player_births,player_lives;
struct Identity{CBasePlayer* player;unsigned slot,serial,spawn,life,world;int engine_serial;};
Identity CaptureNativePlayer(unsigned slot){
    const auto* pair=registry.get(slot);
    return {pair?&edicts[slot].player:nullptr,slot,pair?pair->serial:0,player_births[slot],player_lives[slot],registry.world(),edicts[slot].serialnumber};
}
bool CurrentNativePlayer(const Identity& i){const auto* pair=registry.get(i.slot);return pair&&!edicts[i.slot].free&&i.world==registry.world()
    &&i.serial==pair->serial&&i.spawn==player_births[i.slot]&&i.life==player_lives[i.slot]&&i.engine_serial==edicts[i.slot].serialnumber;}
edict_t *final_minecraft_damage=nullptr,*minecraft_attack=nullptr,*eoNullEntity=edicts;
entvars_t* VARS(edict_t* e){return &e->v;}
edict_t* ENT(entvars_t*){return edicts;}
int RANDOM_LONG(int,int maximum){return maximum;}
void EMIT_SOUND(edict_t*,int,const char*,float,float){}
#include "../../native/server/status_environment.inc"
#include "actual_status_environment.inc"
int checks=0;
void check(bool value){++checks;if(!value){std::cerr<<"failed check "<<checks<<'\n';std::abort();}}
Bytes packet(std::uint64_t revision,unsigned flags=7,unsigned slot=1){Writer w;w.u64(registry.epoch);w.u64(revision);w.u32(slot);w.u32(registry.pair.serial);w.u32(player_births[slot]);w.u32(player_lives[slot]);w.u32(flags);return w.data;}
template<class F>void rejects(F action){try{action();check(false);}catch(const ProtocolError&){check(true);}}
int main(){
    Key key{};Writer hello;hello.bytes(key);hello.bytes(key);hello.u32(static_cast<unsigned>(Role::fabric_server));hello.u32(capability_status_environment);
    HostHandshake handshake(key,key,Role::host_server,Role::fabric_server,1);
    auto frame=encode_frame(Type::hello,0,1,hello.data);
    auto header=decode_header(std::span<const std::uint8_t>(frame).first(header_bytes));
    handshake.accept(header,hello.data);check(handshake.ready());
    const auto welcome=handshake.welcome();Reader reply(welcome);reply.key();reply.u32();check(reply.u32()==capability_status_environment);
    HostHandshake client(key,key,Role::host_client,Role::fabric_client,2);
    hello.data[32]=static_cast<std::uint8_t>(Role::fabric_client);
    rejects([&](){client.accept(header,hello.data);});
    auto environmentFrame=encode_frame(Type::status_environment,1,2,{});
    check(decode_header(std::span<const std::uint8_t>(environmentFrame).first(header_bytes)).type==Type::status_environment);
    for(unsigned n=0;n<storage.size();++n){storage[n].player.entity=&storage[n];storage[n].player.pev=&storage[n].v;player_births[n]=4;player_lives[n]=5;}
    auto* e=edicts+1;auto& p=e->player;auto& v=e->v;
    check(!GoldCraft_StatusImmunity(e,DMG_BURN));HandleStatusEnvironment(packet(1));
    for(int bits:{DMG_BURN,DMG_SLOWBURN,DMG_DROWN,DMG_FALL,DMG_BURN|DMG_FALL,DMG_BURN|DMG_ALWAYSGIB})check(GoldCraft_StatusImmunity(e,bits));
    for(int bits:{0,DMG_BULLET,DMG_ACID,DMG_BLAST,DMG_BURN|DMG_BLAST,DMG_DROWN|DMG_BULLET,DMG_ALWAYSGIB})check(!GoldCraft_StatusImmunity(e,bits));
    check(!GoldCraft_StatusImmunity(edicts+2,DMG_BURN));
    minecraft_attack=e;check(!GoldCraft_StatusImmunity(e,DMG_BURN));minecraft_attack=nullptr;
    final_minecraft_damage=e;check(!GoldCraft_StatusImmunity(e,DMG_FALL));final_minecraft_damage=nullptr;
    // Actual WaterMove: keep head submerged, refresh air, no fake recovery credit.
    v.waterlevel=3;v.air_finished=0;p.WaterMove();check(v.waterlevel==3&&v.air_finished==globals.time+AIRTIME&&p.damage_calls==0&&p.m_idrowndmg==0);
    v.watertype=CONTENT_LAVA;p.WaterMove();check(p.damage_calls==0&&v.health==100);
    v.watertype=CONTENT_SLIME;p.WaterMove();check(p.damage_calls==1&&v.health==88); // acid is not fire
    HandleStatusEnvironment(packet(2,0));check(!GoldCraft_StatusBreathing(e));
    v.watertype=-3;v.air_finished=0;v.pain_finished=0;p.WaterMove();check(p.damage_calls==2&&p.m_idrowndmg==3&&v.health==85);
    HandleStatusEnvironment(packet(3,2));p.WaterMove();check(p.m_idrowndmg==3&&p.damage_calls==2&&v.waterlevel==3);
    v.waterlevel=0;p.WaterMove();check((p.m_bitsDamageType&DMG_DROWNRECOVER)!=0); // prior real debt remains
    HandleStatusEnvironment(packet(3,7));check(!GoldCraft_StatusImmunity(e,DMG_BURN)); // duplicate cannot overwrite
    globals.time+=.36f;check(!GoldCraft_StatusBreathing(e));HandleStatusEnvironment(packet(3,2));check(!GoldCraft_StatusBreathing(e)); // duplicate cannot renew
    HandleStatusEnvironment(packet(4));check(GoldCraft_StatusBreathing(e));globals.time-=.1f;check(!GoldCraft_StatusBreathing(e));globals.time+=.1f;
    ++player_lives[1];check(!GoldCraft_StatusBreathing(e));--player_lives[1];
    ++player_births[1];check(!GoldCraft_StatusBreathing(e));--player_births[1];
    ++registry.pair.serial;check(!GoldCraft_StatusBreathing(e));--registry.pair.serial;
    ++registry.epoch;check(!GoldCraft_StatusBreathing(e));--registry.epoch;
    p.alive=false;check(!GoldCraft_StatusBreathing(e));p.alive=true;
    e->free=true;check(!GoldCraft_StatusBreathing(e));e->free=false;
    alive_hook=[&](){++player_lives[1];};check(!GoldCraft_StatusBreathing(e));alive_hook={};--player_lives[1];
    link.capable=false;check(!GoldCraft_StatusBreathing(e));rejects([&](){HandleStatusEnvironment(packet(5));});link.capable=true;
    rejects([&](){auto bytes=packet(5);bytes.push_back(0);HandleStatusEnvironment(bytes);});
    rejects([&](){auto bytes=packet(5);bytes.pop_back();HandleStatusEnvironment(bytes);});
    rejects([&](){HandleStatusEnvironment(packet(5,8));});
    rejects([&](){HandleStatusEnvironment(packet(0));});
    ClearStatusEnvironment();check(!GoldCraft_StatusBreathing(e));
    std::cout<<checks<<" native environment checks: actual consumers/WaterMove, mixed damage, air/debt, lease/identity/reentry, protocol rejection\n";
}
