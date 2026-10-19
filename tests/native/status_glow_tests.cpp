#include "goldcraft/client_features.hpp"
#include "goldcraft/status_glow.hpp"
#include <cstring>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
using namespace goldcraft;
constexpr int FL_FAKECLIENT=1,EF_NODRAW=2,EF_OWNER_NO_VISIBILITY=4,EF_OWNER_VISIBILITY=8,EF_FORCEVISIBILITY=16;
constexpr int FL_SPECTATOR=2,FL_SKIPLOCALHOST=4,GROUP_OP_AND=0,GROUP_OP_NAND=1,MSG_ONE=1;
#define BOOL bool
#define TRUE true
#define FALSE false
#define EXT_FUNC
#define REGAMEDLL_ADD
struct edict_t {bool free=false;int serialnumber=1;void* pvPrivateData=this;
    struct {int flags=0,effects=0,modelindex=1,groupinfo=0;const char* model="player";edict_t* owner=nullptr;}v;};
std::array<edict_t,65> storage;edict_t* edicts=storage.data();int max_clients=3;
int ENTINDEX(edict_t* e){return static_cast<int>(e-edicts);}
const char* STRING(const char* s){return s;}
struct entity_state_s {};
struct {float time=1;}globals;auto* gpGlobals=&globals;
std::function<void()> alive_hook;
struct Player {bool alive=true;bool IsAlive()const{if(alive_hook)alive_hook();return alive;}}player;
struct Binding {unsigned serial=3,userid=11;Key uuid{};};
struct {Binding pair;unsigned epoch=1;bool present=true;
    const Binding* get(unsigned slot){return present&&slot>=1&&slot<=3?&pair:nullptr;}unsigned world(){return epoch;}}registry;
struct {bool capable=true;std::uint64_t gen=1;bool status_glow(){return capable;}std::uint64_t generation(){return gen;}}link;
std::array<unsigned,65> player_births,player_lives;
std::array<bool,65> client_ready;
std::array<const char*,65> viewer_caps;
struct NativePlayerIdentity {Player* player=nullptr;edict_t* table=nullptr;unsigned slot=0,serial=0,spawn=0,life=0,world=0;int entity_serial=0;};
NativePlayerIdentity CaptureNativePlayer(unsigned slot){const auto* pair=registry.get(slot);return {pair?&player:nullptr,edicts,slot,pair?pair->serial:0,
    player_births[slot],player_lives[slot],registry.world(),edicts[slot].serialnumber};}
bool CurrentNativePlayer(const NativePlayerIdentity& i){const auto* pair=registry.get(i.slot);return pair&&i.table==edicts&&!edicts[i.slot].free&&i.world==registry.world()
    &&i.serial==pair->serial&&i.spawn==player_births[i.slot]&&i.life==player_lives[i.slot]&&i.entity_serial==edicts[i.slot].serialnumber;}
