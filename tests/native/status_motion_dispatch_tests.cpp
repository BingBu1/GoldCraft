#include "goldcraft/status_motion.hpp"
#include <cstring>
#include "regamedll/regamedll_const.h"
#include <array>
#include <cassert>
#include <cmath>
#include <functional>
#include <iostream>
using namespace goldcraft;
// Observe the production dispatcher through isolated engine data/message sinks.
struct edict_t;
struct entvars_t {int movetype=3,waterlevel=0,watertype=-1,flags=0;float air_finished=0,dmg=2,pain_finished=0,dmgtime=0,health=100;};
bool GoldCraft_StatusImmunity(edict_t*,int);
std::function<void()> alive_hook;
struct CBasePlayer {
    edict_t* entity=nullptr;entvars_t* pev=nullptr;bool alive=true;
    int m_idrowndmg=0,m_idrownrestored=0,m_bitsDamageType=0,m_rgbTimeBasedDamage[8]{},damage_calls=0;
    bool IsAlive()const{if(alive_hook)alive_hook();return alive;}
    edict_t* edict(){return entity;}

};
struct edict_t {bool free=false;int serialnumber=1;entvars_t v;CBasePlayer player;};
std::array<edict_t,65> storage;edict_t* edicts=storage.data();int max_clients=2;
int ENTINDEX(edict_t* e){return static_cast<int>(e-edicts);}
struct Binding{unsigned serial=3;};
struct{Binding pair;unsigned epoch=1;bool present=true;const Binding* get(unsigned slot){return present&&slot<=2?&pair:nullptr;}unsigned world(){return epoch;}}registry;
struct{bool capable=true;bool status_motion(){return capable;}}link;
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

std::array<bool,65> minecraft_forms,client_ready;
struct Controlled {bool active=false;};std::array<Controlled,65> controlled;
constexpr int FL_FAKECLIENT=8192,MSG_ONE=1;
goldcraft::Bytes sent;edict_t* recipient=nullptr;
const char* userCapability="1";
struct Engine {char* pfnGetInfoKeyBuffer(edict_t*){return nullptr;}const char* pfnInfoKeyValue(char*,const char*){return userCapability;}}g_engfuncs;
#define MESSAGE_BEGIN(mode,id,origin,entity) assert(mode==MSG_ONE&&id!=0);sent.clear();recipient=entity
#define WRITE_BYTE(b) sent.push_back(b)
#define MESSAGE_END() ((void)0)
#include "../../native/server/status_motion.inc"
int checks=0;
void check(bool ok){++checks;if(!ok){std::cerr<<"check "<<checks<<" failed\n";std::abort();}}
Bytes packet(std::uint64_t revision){Writer w;w.u64(registry.epoch);w.u64(revision);w.u32(1);w.u32(registry.pair.serial);w.u32(player_births[1]);w.u32(player_lives[1]);w.u32(2);w.f32(.1f);w.f32(.05f);return w.data;}
int main(){
    Key key{};Writer hello;hello.bytes(key);hello.bytes(key);hello.u32(static_cast<unsigned>(Role::fabric_server));hello.u32(capability_status_motion);
    HostHandshake handshake(key,key,Role::host_server,Role::fabric_server,1);
    auto frame=encode_frame(Type::hello,0,1,hello.data);auto header=decode_header(std::span<const std::uint8_t>(frame).first(header_bytes));
    handshake.accept(header,hello.data);check(handshake.ready());const auto welcome=handshake.welcome();Reader r(welcome);r.key();r.u32();check(r.u32()==capability_status_motion);
    auto motionFrame=encode_frame(Type::status_motion,1,2,{});check(decode_header(std::span<const std::uint8_t>(motionFrame).first(header_bytes)).type==Type::status_motion);
    for(unsigned i=0;i<storage.size();++i){storage[i].player.entity=&storage[i];storage[i].player.pev=&storage[i].v;player_births[i]=4;player_lives[i]=5;}
    motion_message=1;client_ready[1]=true;
    check(!CurrentMotion(1));HandleStatusMotion(packet(1));check(CurrentMotion(1)!=nullptr);check(sent==packet(1)&&recipient==edicts+1);
    check(!CurrentMotion(0)&&!CurrentMotion(2)&&!CurrentMotion(65));
    auto rejected=[&](auto mutate,auto restore){mutate();check(!CurrentMotion(1));restore();check(CurrentMotion(1)!=nullptr);};
    rejected([&]{++player_lives[1];},[&]{--player_lives[1];});
    rejected([&]{++player_births[1];},[&]{--player_births[1];});
    rejected([&]{++registry.pair.serial;},[&]{--registry.pair.serial;});
    rejected([&]{++registry.epoch;},[&]{--registry.epoch;});
    rejected([&]{minecraft_forms[1]=true;},[&]{minecraft_forms[1]=false;});
    rejected([&]{controlled[1].active=true;},[&]{controlled[1].active=false;});
    rejected([&]{edicts[1].free=true;},[&]{edicts[1].free=false;});
    rejected([&]{edicts[1].player.alive=false;},[&]{edicts[1].player.alive=true;});
    rejected([&]{link.capable=false;},[&]{link.capable=true;});
    rejected([&]{globals.time-=.1f;},[&]{globals.time+=.1f;});
    alive_hook=[&]{++player_lives[1];};check(!CurrentMotion(1));alive_hook={};--player_lives[1];
    globals.time+=.36f;HandleStatusMotion(packet(1));check(!CurrentMotion(1));HandleStatusMotion(packet(2));check(CurrentMotion(1)!=nullptr);
    sent.clear();userCapability="0";HandleStatusMotion(packet(3));check(sent.empty()&&CurrentMotion(1)!=nullptr);userCapability="1";
    sent.clear();edicts[1].v.flags|=FL_FAKECLIENT;HandleStatusMotion(packet(4));check(sent.empty());edicts[1].v.flags=0;
    auto old=packet(5);++player_births[1];HandleStatusMotion(old);check(!CurrentMotion(1));--player_births[1];
    ClearStatusMotion();check(!CurrentMotion(1));
    std::cout<<checks<<" motion dispatcher checks: actual authority, prediction delivery, identity, lease and reentry\n";
}
