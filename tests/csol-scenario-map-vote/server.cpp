// SPDX-License-Identifier: GPL-3.0-or-later
// Actual server adapter methods; engine identities, callbacks and transport
// are controlled. No actual clients, network channel or GameRules is claimed.
#include "../../native/csol/scenario/core/map_vote.hpp"
#include <algorithm>
#include <charconv>
#include <climits>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <string>
#include <vector>
using namespace csol::scenario::map_vote;
unsigned checks{},failures{},transportViolations{};
void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::cerr<<"FAIL "<<label<<'\n';}}
constexpr int FL_CLIENT=1,FL_FAKECLIENT=2,FL_PROXY=4,FL_KILLME=8,MSG_ONE=1;
struct edict_t;
struct CBaseEntity {virtual ~CBaseEntity()=default;static CBaseEntity* Instance(edict_t*);};
struct CBasePlayer:CBaseEntity {bool has_disconnected{};};
struct edict_t {bool free{};struct {int flags{};}v;int userid{},slot{};CBaseEntity* object{};};
CBaseEntity* CBaseEntity::Instance(edict_t* e){return e->object;}
std::array<edict_t,33> entities;
std::array<CBasePlayer,33> players;
struct Globals {float time{};} globals,*gpGlobals=&globals;
struct Engine {bool pfnGetPlayerUserId{true},pfnMessageBegin{true},pfnMessageEnd{true},pfnWriteByte{true},pfnRandomLong{true};}g_engfuncs;
std::vector<std::string> arguments;
std::uint32_t tieValue{};
struct Packet {int recipient{},id{};std::vector<std::byte> bytes;};
std::vector<Packet> packets;
bool packetOpen{};
std::function<void()> beginHook;
void Begin(int type,int id,const void*,edict_t* entity)
{
    if(type!=MSG_ONE||packetOpen)++transportViolations;packetOpen=true;
    packets.push_back({entity->slot,id,{}});if(beginHook){auto hook=std::move(beginHook);hook();}
}
void Write(int value){if(!packetOpen)++transportViolations;packets.back().bytes.push_back(std::byte(value));}
void End(){if(!packetOpen)++transportViolations;packetOpen=false;}
#define GETPLAYERUSERID(e) ((e)->userid)
#define INDEXENT(i) (&entities.at(i))
#define ENTINDEX(e) ((e)->slot)
#define RANDOM_LONG(a,b) (tieValue)
#define MESSAGE_BEGIN(t,id,p,e) Begin(t,id,p,e)
#define WRITE_BYTE(v) Write(v)
#define MESSAGE_END() End()
#define CMD_ARGC() int(arguments.size())
#define CMD_ARGV(i) arguments.at(i).c_str()
Vote vote;
std::uint32_t generation{1};
std::uint64_t epoch{1};
int message{100};
bool busy{},sending{},resourcesReady{true};
struct Recipient {std::uint32_t token{},revision{};std::uint16_t seconds{};double sentAt{-1};};
std::array<Recipient,MaxPlayers> recipients{};
#include "actual_server.inc"