struct {edict_t* pfnGetInfoKeyBuffer(edict_t* e){return e;}const char* pfnInfoKeyValue(edict_t* e,const char* key){return std::strcmp(key,client_features::key)==0?"":viewer_caps[ENTINDEX(e)];}}g_engfuncs;
struct Sent {int slot=0;Bytes bytes;};std::vector<Sent> sent;
void MESSAGE_BEGIN(int,int,const void*,edict_t* e){sent.push_back({ENTINDEX(e),{}});}
void WRITE_BYTE(unsigned b){sent.back().bytes.push_back(static_cast<std::uint8_t>(b));}
void MESSAGE_END(){}
#include "client_features_adapter.hpp"
#include "../../native/server/status_glow.inc"
bool GoldCraft_StatusGlowVisible(edict_t* entity,edict_t* viewer){return StatusGlowVisible(entity,viewer);}
bool pvs=false,recent=false,leaf_changed=false,map_changed=false;int positive_marks=0,negative_marks=0,g_groupop=GROUP_OP_AND;
bool GoldCraft_MapVisibilityChanged(int){return map_changed;}
bool CheckPlayerPVSLeafChanged(edict_t*,int){return leaf_changed;}
void ResetPlayerPVS(edict_t*,int){recent=false;}
bool CheckEntityRecentlyInPVS(int,int,float){return recent;}
bool ENGINE_CHECK_VISIBILITY(edict_t*,unsigned char*){return pvs;}
void MarkEntityInPVS(int,int,float,bool yes){if(yes){++positive_marks;recent=true;}else{++negative_marks;recent=false;}}
void UTIL_SetGroupTrace(int,int op){g_groupop=op;}
void UTIL_UnsetGroupTrace(){}
#include "actual_status_glow.inc"
int checks;
void check(bool ok){++checks;if(!ok){std::cerr<<"failed check "<<checks<<'\n';std::abort();}}
template<class F>void rejects(F f){try{f();check(false);}catch(const ProtocolError&){check(true);}}
Bytes packet(std::uint64_t revision,unsigned flags=1,unsigned color=0x5577ff,unsigned slot=1){
    Writer w;w.u64(registry.epoch);w.u64(revision);w.u32(slot);w.u32(registry.pair.serial);
    w.u32(player_births[slot]);w.u32(player_lives[slot]);w.u32(flags);w.u32(color);return w.data;
}
void receiver_test(){
    status_glow::Receiver receiver;status_glow::Presentation p{status_glow::decode(packet(1)),11,{}};
    PlayerPresentation roster{p.identity(),1};
    check(!receiver.active(roster,1,1));check(receiver.accept(p,1,1));check(receiver.active(roster,1,1));
    check(!receiver.accept(p,1,1.2));check(!receiver.active(roster,1,1.36));check(!receiver.active(roster,1,.9));
    check(!receiver.active(roster,2,1));check(!receiver.active(roster,1,std::numeric_limits<double>::quiet_NaN()));
    for(unsigned flags=0;flags<128;++flags){roster.flags=flags;check(bool(receiver.active(roster,1,1))==((flags&5u)==1u));}roster.flags=1;
    for(int field=0;field<4;++field){auto wrong=roster;if(field==0)++wrong.identity.slot;if(field==1)++wrong.identity.serial;
        if(field==2)++wrong.identity.userid;if(field==3)++wrong.identity.uuid[0];check(!receiver.active(wrong,1,1));}
    p.state.revision=2;p.state.flags=0;p.state.color=0;check(receiver.accept(p,1,1.1));check(!receiver.active(roster,1,1.1));
    p.state.revision=1;p.state.flags=1;p.state.color=0;check(!receiver.accept(p,1,1.1));
    p.state.revision=3;p.state.spawn++;p.state.life++;check(receiver.accept(p,1,1.1));check(receiver.active(roster,1,1.1));
    auto bytes=status_glow::encode(p);check(bytes.size()==status_glow::wire_bytes);check(status_glow::decode_presentation(bytes).identity()==p.identity());
    for(std::size_t n=0;n<bytes.size();++n)rejects([&]{status_glow::decode_presentation(std::span(bytes).first(n));});
    bytes.push_back(0);rejects([&]{status_glow::decode_presentation(bytes);});
    p.userid=0;check(!receiver.accept(p,1,1.2));rejects([&]{status_glow::encode(p);});p.userid=11;
    p.state.epoch=2;p.state.revision=1;check(!receiver.accept(p,1,1.2));check(receiver.accept(p,2,1.2));
    check(!receiver.active(roster,1,1.2));receiver.clear();check(!receiver.active(roster,2,1.2));
}
int main(){
    player_births.fill(4);player_lives.fill(5);client_ready.fill(true);viewer_caps.fill("1");glow_message=1;
    Key key{};Writer hello;hello.bytes(key);hello.bytes(key);hello.u32(static_cast<unsigned>(Role::fabric_server));hello.u32(capability_status_glow);
    HostHandshake handshake(key,key,Role::host_server,Role::fabric_server,1);auto frame=encode_frame(Type::hello,0,1,hello.data);
    auto header=decode_header(std::span<const std::uint8_t>(frame).first(header_bytes));handshake.accept(header,hello.data);check(handshake.ready());
    auto welcome=handshake.welcome();Reader reply(welcome);reply.key();reply.u32();check(reply.u32()==capability_status_glow);
    HostHandshake client(key,key,Role::host_client,Role::fabric_client,2);hello.data[32]=static_cast<std::uint8_t>(Role::fabric_client);
    rejects([&]{client.accept(header,hello.data);});
    frame=encode_frame(Type::status_glow,1,2,{});check(decode_header(std::span<const std::uint8_t>(frame).first(header_bytes)).type==Type::status_glow);
    receiver_test();
    auto* e=edicts+1;auto* viewer=edicts+2;
    check(!CurrentGlow(1));HandleStatusGlow(packet(1));check(CurrentGlow(1)&&CurrentGlow(1)->color==0x5577ff);
    check(StatusGlowVisible(e,viewer));check(!StatusGlowVisible(nullptr,viewer));check(!StatusGlowVisible(e,nullptr));
    for(unsigned flags=2;flags<64;++flags)rejects([&]{HandleStatusGlow(packet(2,flags));});
    rejects([&]{HandleStatusGlow(packet(2,1,0x1000000));});rejects([&]{HandleStatusGlow(packet(2,0,1));});
    for(std::size_t n=0;n<40;++n)rejects([&]{auto b=packet(2);HandleStatusGlow(std::span(b).first(n));});
    rejects([&]{auto b=packet(2);b.push_back(0);HandleStatusGlow(b);});rejects([&]{HandleStatusGlow(packet(0));});
    viewer_caps[3]="0";SendStatusGlow();check(sent.size()==2);auto first=status_glow::decode_presentation(sent[0].bytes);
    check(first.state.flags==1&&first.state.color==0x5577ff&&first.identity().userid==11);
    HandleStatusGlow(packet(2));SendStatusGlow();check(sent.size()==2); // producer ticks do not amplify viewer traffic
    globals.time=1.16f;HandleStatusGlow(packet(3));SendStatusGlow();check(sent.size()==4);
    auto second=status_glow::decode_presentation(sent[2].bytes);check(second.state.revision>first.state.revision);
    globals.time=1.52f;check(!CurrentGlow(1));SendStatusGlow();check(sent.size()==6);
    auto clear=status_glow::decode_presentation(sent[4].bytes);check(!clear.state.flags&&!clear.state.color&&clear.state.revision>second.state.revision);
    HandleStatusGlow(packet(3));check(!CurrentGlow(1));SendStatusGlow();check(sent.size()==6);
    HandleStatusGlow(packet(4));SendStatusGlow();check(sent.size()==8);
    HandleStatusGlow(packet(5,0,0));SendStatusGlow();check(sent.size()==10&&!CurrentGlow(1));
    HandleStatusGlow(packet(4));check(!CurrentGlow(1));
    HandleStatusGlow(packet(6));globals.time-=.1f;check(!CurrentGlow(1));globals.time+=.1f;
    ++player_lives[1];check(!CurrentGlow(1));--player_lives[1];++player_births[1];check(!CurrentGlow(1));--player_births[1];
    ++registry.pair.serial;check(!CurrentGlow(1));--registry.pair.serial;++registry.epoch;check(!CurrentGlow(1));--registry.epoch;
    player.alive=false;check(!CurrentGlow(1));HandleStatusGlow(packet(7));player.alive=true;
    e->free=true;check(!CurrentGlow(1));HandleStatusGlow(packet(8));e->free=false;
    ++e->serialnumber;check(!CurrentGlow(1));HandleStatusGlow(packet(9));check(CurrentGlow(1));
    alive_hook=[&]{++player_lives[1];};check(!CurrentGlow(1));alive_hook={};--player_lives[1];
    ClearStatusGlow();alive_hook=[&]{++e->serialnumber;};HandleStatusGlow(packet(10));alive_hook={};check(!CurrentGlow(1));
    alive_hook=[&]{++link.gen;};HandleStatusGlow(packet(11));alive_hook={};check(!CurrentGlow(1));
    HandleStatusGlow(packet(12));check(CurrentGlow(1));++link.gen;check(!CurrentGlow(1));
    HandleStatusGlow(packet(1));check(CurrentGlow(1));link.capable=false;check(!CurrentGlow(1));
    rejects([&]{HandleStatusGlow(packet(2));});SendStatusGlow();check(!status_glow::decode_presentation(sent.back().bytes).state.flags);link.capable=true;
    ClearStatusGlow();check(!CurrentGlow(1));HandleStatusGlow(packet(1));
    // PVS bypass never marks an invisible entity as normally visible, and all
    // other production admission branches still execute.
    auto pack=[&](bool is_player=true,int hostflags=0){return AddToFullPack(nullptr,1,e,viewer,hostflags,is_player,nullptr);};
    check(pack());check(positive_marks==0&&negative_marks==1);check(!pack(false));
    viewer_caps[2]="0";check(!pack());viewer_caps[2]="1";
    client_ready[2]=false;check(!pack());client_ready[2]=true;
    viewer->v.flags=FL_FAKECLIENT;check(!pack());viewer->v.flags=0;
    for(int effects:{EF_NODRAW,EF_OWNER_VISIBILITY}){e->v.effects=effects;check(!pack());}e->v.effects=0;
    e->v.owner=viewer;e->v.effects=EF_OWNER_NO_VISIBILITY;check(!pack());e->v.effects=0;
    e->v.flags=FL_SKIPLOCALHOST;check(!pack(true,1));e->v.flags=FL_SPECTATOR;check(!pack());e->v.flags=0;e->v.owner=nullptr;
    e->v.modelindex=0;check(!pack());e->v.modelindex=1;
    viewer->v.groupinfo=1;e->v.groupinfo=2;check(!pack());e->v.groupinfo=1;check(pack());viewer->v.groupinfo=e->v.groupinfo=0;
    globals.time+=.36f;check(!pack());check(positive_marks==0);pvs=true;check(pack());check(positive_marks==1);
    pvs=false;check(pack());map_changed=true;check(!pack());map_changed=false;
    ClearStatusGlow(true);check(sent.size()>0&&glow_delivery_revision==0);check(!CurrentGlow(1));
    std::cout<<checks<<" native glow checks: actual authority/delivery/PVS decisions, strict wire, identity/expiry/reentry, reliable client receiver\n";
}
