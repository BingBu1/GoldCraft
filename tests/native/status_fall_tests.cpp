#include "goldcraft/status_fall.hpp"
#include "../../native/server/bridge.h"
#include <array>
#include <cassert>
#include <functional>
#include <iostream>
using namespace goldcraft;
using edict_t=edict_s;
constexpr int FL_ONGROUND=512,CONTENT_WATER=-3,DMG_FALL=32,CHAN_ITEM=3,HITGROUP_GENERIC=0,EVENT_PLAYER_LANDED_FROM_HEIGHT=1,PLAYER_WALK=1;
constexpr float VOL_NORM=1,ATTN_NORM=.8f;
struct Vec{float x=0;};
struct entvars_t{int flags=FL_ONGROUND,watertype=-1;float health=100,gravity=1;Vec punchangle;};
std::function<void()> alive_hook;
struct CBasePlayer {
    edict_t* entity=nullptr;entvars_t* pev=nullptr;bool alive=true;
    float m_flFallVelocity=0,damage=0;int damage_calls=0,m_LastHitGroup=0;
    bool IsAlive()const{if(alive_hook)alive_hook();return alive;}
    edict_t* edict(){return entity;}
    void SetAnimation(int){}
    void Landing();
    void TakeDamage(entvars_t* inflictor,entvars_t* attacker,float amount,int bits){
        assert(inflictor==pev&&attacker==pev&&bits==DMG_FALL);
        damage=amount;++damage_calls;pev->health-=amount;
    }
};
struct edict_s{bool free=false;int serialnumber=1;entvars_t v;CBasePlayer player;};
std::array<edict_t,65> storage;edict_t* edicts=storage.data();int max_clients=2;
int ENTINDEX(edict_t* e){return static_cast<int>(e-edicts);}
struct Binding{unsigned serial=3;};
struct{Binding pair;unsigned epoch=1;bool present=true;const Binding* get(unsigned slot){return present&&slot<=2?&pair:nullptr;}unsigned world(){return epoch;}}registry;
struct{bool capable=true;bool status_fall(){return capable;}}link;
struct{float time=1;}globals;auto* gpGlobals=&globals;
std::array<unsigned,65> player_births,player_lives;
std::array<bool,65> minecraft_forms;
struct Controlled{bool active=false;};std::array<Controlled,65> controlled;
struct Identity{CBasePlayer* player;unsigned slot,serial,spawn,life,world;int engine_serial;};
Identity CaptureNativePlayer(unsigned slot){const auto* p=registry.get(slot);return {p?&edicts[slot].player:nullptr,slot,p?p->serial:0,player_births[slot],player_lives[slot],registry.world(),edicts[slot].serialnumber};}
bool CurrentNativePlayer(const Identity& i){const auto* p=registry.get(i.slot);return p&&!edicts[i.slot].free&&i.world==registry.world()
    &&i.serial==p->serial&&i.spawn==player_births[i.slot]&&i.life==player_lives[i.slot]&&i.engine_serial==edicts[i.slot].serialnumber;}
