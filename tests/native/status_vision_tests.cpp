#include "goldcraft/status_vision.hpp"
#include <array>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
using namespace goldcraft;
constexpr int FL_FAKECLIENT=8192,MSG_ONE=1;
struct edict_t {bool free=false;int serialnumber=1;struct {int flags=0;}v;};
std::array<edict_t,65> storage;edict_t* edicts=storage.data();int max_clients=2;
std::function<void()> alive_hook;
struct Player {bool alive=true;bool IsAlive()const{if(alive_hook)alive_hook();return alive;}}player;
struct Binding {unsigned serial=3;};
struct {Binding pair;unsigned epoch=1;bool present=true;
 const Binding* get(unsigned slot){return present&&slot>=1&&slot<=2?&pair:nullptr;}unsigned world(){return epoch;}}registry;
struct {bool capable=true;bool status_vision(){return capable;}}link;
std::array<unsigned,65> player_births,player_lives;
std::array<bool,65> client_ready;
struct Identity {Player* player;unsigned slot,serial,spawn,life,world;int engine_serial;};
Identity CaptureNativePlayer(unsigned slot){const auto* pair=registry.get(slot);return {pair?&player:nullptr,slot,pair?pair->serial:0,
 player_births[slot],player_lives[slot],registry.world(),edicts[slot].serialnumber};}
bool CurrentNativePlayer(const Identity& i){const auto* pair=registry.get(i.slot);return pair&&!edicts[i.slot].free&&i.world==registry.world()
 &&i.serial==pair->serial&&i.spawn==player_births[i.slot]&&i.life==player_lives[i.slot]&&i.engine_serial==edicts[i.slot].serialnumber;}
const char* capability="1";
struct {edict_t* pfnGetInfoKeyBuffer(edict_t* e){return e;}
 const char* pfnInfoKeyValue(edict_t*,const char*){return capability;}}g_engfuncs;
unsigned sent=0;Bytes forwarded;
void MESSAGE_BEGIN(int,int,void*,edict_t*){++sent;forwarded.clear();}
void WRITE_BYTE(unsigned char b){forwarded.push_back(b);}
void MESSAGE_END(){}
#include "../../native/server/status_vision.inc"
int checks;
void check(bool ok){++checks;if(!ok){std::cerr<<"failed check "<<checks<<'\n';std::abort();}}
Bytes packet(std::uint64_t revision,float night=1,float blind=0,float dark=0,float pulse=0){Writer w;w.u64(registry.epoch);w.u64(revision);w.u32(1);
 w.u32(registry.pair.serial);w.u32(player_births[1]);w.u32(player_lives[1]);w.f32(night);w.f32(blind);w.f32(dark);w.f32(pulse);return w.data;}
template<class F>void rejects(F action){try{action();check(false);}catch(const ProtocolError&){check(true);}}
int main(){
 Key key{};Writer hello;hello.bytes(key);hello.bytes(key);hello.u32(static_cast<unsigned>(Role::fabric_server));hello.u32(capability_status_vision);
 HostHandshake handshake(key,key,Role::host_server,Role::fabric_server,1);auto frame=encode_frame(Type::hello,0,1,hello.data);
 auto header=decode_header(std::span<const std::uint8_t>(frame).first(header_bytes));handshake.accept(header,hello.data);check(handshake.ready());
 auto welcome=handshake.welcome();Reader reply(welcome);reply.key();reply.u32();check(reply.u32()==capability_status_vision);
 HostHandshake client(key,key,Role::host_client,Role::fabric_client,2);hello.data[32]=static_cast<std::uint8_t>(Role::fabric_client);
 rejects([&](){client.accept(header,hello.data);});
 auto visionFrame=encode_frame(Type::status_vision,1,2,{});
 check(decode_header(std::span<const std::uint8_t>(visionFrame).first(header_bytes)).type==Type::status_vision);
 player_births.fill(4);player_lives.fill(5);client_ready.fill(true);vision_message=1;
 auto b=packet(1);HandleStatusVision(b);check(sent==1&&forwarded==b);
 HandleStatusVision(b);check(sent==1);
 for(int i=0;i<8;++i){auto bad=packet(2);bad[i*4]=0;bad[i*4+1]=0;bad[i*4+2]=0;bad[i*4+3]=0;
  if(i==1||i==3)continue;rejects([&](){HandleStatusVision(bad);});}
 for(float bad:{-1.f,1.01f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
  rejects([&](){HandleStatusVision(packet(2,bad));});rejects([&](){HandleStatusVision(packet(2,0,bad));});
  rejects([&](){HandleStatusVision(packet(2,0,0,bad));});rejects([&](){HandleStatusVision(packet(2,0,0,0,bad));});}
 rejects([&](){HandleStatusVision(packet(2,0,0,.5f,.3f));});
 rejects([&](){auto bad=b;bad.push_back(0);HandleStatusVision(bad);});
 rejects([&](){auto bad=b;bad.pop_back();HandleStatusVision(bad);});
 auto stale=packet(2);++player_lives[1];HandleStatusVision(stale);--player_lives[1];
 ++player_births[1];HandleStatusVision(stale);--player_births[1];
 ++registry.epoch;HandleStatusVision(stale);--registry.epoch;
 ++registry.pair.serial;HandleStatusVision(stale);--registry.pair.serial;check(sent==1);
 edicts[1].free=true;HandleStatusVision(stale);edicts[1].free=false;
 player.alive=false;HandleStatusVision(stale);player.alive=true;
 registry.present=false;HandleStatusVision(stale);registry.present=true;
 alive_hook=[&](){++edicts[1].serialnumber;};HandleStatusVision(stale);alive_hook={};check(sent==1);
 capability="";HandleStatusVision(packet(3));capability="1";check(sent==1);
 client_ready[1]=false;HandleStatusVision(packet(4));client_ready[1]=true;check(sent==1);
 edicts[1].v.flags=FL_FAKECLIENT;HandleStatusVision(packet(5));edicts[1].v.flags=0;check(sent==1);
 link.capable=false;rejects([&](){HandleStatusVision(packet(6));});link.capable=true;
 HandleStatusVision(packet(6,0));check(sent==2&&status_vision::decode(forwarded).night==0);
 ClearStatusVision();HandleStatusVision(packet(1));check(sent==3);
 status_vision::Receiver receiver;auto state=status_vision::decode(packet(1));
 check(!receiver.active(1,1,3,5,1));check(receiver.accept(state,1));check(receiver.active(1,1,3,5,1.349));
 check(!receiver.active(1,1,3,5,1.351));check(!receiver.accept(state,1.352));check(!receiver.active(1,1,3,5,.9));
 check(!receiver.active(2,1,3,5,1));check(!receiver.active(1,2,3,5,1));check(!receiver.active(1,1,4,5,1));check(!receiver.active(1,1,3,6,1));
 state.revision=2;state.night=0;check(receiver.accept(state,1.4));check(receiver.active(1,1,3,5,1.4)->night==0);
 check(!receiver.accept(state,std::numeric_limits<double>::quiet_NaN()));receiver.clear();check(!receiver.active(1,1,3,5,1.4));
 std::cout<<checks<<" vision checks: actual server consumer, capability/identity/reentry, exact forwarding and client lease\n";
}
