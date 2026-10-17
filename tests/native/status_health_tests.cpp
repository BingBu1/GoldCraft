#include "goldcraft/status_health.hpp"
#include "../../native/server/bridge.h"
#include <array>
#include <functional>
#include <iostream>
#include <source_location>
using namespace goldcraft;
using edict_t=edict_s;
using BOOL=int;
constexpr int FALSE=0,TRUE=1,DAMAGE_NO=0;
struct entvars_t {float health=100,max_health=100;int takedamage=1;edict_t* entity=nullptr;};
struct CBaseEntity {
    entvars_t* pev=nullptr;
    BOOL TakeHealth(float,int);
    bool CanTakeHealth(float) const;
};
std::function<void()> alive_hook;
struct CBasePlayer:CBaseEntity {bool alive=true;bool IsAlive()const{if(alive_hook)alive_hook();return alive;}};
struct edict_s {bool free=false;int serialnumber=1;entvars_t v;CBasePlayer player;};
std::array<edict_t,65> storage;edict_t* edicts=storage.data();int max_clients=2;
int ENTINDEX(edict_t* e){return static_cast<int>(e-edicts);}
edict_t* ENT(entvars_t* v){return v->entity;}
struct Binding{unsigned serial=3;};
struct {
    Binding pair;std::uint64_t epoch=1;bool present=true;
    const Binding* get(unsigned slot){return present&&slot>=1&&slot<=2?&pair:nullptr;}
    std::uint64_t world(){return epoch;}
} registry;
struct {
    bool capable=true;std::uint64_t serial=1;std::vector<Message> sent;
    bool status_health(){return capable;}
    std::uint64_t generation(){return serial;}
    void send(Type type,const Bytes& bytes){sent.push_back({type,bytes});}
} link;
struct {float time=1;} globals;auto* gpGlobals=&globals;
float next_snapshot=1;std::uint64_t tick=7;
std::array<unsigned,65> player_births,player_lives;
struct Identity{CBasePlayer* player;unsigned slot,serial,spawn,life;std::uint64_t world;int engine_serial;};
Identity CaptureNativePlayer(unsigned slot){const auto* p=registry.get(slot);return {p?&edicts[slot].player:nullptr,slot,p?p->serial:0,player_births[slot],player_lives[slot],registry.world(),edicts[slot].serialnumber};}
bool CurrentNativePlayer(const Identity& i){const auto* p=registry.get(i.slot);return p&&!edicts[i.slot].free&&i.world==registry.world()
    &&i.serial==p->serial&&i.spawn==player_births[i.slot]&&i.life==player_lives[i.slot]&&i.engine_serial==edicts[i.slot].serialnumber;}