bool mcFall=false,immune=false;
bool GoldCraft_MinecraftFallAuthority(edict_t*){return mcFall;}
bool GoldCraft_StatusImmunity(edict_t*,int){return immune;}
edict_t* ENT(entvars_t*){return edicts;}
void EMIT_SOUND(edict_t*,int,const char*,float,float){}
struct Bots{void OnEvent(int,CBasePlayer*){}};Bots* TheBots=nullptr;
struct{float value=800;}gravity;auto* g_psv_gravity=&gravity;
struct{float value=1;}falldamage;
struct CHalfLifeMultiplay {
    bool cancel=false;float observed=0;
    float NativeFallDamage(CBasePlayer*);
    float FlPlayerFallDamage(CBasePlayer* p){observed=p->m_flFallVelocity;return cancel?0:NativeFallDamage(p);}
}rules;auto* g_pGameRules=&rules;
#define REGAMEDLL_ADD
#define REGAMEDLL_FIXES
#include "../../native/server/status_fall.inc"
#include "actual_status_fall.inc"
int checks=0;
void check(bool value){++checks;if(!value){std::cerr<<"fall check "<<checks<<" failed\n";std::abort();}}
void near(float a,float b){check(std::abs(a-b)<.0002f+std::abs(b)*.000001f);}
Bytes packet(std::uint64_t revision,float distance=4,float multiplier=1){Writer w;w.u64(registry.epoch);w.u64(revision);w.u32(1);w.u32(registry.pair.serial);w.u32(player_births[1]);w.u32(player_lives[1]);w.f32(distance);w.f32(multiplier);return w.data;}
template<class F>void rejects(F f){try{f();check(false);}catch(const ProtocolError&){check(true);}}
int main(){
    for(unsigned n=0;n<storage.size();++n){storage[n].player.entity=&storage[n];storage[n].player.pev=&storage[n].v;player_births[n]=4;player_lives[n]=5;}
    Key key{};Writer hello;hello.bytes(key);hello.bytes(key);hello.u32(static_cast<unsigned>(Role::fabric_server));hello.u32(capability_status_fall);
    HostHandshake handshake(key,key,Role::host_server,Role::fabric_server,1);
    auto frame=encode_frame(Type::hello,0,1,hello.data);auto header=decode_header(std::span<const std::uint8_t>(frame).first(header_bytes));
    handshake.accept(header,hello.data);check(handshake.ready());auto welcome=handshake.welcome();Reader r(welcome);r.key();r.u32();check(r.u32()==capability_status_fall);
    HostHandshake client(key,key,Role::host_client,Role::fabric_client,2);hello.data[32]=static_cast<std::uint8_t>(Role::fabric_client);rejects([&]{client.accept(header,hello.data);});
    frame=encode_frame(Type::status_fall,1,2,{});check(decode_header(std::span<const std::uint8_t>(frame).first(header_bytes)).type==Type::status_fall);
    auto bytes=packet(1);check(bytes.size()==40);bytes.push_back(0);rejects([&]{status_fall::decode(bytes);});bytes=packet(1);bytes.pop_back();rejects([&]{status_fall::decode(bytes);});
    for(float bad:{INFINITY,NAN}){rejects([&]{status_fall::decode(packet(1,bad));});rejects([&]{status_fall::decode(packet(1,4,bad));});}
    rejects([&]{status_fall::decode(packet(1,-1025));});rejects([&]{status_fall::decode(packet(1,1025));});
    rejects([&]{status_fall::decode(packet(1,4,-1));});rejects([&]{status_fall::decode(packet(1,4,101));});
    auto& p=edicts[1].player;auto& v=edicts[1].v;
    auto land=[&](float speed){p.damage=p.damage_calls=0;p.m_flFallVelocity=speed;v.health=100;p.Landing();return p.damage;};
    const auto native=[](float speed){return speed>MAX_PLAYER_SAFE_FALL_SPEED?(speed-MAX_PLAYER_SAFE_FALL_SPEED)*DAMAGE_FOR_FALL_SPEED*1.25f:0;};
    const float baseline=land(700);near(baseline,native(700));check(p.m_flFallVelocity==0);
    std::uint64_t revision=0;HandleStatusFall(packet(++revision));
    near(land(700),native(std::sqrt(700.f*700-2*800*32)));check(p.damage<baseline);near(rules.observed,std::sqrt(700.f*700-2*800*32));
    // Default attributes preserve native damage exactly; level I adds one block,
    // level II adds two. Attribute overrides are independent of jump velocity.
    for(float safe:{-1024.f,-1.f,0.f,3.f,4.f,5.f,13.f,259.f,1024.f})for(float mult:{0.f,.5f,1.f,2.f}){
        HandleStatusFall(packet(++revision,safe,mult));
        for(float g:{0.f,400.f,800.f,1600.f})for(float scale:{0.f,.5f,1.f,2.f}){
            gravity.value=g;v.gravity=scale;const float effective=g*(scale==0?1:scale);
            float speed=700;if(effective>0&&safe!=3)speed=std::sqrt(std::max(0.f,700.f*700-2*effective*(safe-3)*32));
            const float expected=native(speed)*mult;near(land(700),expected>=1?expected:0);
            check(p.m_flFallVelocity==0);
        }
    }
    gravity.value=800;v.gravity=1;HandleStatusFall(packet(++revision));
    v.flags=0;check(land(700)==0&&p.m_flFallVelocity==700);v.flags=FL_ONGROUND;
    v.watertype=CONTENT_WATER;check(land(700)==0);v.watertype=-1;
    rules.cancel=true;check(land(700)==0);rules.cancel=false;
    falldamage.value=0;check(land(700)==0);falldamage.value=1;
    mcFall=true;check(land(700)==0);mcFall=false;immune=true;check(land(700)==0);immune=false;
    check(land(0)==0);near(land(700),native(std::sqrt(700.f*700-2*800*32)));
    auto blocked=[&](auto set,auto restore){set();check(!CurrentFall(1));near(land(700),baseline);restore();check(CurrentFall(1)!=nullptr);};
    blocked([&]{++player_lives[1];},[&]{--player_lives[1];});blocked([&]{++player_births[1];},[&]{--player_births[1];});
    blocked([&]{++registry.pair.serial;},[&]{--registry.pair.serial;});blocked([&]{++registry.epoch;},[&]{--registry.epoch;});
    blocked([&]{minecraft_forms[1]=true;},[&]{minecraft_forms[1]=false;});blocked([&]{controlled[1].active=true;},[&]{controlled[1].active=false;});
    blocked([&]{link.capable=false;},[&]{link.capable=true;});blocked([&]{edicts[1].free=true;},[&]{edicts[1].free=false;});
    blocked([&]{p.alive=false;},[&]{p.alive=true;});blocked([&]{globals.time-=.1f;},[&]{globals.time+=.1f;});
    alive_hook=[&]{++player_lives[1];};check(!CurrentFall(1));alive_hook={};--player_lives[1];
    globals.time+=.36f;HandleStatusFall(packet(revision));check(!CurrentFall(1));near(land(700),baseline);
    HandleStatusFall(packet(++revision));check(CurrentFall(1)!=nullptr);ClearStatusFall();near(land(700),baseline);
    check(!CurrentFall(0)&&!CurrentFall(65));link.capable=false;rejects([&]{HandleStatusFall(packet(++revision));});
    std::cout<<checks<<" fall checks: actual landing/rule, attributes, native controls, authority and lifecycle\n";
}
