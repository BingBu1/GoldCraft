#include "goldcraft/wire.hpp"
#include "goldcraft/fluid_volumes.hpp"
#include <cmath>
#include <functional>
#include <iostream>
#include <cstdlib>
using namespace goldcraft;
struct Vec {float x=0,y=0,z=0;operator const float*()const{return &x;}Vec operator+(Vec v)const{return {x+v.x,y+v.y,z+v.z};}};
struct Edict {struct {Vec origin,view_ofs;}v;};
std::array<Edict,65> storage;auto* edicts=storage.data();
std::array<unsigned,65> player_births;
struct Player {bool alive=true;bool IsAlive()const{return alive;}}player;
struct Identity {Player* player;unsigned serial,life;};
unsigned serial=3,life=7;bool current=true;
Identity CaptureNativePlayer(unsigned){return {&player,serial,life};}
bool CurrentNativePlayer(const Identity& v){return current&&v.serial==serial&&v.life==life;}
struct {std::uint64_t epoch=41;std::uint64_t world(){return epoch;}}registry;
std::uint64_t tick=10;
struct {bool capable=true;std::uint64_t gen=1;Bytes sent;int calls=0;
 bool native_view(){return capable;}std::uint64_t generation(){return gen;}
 void send(Type t,const Bytes& b){if(t!=Type::native_view)std::abort();sent=b;++calls;}}link;
constexpr int CONTENTS_WATER=-3;
int map_contents=-1;Vec last_eye;std::function<void()> query_hook;
int query(const float* p){last_eye={p[0],p[1],p[2]};if(query_hook)query_hook();return map_contents;}
struct {int(*pfnPointContents)(const float*)=query;}g_engfuncs;
fluids::Field field;bool fluids_active=true;
const fluids::Field* ActiveFluids(){return fluids_active?&field:nullptr;}
#include "../../native/server/native_view.inc"
int checks;
void check(bool value){++checks;if(!value){std::cerr<<"check "<<checks<<" failed\n";std::abort();}}
unsigned send(){link.sent.clear();SendNativeView({1});Reader r(link.sent);check(r.u64()==41&&r.u64()==10);auto count=r.u32();
 if(!count){r.finish();return 2;}
 check(count==1&&r.u32()==1&&r.u32()==serial&&r.u32()==player_births[1]&&r.u32()==life);
 check(r.f32()==last_eye.x&&r.f32()==last_eye.y&&r.f32()==last_eye.z);auto flags=r.u32();r.finish();return flags;}
int main(){
 Key key{};Writer hello;hello.bytes(key);hello.bytes(key);hello.u32(static_cast<unsigned>(Role::fabric_server));hello.u32(capability_native_view);
 HostHandshake handshake(key,key,Role::host_server,Role::fabric_server,1);auto frame=encode_frame(Type::hello,0,1,hello.data);
 auto header=decode_header(std::span<const std::uint8_t>(frame).first(header_bytes));handshake.accept(header,hello.data);
 auto welcome=handshake.welcome();Reader reply(welcome);reply.key();reply.u32();check(reply.u32()==capability_native_view);
 player_births[1]=4;edicts[1].v.view_ofs={0,0,17};field.assign({41,1,{{-3,{-10,-10,0},{10,10,12}}}});
 check(send()==0); // shallow buoyancy must not cover an eye above the surface
 edicts[1].v.view_ofs.z=10;check(send()==1);
 edicts[1].v.view_ofs.z=12;check(send()==0); // exact liquid surface is not underwater
 map_contents=-3;check(send()==1);map_contents=-5;check(send()==0); // native lava wins
 map_contents=-2;edicts[1].v.view_ofs.z=10;check(send()==0); // solid beats legacy embedded MC water
 map_contents=-1;fluids_active=false;check(send()==0);fluids_active=true;
 edicts[1].v.origin={100,200,300};edicts[1].v.view_ofs={1,2,3};check(send()==0);check(last_eye.x==101&&last_eye.y==202&&last_eye.z==303);
 player.alive=false;check(send()==2);player.alive=true;current=false;check(send()==2);current=true;
 query_hook=[](){++life;};check(send()==2);query_hook={};
 query_hook=[](){++player_births[1];};check(send()==2);query_hook={};
 auto calls=link.calls;query_hook=[](){++link.gen;};SendNativeView({1});check(link.calls==calls);query_hook={};
 calls=link.calls;link.capable=false;SendNativeView({1});check(link.calls==calls);
 std::cout<<checks<<" native view checks: production eye/water sender, native/MC priority, authority/lifecycle\n";
}