#include "../../native/server/status_health.inc"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#include "actual_status_health.inc"
#pragma clang diagnostic pop
int checks=0;
void check(bool value,const std::source_location where=std::source_location::current()){
    ++checks;if(!value){std::cerr<<"health check "<<checks<<" failed at line "<<where.line()<<'\n';std::abort();}
}
void near(float a,float b,const std::source_location where=std::source_location::current()){check(std::abs(a-b)<.001f,where);}
Bytes packet(std::uint64_t revision,float base=100,float effective=120){Writer w;w.u64(registry.epoch);w.u64(revision);w.u32(1);w.u32(registry.pair.serial);w.u32(player_births[1]);w.u32(player_lives[1]);w.f32(base);w.f32(effective);return w.data;}
template<class F>void rejects(F f){try{f();check(false);}catch(const ProtocolError&){check(true);}}
int main(){
    for(unsigned n=0;n<storage.size();++n){storage[n].v.entity=&storage[n];storage[n].player.pev=&storage[n].v;player_births[n]=4;player_lives[n]=5;}
    Key key{};Writer hello;hello.bytes(key);hello.bytes(key);hello.u32(static_cast<unsigned>(Role::fabric_server));hello.u32(capability_status_health);
    HostHandshake hs(key,key,Role::host_server,Role::fabric_server,1);
    auto frame=encode_frame(Type::hello,0,1,hello.data);auto header=decode_header(std::span<const std::uint8_t>(frame).first(header_bytes));
    hs.accept(header,hello.data);check(hs.ready());auto welcome=hs.welcome();Reader wr(welcome);wr.key();wr.u32();check(wr.u32()==capability_status_health);
    HostHandshake client(key,key,Role::host_client,Role::fabric_client,2);hello.data[32]=static_cast<std::uint8_t>(Role::fabric_client);rejects([&]{client.accept(header,hello.data);});
    for(auto type:{Type::health_limits,Type::status_health}){frame=encode_frame(type,1,2,{});check(decode_header(std::span<const std::uint8_t>(frame).first(header_bytes)).type==type);check(static_cast<unsigned>(type)<wire_type_count);}
    auto bytes=packet(1);check(bytes.size()==40);
    for(std::size_t n=0;n<bytes.size();++n)rejects([&]{status_health::decode(std::span<const std::uint8_t>(bytes).first(n));});
    bytes.push_back(0);rejects([&]{status_health::decode(bytes);});
    for(float bad:{-1.f,0.f,10000001.f,INFINITY,NAN}){rejects([&]{status_health::decode(packet(1,bad,120));});rejects([&]{status_health::decode(packet(1,100,bad));});}
    rejects([&]{status_health::decode(packet(0));});
    auto& p=edicts[1].player;auto& v=edicts[1].v;std::uint64_t revision=0;
    check(!p.TakeHealth(20,0));v.health=70;check(p.TakeHealth(20,0));near(v.health,90);check(!p.CanTakeHealth(11));
    HandleStatusHealth(packet(++revision));near(v.max_health,100);near(v.health,90);check(p.CanTakeHealth(30));check(!p.CanTakeHealth(31));
    check(p.TakeHealth(100,0));near(v.health,120);near(v.max_health,100);check(!p.TakeHealth(1,0));
    v.health=110;HandleStatusHealth(packet(++revision,100,100));near(v.health,100);
    HandleStatusHealth(packet(++revision));v.health=170;HandleStatusHealth(packet(++revision,100,100));near(v.health,150);
    HandleStatusHealth(packet(++revision));v.health=50;HandleStatusHealth(packet(++revision,100,100));near(v.health,50);
    HandleStatusHealth(packet(++revision));v.health=110;globals.time+=.36f;
    check(!CurrentHealthLimit(1));near(v.health,110);check(!p.TakeHealth(1,0));
    HandleStatusHealth(packet(revision));check(!CurrentHealthLimit(1));
    HandleStatusHealth(packet(++revision));check(CurrentHealthLimit(1));
    // A new class owns a new base immediately; an in-flight old-class lease loses.
    v.max_health=200;v.health=150;check(!CurrentHealthLimit(1));HandleStatusHealth(packet(++revision));check(!CurrentHealthLimit(1));
    check(p.TakeHealth(80,0));near(v.health,200);HandleStatusHealth(packet(++revision,200,240));check(p.TakeHealth(80,0));near(v.health,240);near(v.max_health,200);
    for(float base:{1.f,100.f,1200.f,12000.f,1000000.f})for(float bonus:{0.f,20.f,40.f,5120.f}){
        ClearStatusHealth();v.max_health=base;v.health=base;HandleStatusHealth(packet(++revision,base,base+bonus));
        near(v.health,base);if(bonus>0)check(p.TakeHealth(bonus,0));near(v.health,base+bonus);near(v.max_health,base);
        HandleStatusHealth(packet(++revision,base,base));near(v.health,base);
    }
    ClearStatusHealth();v.max_health=v.health=100;HandleStatusHealth(packet(++revision));
    auto blocked=[&](auto set,auto restore){set();check(!CurrentHealthLimit(1));near(GoldCraft_StatusMaxHealth(&edicts[1],100),100);restore();check(CurrentHealthLimit(1));};
    blocked([&]{++player_lives[1];},[&]{--player_lives[1];});blocked([&]{++player_births[1];},[&]{--player_births[1];});
    blocked([&]{++registry.pair.serial;},[&]{--registry.pair.serial;});blocked([&]{++registry.epoch;},[&]{--registry.epoch;});
    blocked([&]{++link.serial;},[&]{--link.serial;});blocked([&]{link.capable=false;},[&]{link.capable=true;});
    blocked([&]{p.alive=false;},[&]{p.alive=true;});blocked([&]{edicts[1].free=true;},[&]{edicts[1].free=false;});
    blocked([&]{++edicts[1].serialnumber;},[&]{--edicts[1].serialnumber;});blocked([&]{globals.time-=.1f;},[&]{globals.time+=.1f;});
    alive_hook=[&]{++player_lives[1];};check(!CurrentHealthLimit(1));alive_hook={};--player_lives[1];
    v.takedamage=DAMAGE_NO;check(!p.TakeHealth(10,0));v.takedamage=1;
    // A form transition has a new life but the same spawn and effects.
    ++player_lives[1];check(!CurrentHealthLimit(1));v.health=120;HandleStatusHealth(packet(++revision));near(v.health,120);check(CurrentHealthLimit(1));
    SendHealthLimits({1,2});check(link.sent.size()==1&&link.sent[0].type==Type::health_limits);
    Reader snap(link.sent[0].payload);check(snap.u64()==registry.world()&&snap.u64()==tick&&snap.u32()==2);
    check(snap.u32()==1&&snap.u32()==3&&snap.u32()==4&&snap.u32()==player_lives[1]);near(snap.f32(),100);near(snap.f32(),120);check(snap.u64()==revision);
    check(snap.u32()==2&&snap.u32()==3&&snap.u32()==4&&snap.u32()==5);near(snap.f32(),100);near(snap.f32(),100);check(snap.u64()==0);snap.finish();
    auto previous=native_health_limits[1].state.revision;alive_hook=[&]{v.max_health=200;};HandleStatusHealth(packet(++revision));alive_hook={};check(native_health_limits[1].state.revision==previous);v.max_health=100;
    check(!CurrentHealthLimit(0)&&!CurrentHealthLimit(65));near(GoldCraft_StatusMaxHealth(nullptr,77),77);
    link.capable=false;SendHealthLimits({1});check(link.sent.size()==1);rejects([&]{HandleStatusHealth(packet(++revision));});
    link.capable=true;ClearStatusHealth();check(!CurrentHealthLimit(1));near(v.max_health,100);
    std::cout<<checks<<" health checks: actual native healing, cap changes, rewards, high HP, identity and authority lifecycle\n";
}
