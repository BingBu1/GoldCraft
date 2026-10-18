#include "goldcraft/status_melee.hpp"
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
using namespace goldcraft;
struct edict_t {bool free=false;int serialnumber=1;struct {int iuser1=0;}v;};
std::array<edict_t,65> storage;edict_t* edicts=storage.data();int max_clients=2;
int ENTINDEX(edict_t* e){return static_cast<int>(e-edicts);}
edict_t *final_minecraft_damage=nullptr,*minecraft_attack=nullptr;
std::array<bool,65> minecraft_forms;
struct {float time=1;}globals;auto* gpGlobals=&globals;
std::function<void()> alive_hook;
struct Player {bool alive=true;bool IsAlive()const{if(alive_hook)alive_hook();return alive;}}player;
struct Binding {unsigned serial=3;};
struct {Binding pair;unsigned epoch=1;bool present=true;
 const Binding* get(unsigned slot){return present&&slot>=1&&slot<=2?&pair:nullptr;}unsigned world(){return epoch;}}registry;
struct {bool capable=true;unsigned gen=1;unsigned generation(){return gen;}bool status_melee(){return capable;}}link;
std::array<unsigned,65> player_births,player_lives;
struct NativePlayerIdentity {Player* player;unsigned slot,serial,spawn,life,world;int entity_serial;edict_t* table;bool form;};
NativePlayerIdentity CaptureNativePlayer(unsigned slot){const auto* pair=registry.get(slot);return {pair?&player:nullptr,slot,pair?pair->serial:0,
 player_births[slot],player_lives[slot],registry.world(),edicts[slot].serialnumber,edicts,minecraft_forms[slot]};}
bool CurrentNativePlayer(const NativePlayerIdentity& i){const auto* pair=registry.get(i.slot);return pair&&i.table==edicts&&!edicts[i.slot].free&&i.world==registry.world()
 &&i.serial==pair->serial&&i.spawn==player_births[i.slot]&&i.life==player_lives[i.slot]&&i.entity_serial==edicts[i.slot].serialnumber&&i.form==minecraft_forms[i.slot];}
#include "../../native/server/status_melee.inc"
#include "status_melee_dispatch.inc"
int checks;
void check(bool ok){++checks;if(!ok){std::cerr<<"failed check "<<checks<<'\n';std::abort();}}
Bytes packet(std::uint64_t revision,float add=3,float base=0,float total=1){Writer w;w.u64(registry.epoch);w.u64(revision);w.u32(1);
 w.u32(registry.pair.serial);w.u32(player_births[1]);w.u32(player_lives[1]);w.f32(add);w.f32(base);w.f32(total);return w.data;}
template<class F>void rejects(F action){try{action();check(false);}catch(const ProtocolError&){check(true);}}
int main(){
 Key key{};Writer hello;hello.bytes(key);hello.bytes(key);hello.u32(static_cast<unsigned>(Role::fabric_server));hello.u32(capability_status_melee);
 HostHandshake handshake(key,key,Role::host_server,Role::fabric_server,1);auto frame=encode_frame(Type::hello,0,1,hello.data);
 auto header=decode_header(std::span<const std::uint8_t>(frame).first(header_bytes));handshake.accept(header,hello.data);check(handshake.ready());
 auto welcome=handshake.welcome();Reader reply(welcome);reply.key();reply.u32();check(reply.u32()==capability_status_melee);
 HostHandshake client(key,key,Role::host_client,Role::fabric_client,2);hello.data[32]=static_cast<std::uint8_t>(Role::fabric_client);
 rejects([&](){client.accept(header,hello.data);});
 auto typeFrame=encode_frame(Type::status_melee,1,2,{});check(decode_header(std::span<const std::uint8_t>(typeFrame).first(header_bytes)).type==Type::status_melee);
 player_births.fill(4);player_lives.fill(5);auto* e=edicts+1;
 check(GoldCraft_StatusMeleeDamage(e,35)==35);HandleStatusMelee(packet(1));check(GoldCraft_StatusMeleeDamage(e,35)==50);
 check(knife_swing(e,15)==30);check(knife_stab(e,65,3)==240); // real call expressions, before backstab
 HandleStatusMelee(packet(1,-4));check(GoldCraft_StatusMeleeDamage(e,35)==50); // replay
 HandleStatusMelee(packet(2,-4));check(knife_swing(e,35)==15);check(knife_swing(e,15)==0);
 HandleStatusMelee(packet(3,3,.5f,1.2f));check(std::abs(knife_swing(e,35)-90)<.0001f);
 check(knife_swing(e,0)==0);check(knife_swing(e,-2)==-2);
 HandleStatusMelee(packet(4,0,0,1));check(knife_swing(e,100000)==100000);
 HandleStatusMelee(packet(5,1000000));check(knife_swing(e,35)==10240);
 HandleStatusMelee(packet(6,-1000000));check(knife_swing(e,35)==0);
 HandleStatusMelee(packet(7,3));check(!CurrentMelee(edicts+2));
 globals.time=1.349f;check(CurrentMelee(e));globals.time=1.351f;check(!CurrentMelee(e));globals.time=.9f;check(!CurrentMelee(e));globals.time=1;
 auto b=packet(8);for(unsigned n=0;n<44;++n)rejects([&](){HandleStatusMelee(std::span<const std::uint8_t>(b).first(n));});
 rejects([&](){auto extra=b;extra.push_back(0);HandleStatusMelee(extra);});rejects([&](){HandleStatusMelee(packet(0));});
 for(float bad:{1000001.f,-1000001.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
  rejects([&](){HandleStatusMelee(packet(8,bad));});rejects([&](){HandleStatusMelee(packet(8,0,bad));});rejects([&](){HandleStatusMelee(packet(8,0,0,bad));});}
 ++player_lives[1];check(!CurrentMelee(e));HandleStatusMelee(b);--player_lives[1];
 ++player_births[1];check(!CurrentMelee(e));HandleStatusMelee(b);--player_births[1];
 ++registry.epoch;check(!CurrentMelee(e));HandleStatusMelee(b);--registry.epoch;
 ++registry.pair.serial;check(!CurrentMelee(e));HandleStatusMelee(b);--registry.pair.serial;
 player.alive=false;check(!CurrentMelee(e));HandleStatusMelee(b);player.alive=true;
 minecraft_forms[1]=true;check(knife_swing(e,35)==35);minecraft_forms[1]=false;
 e->v.iuser1=1;check(!CurrentMelee(e));e->v.iuser1=0;
 final_minecraft_damage=e;check(!CurrentMelee(e));final_minecraft_damage=nullptr;minecraft_attack=e;check(!CurrentMelee(e));minecraft_attack=nullptr;
 alive_hook=[&](){++link.gen;};HandleStatusMelee(b);alive_hook={};check(!CurrentMelee(e));
 HandleStatusMelee(packet(1));check(knife_swing(e,35)==50);
 alive_hook=[&](){++e->serialnumber;};check(!CurrentMelee(e));alive_hook={};
 HandleStatusMelee(packet(1));check(CurrentMelee(e));ClearStatusMelee();check(!CurrentMelee(e));
 link.capable=false;rejects([&](){HandleStatusMelee(b);});check(knife_swing(e,35)==35);
 std::cout<<checks<<" native melee checks: actual consumer/knife call expressions, attribute algebra, authority and lifecycle\n";
}
