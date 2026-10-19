#include "goldcraft/client_features.hpp"
#include "goldcraft/status_nausea.hpp"
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
struct {bool capable=true;unsigned gen=1;unsigned generation(){return gen;}bool status_nausea(){return capable;}}link;
std::array<unsigned,65> player_births,player_lives;
std::array<bool,65> client_ready;
struct NativePlayerIdentity {Player* player;unsigned slot,serial,spawn,life,world;int entity_serial;edict_t* table;};
NativePlayerIdentity CaptureNativePlayer(unsigned slot){const auto* pair=registry.get(slot);return {pair?&player:nullptr,slot,pair?pair->serial:0,
 player_births[slot],player_lives[slot],registry.world(),edicts[slot].serialnumber,edicts};}
bool CurrentNativePlayer(const NativePlayerIdentity& i){const auto* pair=registry.get(i.slot);return pair&&!edicts[i.slot].free&&i.world==registry.world()
 &&i.serial==pair->serial&&i.spawn==player_births[i.slot]&&i.life==player_lives[i.slot]&&i.entity_serial==edicts[i.slot].serialnumber;}
const char* capability="1";
struct {edict_t* pfnGetInfoKeyBuffer(edict_t* e){return e;}
 const char* pfnInfoKeyValue(edict_t*,const char* key){return std::strcmp(key,client_features::key)==0?"":capability;}}g_engfuncs;
unsigned sent=0;Bytes forwarded;
void MESSAGE_BEGIN(int,int,void*,edict_t*){++sent;forwarded.clear();}
void WRITE_BYTE(unsigned char b){forwarded.push_back(b);}
void MESSAGE_END(){}
#include "client_features_adapter.hpp"
#include "../../native/server/status_nausea.inc"
int checks;
void check(bool ok){++checks;if(!ok){std::cerr<<"failed check "<<checks<<'\n';std::abort();}}
Bytes packet(std::uint64_t renausea,float previous=1,float current=0,float angle=0,float step=0){Writer w;w.u64(registry.epoch);w.u64(renausea);w.u32(1);
 w.u32(registry.pair.serial);w.u32(player_births[1]);w.u32(player_lives[1]);w.f32(previous);w.f32(current);w.f32(angle);w.f32(step);return w.data;}
