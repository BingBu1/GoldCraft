#include "goldcraft/native_impulse.hpp"
#include "goldcraft/native_knockback.hpp"
#include "goldcraft/combat.hpp"
#include <cassert>
#include <functional>
#include <iostream>
#include <limits>
using namespace goldcraft;
struct Vector {float x=0,y=0,z=0;Vector()=default;Vector(float a,float b,float c):x(a),y(b),z(c){}
    Vector operator+(Vector b)const{return {x+b.x,y+b.y,z+b.z};}
    Vector operator*(float s)const{return {x*s,y*s,z*s};}};
struct Vars{Vector origin{},mins{},maxs{},velocity{10,20,30};int iuser1=0,movetype=3,flags=512;};
struct edict_t{bool free=false;Vars v;};edict_t table[65],*edicts=table;
struct Player{bool alive=true;std::function<void()> hook;bool IsAlive(){if(hook)hook();return alive;}}player;
struct Pair{unsigned serial=7;}pair;
struct Registry{std::uint64_t epoch=1;std::uint64_t world()const{return epoch;}const Pair* get(unsigned s)const{return s==1?&pair:nullptr;}}registry;
struct Link{bool capable=true;std::uint64_t gen=2;bool native_impulse()const{return capable;}bool native_knockback()const{return capable;}std::uint64_t generation()const{return gen;}}link;
std::array<unsigned,65> player_births,player_lives;std::array<bool,65> minecraft_forms;
struct Globals{float time=10;}globals,*gpGlobals=&globals;int max_clients=32;
constexpr int MOVETYPE_WALK=3,FL_ONGROUND=512;
struct Identity{Player* player=nullptr;int team=2;unsigned life=0;};
Identity CaptureNativePlayer(unsigned s){return {&player,2,player_lives[s]};}
bool CurrentNativePlayer(const Identity& i){return edicts&&!edicts[1].free&&i.life==player_lives[1]&&!minecraft_forms[1];}
SkillDamageSnapshots skill_damage_snapshots;
#include "../../native/server/native_impulse.inc"
int checks=0;
void check(bool b){++checks;if(!b){std::cerr<<"failed "<<checks<<'\n';std::abort();}}
Bytes packet(std::uint64_t event=1,std::uint64_t epoch=1,std::uint64_t tick=5,unsigned serial=7,unsigned spawn=9,unsigned life=11,Vec3 center={},Vec3 delta={100,-200,300}){
    Writer w;w.u64(epoch);w.u64(event);w.u64(tick);w.u32(1);w.u32(serial);w.u32(spawn);w.u32(life);
    for(float v:{center.x,center.y,center.z,delta.x,delta.y,delta.z})w.f32(v);return w.data;
}
void fresh(){
    table[1]={};edicts=table;player={};registry.epoch=1;link={};gpGlobals->time=10;
    player_births[1]=9;player_lives[1]=11;minecraft_forms[1]=false;
    native_impulse_event=native_impulse_generation=native_impulse_epoch=0;
    native_knockback_grants={};
    skill_damage_snapshots.clear();skill_damage_snapshots.begin(1,5,10);skill_damage_snapshots.actor(5,1,7,9,{});
}
void unchanged(){check(table[1].v.velocity.x==10&&table[1].v.velocity.y==20&&table[1].v.velocity.z==30);}
template<class F>void rejected(F f){fresh();f();HandleNativeImpulse(packet());unchanged();}
int main(){
    fresh();HandleNativeImpulse(packet());check(table[1].v.velocity.x==110&&table[1].v.velocity.y==-180&&table[1].v.velocity.z==330);check(!(table[1].v.flags&FL_ONGROUND));
    HandleNativeImpulse(packet());check(table[1].v.velocity.x==110);HandleNativeImpulse(packet(2));check(table[1].v.velocity.x==210);
    rejected([]{table[1].free=true;});rejected([]{player.alive=false;});rejected([]{minecraft_forms[1]=true;});
    rejected([]{player_births[1]++;});rejected([]{player_lives[1]++;});rejected([]{table[1].v.iuser1=1;});
    rejected([]{table[1].v.movetype=8;});rejected([]{gpGlobals->time=11.01f;});rejected([]{gpGlobals->time=9.9f;});
    rejected([]{table[1].v.origin.x=65;});rejected([]{skill_damage_snapshots.clear();});
    rejected([]{player.hook=[](){player_lives[1]++;};});rejected([]{player.hook=[](){link.gen++;};});
    for(auto b:{packet(1,2),packet(1,1,6),packet(1,1,5,8),packet(1,1,5,7,10),packet(1,1,5,7,9,12),packet(1,1,5,7,9,11,{2,0,0})}){fresh();HandleNativeImpulse(b);unchanged();}
    fresh();table[1].v.iuser1=1;HandleNativeImpulse(packet());table[1].v.iuser1=0;HandleNativeImpulse(packet());unchanged();
    link.gen++;HandleNativeImpulse(packet());check(table[1].v.velocity.x==110);
    registry.epoch=2;skill_damage_snapshots.begin(2,1,10);skill_damage_snapshots.actor(1,1,7,9,{});HandleNativeImpulse(packet(1,2,1));check(table[1].v.velocity.x==210);
    fresh();link.capable=false;bool caught=false;try{HandleNativeImpulse(packet());}catch(const ProtocolError&){caught=true;}check(caught);unchanged();
    fresh();auto b=packet();for(std::size_t n=0;n<b.size();n++){caught=false;try{native_impulse::decode(std::span(b).first(n));}catch(const ProtocolError&){caught=true;}check(caught);}
    b.push_back(0);caught=false;try{native_impulse::decode(b);}catch(const ProtocolError&){caught=true;}check(caught);
    for(float v:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),65537.f,-65537.f}){
        caught=false;try{native_impulse::decode(packet(1,1,5,7,9,11,{},{v,0,0}));}catch(const ProtocolError&){caught=true;}check(caught);
    }
    fresh();HandleNativeImpulse(packet(1,1,5,7,9,11,{},{0,0,-10}));check(table[1].v.velocity.z==20&&(table[1].v.flags&FL_ONGROUND));
    auto hurt=[](std::uint64_t event=1,unsigned life=11){Writer w;w.bytes(packet(event,1,5,7,9,life));w.u64(42);return w.data;};
    auto grant=[](){native_knockback_grants.grant(1,2,42,1,7,9,11,2,10);};
    fresh();HandleNativeKnockback(hurt());unchanged();grant();HandleNativeKnockback(hurt());check(table[1].v.velocity.x==110);
    HandleNativeKnockback(hurt(2));check(table[1].v.velocity.x==110); // grant is single-use even with another impulse event
    fresh();grant();gpGlobals->time=11.01f;HandleNativeKnockback(hurt());unchanged();
    fresh();grant();link.gen=3;HandleNativeKnockback(hurt());unchanged();
    fresh();grant();HandleNativeKnockback(hurt(1,12));HandleNativeKnockback(hurt());unchanged();
    fresh();for(unsigned i=1;i<=257;i++)native_knockback_grants.grant(1,2,i,1,7,9,11,2,10);
    auto q=native_impulse::decode(packet());check(!native_knockback_grants.consume(q,1,2,2,10));check(native_knockback_grants.consume(q,257,2,2,10));
    std::cout<<checks<<" native impulse checks: exact consumer, identity, snapshots, replay and authority\n";
}
