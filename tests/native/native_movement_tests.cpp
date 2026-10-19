#include "goldcraft/wire.hpp"
#include <cmath>
#include <functional>
#include <iostream>
#include <cstdlib>
using namespace goldcraft;
struct Edict {struct {Vec3 velocity;int flags=0,movetype=3;}v;};
std::array<Edict,65> storage;auto* edicts=storage.data();
std::array<unsigned,65> player_births;
std::function<void()> alive_hook;
struct Player {bool alive=true;bool IsAlive()const{if(alive_hook)alive_hook();return alive;}}player;
struct Identity {Player* player;unsigned serial,life;};
unsigned serial=3,life=7;bool current=true;
Identity CaptureNativePlayer(unsigned){return {&player,serial,life};}
bool CurrentNativePlayer(const Identity& v){return current&&v.serial==serial&&v.life==life;}
struct {std::uint64_t epoch=41;std::uint64_t world(){return epoch;}}registry;
std::uint64_t tick=10;
struct {bool capable=true;std::uint64_t gen=1;Bytes sent;int calls=0;
 bool native_movement(){return capable;}std::uint64_t generation(){return gen;}
 void send(Type t,const Bytes& b){if(t!=Type::native_movement)std::abort();sent=b;++calls;}}link;
constexpr int FL_ONGROUND=1<<9,MOVETYPE_WALK=3;
#include "../../native/server/native_movement.inc"
int checks;
void check(bool value){++checks;if(!value){std::cerr<<"check "<<checks<<" failed\n";std::abort();}}
unsigned send(){link.sent.clear();SendNativeMovement({1});Reader r(link.sent);check(r.u64()==41&&r.u64()==10);auto count=r.u32();
 if(!count){r.finish();return 4;}
 check(count==1&&r.u32()==1&&r.u32()==serial&&r.u32()==player_births[1]&&r.u32()==life);
 const auto& v=edicts[1].v.velocity;check(r.f32()==v.x&&r.f32()==v.y&&r.f32()==v.z);auto flags=r.u32();r.finish();return flags;}
int main(){
 Key key{};Writer hello;hello.bytes(key);hello.bytes(key);hello.u32(static_cast<unsigned>(Role::fabric_server));hello.u32(capability_native_movement);
 HostHandshake handshake(key,key,Role::host_server,Role::fabric_server,1);auto frame=encode_frame(Type::hello,0,1,hello.data);
 auto header=decode_header(std::span<const std::uint8_t>(frame).first(header_bytes));handshake.accept(header,hello.data);
 auto welcome=handshake.welcome();Reader reply(welcome);reply.key();reply.u32();check(reply.u32()==capability_native_movement);
 auto movement=encode_frame(Type::native_movement,1,2,{});check(decode_header(std::span<const std::uint8_t>(movement).first(header_bytes)).type==Type::native_movement);
 player_births[1]=4;edicts[1].v.velocity={320,-640,128};check(send()==2);
 edicts[1].v.flags=FL_ONGROUND;check(send()==3);edicts[1].v.movetype=0;check(send()==1);
 edicts[1].v.flags=0;check(send()==0);edicts[1].v.velocity.x=65537;check(send()==4);
 edicts[1].v.velocity.x=INFINITY;check(send()==4);edicts[1].v.velocity.x=NAN;check(send()==4);
 edicts[1].v.velocity.x=65536;check(send()==0);
 player.alive=false;check(send()==4);player.alive=true;current=false;check(send()==4);current=true;
 alive_hook=[](){++life;};check(send()==4);alive_hook={};
 alive_hook=[](){++player_births[1];};check(send()==4);alive_hook={};
 auto calls=link.calls;alive_hook=[](){++link.gen;};SendNativeMovement({1});check(link.calls==calls);alive_hook={};
 calls=link.calls;link.capable=false;SendNativeMovement({1});check(link.calls==calls);
 std::cout<<checks<<" native movement checks: production sender, ground/air, velocity, bounds and lifecycle\n";
}