template<class F>void rejects(F action){try{action();check(false);}catch(const ProtocolError&){check(true);}}
int main(){
 Key key{};Writer hello;hello.bytes(key);hello.bytes(key);hello.u32(static_cast<unsigned>(Role::fabric_server));hello.u32(capability_status_nausea);
 HostHandshake handshake(key,key,Role::host_server,Role::fabric_server,1);auto frame=encode_frame(Type::hello,0,1,hello.data);
 auto header=decode_header(std::span<const std::uint8_t>(frame).first(header_bytes));handshake.accept(header,hello.data);check(handshake.ready());
 auto welcome=handshake.welcome();Reader reply(welcome);reply.key();reply.u32();check(reply.u32()==capability_status_nausea);
 HostHandshake client(key,key,Role::host_client,Role::fabric_client,2);hello.data[32]=static_cast<std::uint8_t>(Role::fabric_client);
 rejects([&](){client.accept(header,hello.data);});
 auto nauseaFrame=encode_frame(Type::status_nausea,1,2,{});
 check(decode_header(std::span<const std::uint8_t>(nauseaFrame).first(header_bytes)).type==Type::status_nausea);
 player_births.fill(4);player_lives.fill(5);client_ready.fill(true);nausea_message=1;
 auto b=packet(1);HandleStatusNausea(b);check(sent==1&&forwarded==b);
 HandleStatusNausea(b);check(sent==1);
 for(unsigned length=0;length<status_nausea::wire_bytes;++length)rejects([&](){status_nausea::decode(std::span<const std::uint8_t>(b).first(length));});
 for(int i=0;i<8;++i){auto bad=packet(2);bad[i*4]=0;bad[i*4+1]=0;bad[i*4+2]=0;bad[i*4+3]=0;
  if(i==1||i==3)continue;rejects([&](){HandleStatusNausea(bad);});}
 for(float bad:{-1.f,1.01f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
  rejects([&](){HandleStatusNausea(packet(2,bad));});rejects([&](){HandleStatusNausea(packet(2,0,bad));});
  if(bad!=1.01f&&bad!=-1.f)rejects([&](){HandleStatusNausea(packet(2,0,0,bad));});rejects([&](){HandleStatusNausea(packet(2,0,0,0,bad));});}
 rejects([&](){HandleStatusNausea(packet(2,0,0,3.2f,0));});
 rejects([&](){HandleStatusNausea(packet(2,0,0,0,.35f));});
 rejects([&](){auto bad=b;bad.push_back(0);HandleStatusNausea(bad);});
 rejects([&](){auto bad=b;bad.pop_back();HandleStatusNausea(bad);});
 auto stale=packet(2);++player_lives[1];HandleStatusNausea(stale);--player_lives[1];
 ++player_births[1];HandleStatusNausea(stale);--player_births[1];
 ++registry.epoch;HandleStatusNausea(stale);--registry.epoch;
 ++registry.pair.serial;HandleStatusNausea(stale);--registry.pair.serial;check(sent==1);
 edicts[1].free=true;HandleStatusNausea(stale);edicts[1].free=false;
 player.alive=false;HandleStatusNausea(stale);player.alive=true;
 registry.present=false;HandleStatusNausea(stale);registry.present=true;
 alive_hook=[&](){++edicts[1].serialnumber;};HandleStatusNausea(stale);alive_hook={};check(sent==1);
 capability="";HandleStatusNausea(packet(3));capability="1";check(sent==1);
 client_ready[1]=false;HandleStatusNausea(packet(4));client_ready[1]=true;check(sent==1);
 edicts[1].v.flags=FL_FAKECLIENT;HandleStatusNausea(packet(5));edicts[1].v.flags=0;check(sent==1);
 link.capable=false;rejects([&](){HandleStatusNausea(packet(6));});link.capable=true;
 HandleStatusNausea(packet(6,0));check(sent==2&&status_nausea::decode(forwarded).previous==0);
 ClearStatusNausea();HandleStatusNausea(packet(1));check(sent==3);
 status_nausea::Receiver receiver;auto state=status_nausea::decode(packet(1,0,1,.5f,.1f));
 check(receiver.sample(1,1,3,5,1).intensity==0);check(receiver.accept(state,1));
 check(std::abs(receiver.sample(1,1,3,5,1.025).intensity-.5f)<.00001f);
 check(std::abs(receiver.sample(1,1,3,5,1.025).angle-.55f)<.00001f);
 check(receiver.sample(1,1,3,5,1.349).intensity==1);
 check(receiver.sample(1,1,3,5,1.351).intensity==0);check(!receiver.accept(state,1.352));
 for(auto time:{.9,std::numeric_limits<double>::quiet_NaN()})check(receiver.sample(1,1,3,5,time).intensity==0);
 check(receiver.sample(2,1,3,5,1.1).intensity==0);check(receiver.sample(1,2,3,5,1.1).intensity==0);
 check(receiver.sample(1,1,4,5,1.1).intensity==0);check(receiver.sample(1,1,3,6,1.1).intensity==0);
 state.revision=2;state.previous=state.current=0;check(receiver.accept(state,1.4));check(receiver.sample(1,1,3,5,1.4).intensity==0);
 check(!receiver.accept(state,std::numeric_limits<double>::quiet_NaN()));receiver.clear();check(receiver.sample(1,1,3,5,1.4).intensity==0);
 view_distortion::Frame paired;paired.intensity=0;paired.angle=2;
 check(status_nausea::select(&paired,{1,1}).intensity==0);check(status_nausea::select(&paired,{1,1}).angle==2);
 check(status_nausea::select(nullptr,{1,1}).intensity==1);
 check(status_nausea::select(nullptr,{1,1},true).intensity==0);
 const auto previousSent=sent;alive_hook=[&](){++link.gen;};HandleStatusNausea(packet(100));alive_hook={};check(sent==previousSent);
 HandleStatusNausea(packet(1));check(sent==previousSent+1);const auto beforeReset=status_nausea::decode(forwarded).revision;
 ClearStatusNausea();++link.gen;HandleStatusNausea(packet(1));check(status_nausea::decode(forwarded).revision>beforeReset);
 check(status_nausea::decode(forwarded).slot==1); // owner-only relay
 std::cout<<checks<<" nausea checks: server authority/reentry, strict decode, interpolation, lease and paired accessibility priority\n";
}
