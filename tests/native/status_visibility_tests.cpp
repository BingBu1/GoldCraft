#include "goldcraft/status_visibility.hpp"
#include <array>
#include <cstdlib>
#include <functional>
#include <iostream>
using namespace goldcraft;
constexpr int kRenderTransTexture=2,kRenderFxNone=0;
constexpr int EF_BRIGHTFIELD=1,EF_MUZZLEFLASH=2,EF_BRIGHTLIGHT=4,EF_DIMLIGHT=8,EF_LIGHT=64;
struct entity_state_t {int rendermode=3,renderamt=180,renderfx=7,effects=255,modelindex=9,solid=3,movetype=3;};
struct edict_t {bool free=false;int serialnumber=1;};
std::array<edict_t,65> storage;edict_t* edicts=storage.data();int max_clients=2;
int ENTINDEX(edict_t* e){return static_cast<int>(e-edicts);}
std::function<void()> alive_hook;
struct Player {bool alive=true;bool IsAlive()const{if(alive_hook)alive_hook();return alive;}}player;
struct Binding {unsigned serial=3;};
struct {Binding pair;unsigned epoch=1;bool present=true;
    const Binding* get(unsigned slot){return present&&slot>=1&&slot<=2?&pair:nullptr;}unsigned world(){return epoch;}}registry;
struct {bool capable=true;bool status_visibility(){return capable;}}link;
struct {float time=1;}globals;auto* gpGlobals=&globals;
std::array<unsigned,65> player_births,player_lives;
struct Identity {Player* player;unsigned slot,serial,spawn,life,world;int engine_serial;};
Identity CaptureNativePlayer(unsigned slot){const auto* pair=registry.get(slot);return {pair?&player:nullptr,slot,pair?pair->serial:0,
    player_births[slot],player_lives[slot],registry.world(),edicts[slot].serialnumber};}
bool CurrentNativePlayer(const Identity& i){const auto* pair=registry.get(i.slot);return pair&&!edicts[i.slot].free&&i.world==registry.world()
    &&i.serial==pair->serial&&i.spawn==player_births[i.slot]&&i.life==player_lives[i.slot]&&i.engine_serial==edicts[i.slot].serialnumber;}
#include "../../native/server/status_visibility.inc"
#include "actual_status_visibility.inc"
int checks;
void check(bool ok){++checks;if(!ok){std::cerr<<"failed check "<<checks<<'\n';std::abort();}}
Bytes packet(std::uint64_t revision,unsigned flags=1,unsigned slot=1){Writer w;w.u64(registry.epoch);w.u64(revision);w.u32(slot);
    w.u32(registry.pair.serial);w.u32(player_births[slot]);w.u32(player_lives[slot]);w.u32(flags);return w.data;}
template<class F>void rejects(F action){try{action();check(false);}catch(const ProtocolError&){check(true);}}
void visible(edict_t* e){entity_state_t s;GoldCraft_StatusVisibility(&s,e);check(s.rendermode==3&&s.renderamt==180&&s.renderfx==7&&s.effects==255);
    check(s.modelindex==9&&s.solid==3&&s.movetype==3);}
int main(){
    Key key{};Writer hello;hello.bytes(key);hello.bytes(key);hello.u32(static_cast<unsigned>(Role::fabric_server));hello.u32(capability_status_visibility);
    HostHandshake handshake(key,key,Role::host_server,Role::fabric_server,1);auto frame=encode_frame(Type::hello,0,1,hello.data);
    auto header=decode_header(std::span<const std::uint8_t>(frame).first(header_bytes));handshake.accept(header,hello.data);check(handshake.ready());
    auto welcome=handshake.welcome();Reader reply(welcome);reply.key();reply.u32();check(reply.u32()==capability_status_visibility);
    HostHandshake client(key,key,Role::host_client,Role::fabric_client,2);hello.data[32]=static_cast<std::uint8_t>(Role::fabric_client);
    rejects([&](){client.accept(header,hello.data);});
    auto visibilityFrame=encode_frame(Type::status_visibility,1,2,{});
    check(decode_header(std::span<const std::uint8_t>(visibilityFrame).first(header_bytes)).type==Type::status_visibility);
    player_births.fill(4);player_lives.fill(5);auto* e=edicts+1;
    visible(e);HandleStatusVisibility(packet(1));check(StatusInvisible(e));
    entity_state_t s;GoldCraft_StatusVisibility(&s,e);
    check(s.rendermode==kRenderTransTexture&&s.renderamt==0&&s.renderfx==kRenderFxNone);
    check(s.effects==(255&~(EF_BRIGHTFIELD|EF_MUZZLEFLASH|EF_BRIGHTLIGHT|EF_DIMLIGHT|EF_LIGHT)));
    check(s.modelindex==9&&s.solid==3&&s.movetype==3); // rendering never removes prediction or hit geometry
    visible(edicts+2);visible(nullptr);GoldCraft_StatusVisibility(nullptr,e);
    HandleStatusVisibility(packet(2,0));visible(e);HandleStatusVisibility(packet(1));visible(e);
    HandleStatusVisibility(packet(3));globals.time+=.36f;visible(e);HandleStatusVisibility(packet(3));visible(e);
    HandleStatusVisibility(packet(4));globals.time-=.1f;visible(e);globals.time+=.1f;
    ++player_lives[1];visible(e);--player_lives[1];++player_births[1];visible(e);--player_births[1];
    ++registry.pair.serial;visible(e);--registry.pair.serial;++registry.epoch;visible(e);--registry.epoch;
    player.alive=false;visible(e);HandleStatusVisibility(packet(5));player.alive=true;
    globals.time+=.36f;visible(e);e->free=true;HandleStatusVisibility(packet(6));e->free=false;visible(e);
    HandleStatusVisibility(packet(7));alive_hook=[&](){++player_lives[1];};visible(e);alive_hook={};--player_lives[1];
    ClearStatusVisibility();alive_hook=[&](){++edicts[1].serialnumber;};HandleStatusVisibility(packet(8));alive_hook={};visible(e);
    HandleStatusVisibility(packet(9));link.capable=false;visible(e);rejects([&](){HandleStatusVisibility(packet(10));});link.capable=true;
    rejects([&](){auto b=packet(10);b.push_back(0);HandleStatusVisibility(b);});
    rejects([&](){auto b=packet(10);b.pop_back();HandleStatusVisibility(b);});
    rejects([&](){HandleStatusVisibility(packet(10,2));});rejects([&](){HandleStatusVisibility(packet(0));});
    rejects([&](){HandleStatusVisibility(packet(10,1,0));});
    ClearStatusVisibility();visible(e);registry.present=false;HandleStatusVisibility(packet(11));visible(e);registry.present=true;
    HandleStatusVisibility(packet(12));check(StatusInvisible(e));ClearStatusVisibility();visible(e);
    std::cout<<checks<<" visibility checks: actual consumer, model/solid preserved, expiry/identity/reentry and authenticated capability\n";
}