const std::array choices{Choice{74,"zs_lostcity"},Choice{77,"zs_lastclue"},Choice{98,"zs_deadend"}};
void Reset(unsigned humans=1)
{
    CSOL_ResetScenarioMapVote();message=100;resourcesReady=true;globals.time=0;packets.clear();beginHook={};packetOpen=false;busy=sending=false;
    arguments={};tieValue=0;
    for(unsigned i=1;i<=32;++i){entities[i]={false,{i<=humans?FL_CLIENT:0},int(100+i),int(i),&players[i]};players[i].has_disconnected=false;}
    Check(vote.Begin(++generation,choices,"zs_trap",0,10),"test slate starts without changing production map allowlist");
}
Snapshot Last(unsigned slot)
{
    Snapshot value;
    for(auto it=packets.rbegin();it!=packets.rend();++it)if(it->recipient==int(slot)){
        Check(Decode(it->bytes,value),"actual sent packet decodes");return value;
    }
    Check(false,"expected recipient packet exists");return {};
}
void Command(unsigned slot,unsigned id,std::uint32_t nonce=0)
{
    arguments={CommandName,std::to_string(nonce?nonce:generation),std::to_string(id)};
    Check(CSOL_ScenarioMapVoteCommand(&entities[slot],CommandName),"registered command consumed");
}
void Network()
{
    Reset(32);Check(!CSOL_UpdateScenarioMapVote(),"initial send does not prematurely settle");
    Check(packets.size()==32&&Last(1).eligible==32,"actual traversal sends one snapshot to32 humans");
    const auto initial=packets.size();for(unsigned i=0;i<100;++i){Command(1,74);CSOL_UpdateScenarioMapVote();}
    Check(packets.size()==initial,"submission flood cannot bypass snapshot throttle");
    globals.time=.25f;CSOL_UpdateScenarioMapVote();
    Check(packets.size()==initial+32&&Last(1).selection==74&&Last(2).selection==0,"per-recipient selection and aggregate broadcast");
    auto snapshot=Last(1);Check(snapshot.choices[0].votes==1,"one connection receives one ballot under command flood");
    Command(1,77);globals.time=.5f;CSOL_UpdateScenarioMapVote();snapshot=Last(1);
    Check(snapshot.choices[0].votes==0&&snapshot.choices[1].votes==1,"actual command revote moves one count");
    Command(1,98,generation-1);globals.time=.75f;CSOL_UpdateScenarioMapVote();Check(Last(1).selection==77,"stale generation cannot change selection");
    entities[1].userid=999;globals.time=1;CSOL_UpdateScenarioMapVote();snapshot=Last(1);
    Check(snapshot.selection==0&&snapshot.choices[1].votes==0,"slot reuse revokes old ballot before next send");
    entities[2].v.flags=FL_CLIENT|FL_FAKECLIENT;entities[3].v.flags=FL_CLIENT|FL_PROXY;players[4].has_disconnected=true;
    globals.time=1.25f;CSOL_UpdateScenarioMapVote();Check(Last(1).eligible==29,"bots proxies and disconnected players cannot vote");
}
void Completion()
{
    Reset(2);CSOL_UpdateScenarioMapVote();Command(1,74);Command(2,77);
    globals.time=9.99f;Check(!CSOL_UpdateScenarioMapVote(),"deadline not shortened by complete electorate");
    tieValue=1;globals.time=10;const auto winner=CSOL_UpdateScenarioMapVote();
    Check(winner&&winner->id==77,"actual server finish selects tied winner from engine random value");
    Check(Last(1).phase==Phase::Finished&&Last(1).winner==77&&Last(1).seconds==0,"terminal packet is sent before returning winner");
    Check(!CSOL_UpdateScenarioMapVote(),"winner delivered to owning rules only once");
    Reset(2);CSOL_UpdateScenarioMapVote();Command(1,74);globals.time=.1f;CSOL_CancelScenarioMapVote();
    const auto before=packets.size();Check(!CSOL_UpdateScenarioMapVote(),"admin cancellation cannot produce destination");
    Check(packets.size()==before+2&&Last(1).phase==Phase::Cancelled&&Last(1).seconds==0,"cancel bypasses throttle and sends immediate terminal state");
    Reset(2);CSOL_UpdateScenarioMapVote();globals.time=10;
    beginHook=[](){CSOL_ResetScenarioMapVote();};
    const auto size=packets.size();Check(!CSOL_UpdateScenarioMapVote(),"map reset during terminal send revokes winner dispatch");
    Check(packets.size()==size+1&&!packetOpen&&vote.State()==Phase::Idle,"reset completes current packet without sending stale next recipient");
}
void InvalidCommands()
{
    Reset();CSOL_UpdateScenarioMapVote();const auto revision=vote.Revision();
    for(const auto& pair:std::array{std::pair{"-1","74"},std::pair{"1x","74"},std::pair{"999999999999","74"},std::pair{"1","65536"}}){
        arguments={CommandName,pair.first,pair.second};CSOL_ScenarioMapVoteCommand(&entities[1],CommandName);
        Check(vote.Revision()==revision,"malformed arguments cannot mutate vote");
    }
    Check(!CSOL_ScenarioMapVoteCommand(&entities[1],"buy"),"unrelated command retains native dispatcher");
    busy=true;Command(1,74);busy=false;Check(vote.Revision()==revision,"nested command rejected while sender owns state");
}
int main(){Network();Completion();InvalidCommands();Check(transportViolations==0,"all32 recipients and callback reset preserve message boundaries");std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;}
