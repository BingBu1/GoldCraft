// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/scenario/map_vote.hpp"
#include <array>
#include <iostream>
#include <limits>
#include <string_view>

using namespace csol::scenario::map_vote;
namespace {
unsigned checks{},failures{};
void Check(bool value,std::string_view label)
{
    ++checks;
    if (!value) { ++failures;std::cerr << "FAIL " << label << '\n'; }
}
const std::array choices{Choice{74,"zs_lostcity"},Choice{77,"zs_lastclue"},Choice{98,"zs_deadend"}};
void Lifecycle()
{
    Vote vote;
    Check(!vote.Connect(1,101),"registration requires an open ballot");
    Check(vote.Begin(5,choices,"zs_trap",100,20),"begin validated candidates");
    for (unsigned slot=1;slot<=32;++slot)
    {
        Check(vote.Connect(slot,100+slot),"all32 slots registered");
        Check(vote.Cast(slot,100+slot,5,74,101)==Ballot::Accepted,"all32 ballots accepted");
    }
    auto full=vote.View(1,101,101);
    Check(full.eligible==32&&full.choices[0].votes==32&&full.selection==74,"32 voters counted exactly once");
    Check(vote.Cast(1,101,5,74,102)==Ballot::Unchanged&&vote.Revision()==full.revision,"duplicate ballot preserves revision");
    Check(vote.Cast(1,101,5,77,102)==Ballot::Accepted,"revote permitted");
    auto changed=vote.View(1,101,102);
    Check(changed.choices[0].votes==31&&changed.choices[1].votes==1,"revote moves exactly one ballot");
    Check(!vote.Disconnect(1,999)&&vote.View(1,101,102)==changed,"stale departure cannot revoke current connection");
    Check(vote.Connect(1,201),"slot reused by new userid");
    Check(vote.View(1,201,102).selection==0&&vote.View(1,201,102).choices[1].votes==0,"new userid does not inherit vote");
    Check(vote.Cast(1,101,5,77,102)==Ballot::Ineligible,"old userid rejected");
    Check(vote.Cast(1,201,4,77,102)==Ballot::Stale,"old generation rejected");
    Check(vote.Disconnect(2,102),"current disconnect removes ballot");
    auto departed=vote.View(2,102,103);
    Check(departed.eligible==31&&departed.choices[0].votes==30&&departed.selection==0,"departure adjusts eligible count and votes");
    Check(vote.Connect(2,302)&&vote.Cast(2,302,5,98,119.99)==Ballot::Accepted,"late connection can vote before deadline");
    Check(!vote.Finish(119.999,0),"full electorate does not shorten fixed deadline");
    Check(vote.Cast(2,302,5,74,120)==Ballot::NotOpen,"deadline is exclusive for ballots");
    const auto winner=vote.Finish(120,1);
    Check(winner&&winner->id==74,"plurality wins at deadline");
    auto terminal=vote.View(2,302,120);
    Check(terminal.winner==74&&terminal.phase==Phase::Finished&&terminal.seconds==0,"finished snapshot");
    Check(!vote.Disconnect(2,302)&&!vote.Connect(3,999)&&vote.View(2,302,121)==terminal,"terminal tally remains immutable");
    Check(!vote.Finish(121,0),"finish cannot be dispatched twice");
    vote.Cancel();Check(vote.View(2,302,121)==terminal,"cancel cannot rewrite committed result");
    Check(!vote.Begin(5,choices,"zs_trap",130,20),"generation cannot be reused");
    Check(vote.Begin(6,choices,"zs_trap",130,20),"fresh generation permitted");
    Check(vote.View(2,302,130).eligible==0,"fresh vote requires current connection registration");
    vote.Cancel();Check(vote.State()==Phase::Cancelled,"explicit cancellation");
    Check(vote.Cast(2,302,6,74,131)==Ballot::NotOpen,"cancelled vote rejects submissions");
}
void Validation()
{
    Vote vote;
    for (const auto name:{"", "../zs_deadend", "ZS_DEADEND", "zs_deadend;quit", "a/b", "zs_deadend.bsp"})
        Check(!ValidMapName(name),"unsafe/noncanonical map name rejected");
    Check(ValidMapName("zs_deadend")&&!ValidMapName(std::string(32,'a')),"bounded map name");
    for (unsigned seconds:{0u,9u,121u,UINT32_MAX})
        Check(!vote.Begin(1,choices,"zs_trap",0,seconds),"bounded duration");
    for (double now:{-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
        Check(!vote.Begin(1,choices,"zs_trap",now,20),"finite nonnegative clock required");
    Check(!vote.Begin(0,choices,"zs_trap",0,20),"nonzero generation required");
    Check(!vote.Begin(1,{},"zs_trap",0,20),"empty slate rejected");
    Check(!vote.Begin(1,choices,"../zs_deadend",0,20),"invalid current-map identity rejected");
    auto invalid=choices;invalid[0].id=0;
    Check(!vote.Begin(1,invalid,"zs_trap",0,20),"zero candidate id rejected");
    invalid=choices;invalid[0].id=invalid[1].id;
    Check(!vote.Begin(1,invalid,"zs_trap",0,20),"duplicate candidate id rejected");
    invalid=choices;invalid[0].mapName=invalid[1].mapName;
    Check(!vote.Begin(1,invalid,"zs_trap",0,20),"duplicate destination rejected");
    Check(vote.Begin(1,choices,"zs_trap",0,10),"valid begin after rejected proposals");
    Check(!vote.Begin(2,choices,"zs_trap",0,10),"active vote cannot be replaced");
    for (unsigned slot:{0u,33u,UINT32_MAX}) Check(!vote.Connect(slot,1),"invalid slot rejected");
    Check(!vote.Connect(1,0)&&vote.Connect(1,1),"connection userid required");
    Check(vote.Cast(1,1,1,100,0)==Ballot::InvalidChoice,"unlisted destination rejected");
    Check(vote.View(1,1,9.01).seconds==1&&vote.View(1,1,10).seconds==0,"countdown rounds up before deadline");
    auto none=vote.Finish(10,2);
    Check(none&&none->id==98,"no ballots use supplied random tie choice");
    Check(vote.Begin(2,choices,"zs_trap",20,10),"second vote after completion");
    vote.Connect(1,1);vote.Connect(2,2);
    vote.Cast(1,1,2,74,20);vote.Cast(2,2,2,77,20);
    auto tied=vote.Finish(30,1);
    Check(tied&&tied->id==77,"random choice restricted to tied maximum");
}
void Wire()
{
    Vote vote;vote.Begin(1,choices,"zs_trap",0,20);vote.Connect(1,101);vote.Cast(1,101,1,74,0);
    auto value=vote.View(1,101,0);const auto encoded=Encode(value);
    Check(encoded.size()==27,"three-choice message has bounded27-byte wire size");
    Snapshot decoded;
    Check(Decode(encoded,decoded)&&decoded==value,"round-trip includes recipient ballot");
    for (std::size_t n=0;n<encoded.size();++n)
    {
        auto destination=value;
        Check(!Decode({encoded.data(),n},destination)&&destination==value,"truncated message cannot mutate prior snapshot");
    }
    auto reject=[&](std::vector<std::byte> bytes,const char* label){
        auto previous=value;Check(!Decode(bytes,previous)&&previous==value,label);
    };
    auto bytes=encoded;bytes.push_back(std::byte{0});reject(bytes,"trailing bytes rejected");
    bytes=encoded;bytes[0]=std::byte{2};reject(bytes,"unknown wire version rejected");
    bytes=encoded;bytes[1]=std::byte{7};reject(bytes,"unknown phase rejected");
    bytes=encoded;bytes[2]=std::byte{0};reject(bytes,"zero generation rejected");
    bytes=encoded;bytes[6]=std::byte{0};reject(bytes,"zero revision rejected");
    bytes=encoded;bytes[16]=std::byte{0};reject(bytes,"votes exceeding electorate rejected");
    bytes=encoded;bytes[21]=bytes[18];bytes[22]=bytes[19];reject(bytes,"duplicate wire candidate rejected");
    bytes=encoded;bytes[12]=std::byte{77};reject(bytes,"recipient ballot needs corresponding count");
    vote.Finish(20,0);value=vote.View(1,101,20);
    Check(Decode(Encode(value),decoded)&&decoded==value,"finished result round-trip");
    auto wrong=value;wrong.winner=77;Check(Encode(wrong).empty(),"nonplurality winner rejected");
    wrong=value;wrong.seconds=1;Check(Encode(wrong).empty(),"finished clock must be zero");
}
void ClientCountdown()
{
    Vote vote;vote.Begin(1,choices,"zs_trap",0,20);
    auto packet=vote.View(0,0,0);Countdown clock;
    Check(clock.Observe(packet,10)&&clock.Remaining(10)==20,"initial relative deadline");
    Check(clock.Observe(packet,12)&&clock.Remaining(12)==18,"duplicate does not extend countdown");
    packet.seconds=19;
    Check(clock.Observe(packet,12.5)&&clock.Remaining(12.5)==18,"delayed same-revision seconds packet accepted without extension");
    packet.seconds=15;
    Check(clock.Observe(packet,13)&&clock.Remaining(13)==15,"same-revision clock may shorten to valid server countdown");
    vote.Connect(1,1);vote.Cast(1,1,1,74,3);packet=vote.View(1,1,3);
    Check(clock.Observe(packet,14)&&clock.Remaining(14)==14,"new tally revision does not extend fixed deadline");
    packet.revision=1;packet.seconds=1;
    Check(!clock.Observe(packet,15)&&clock.Remaining(15)==13,"old revision cannot change active countdown");
    Check(clock.Remaining(28)==0,"local expiry closes even if terminal network message is delayed");
    vote.Finish(20,0);packet=vote.View(1,1,20);
    Check(clock.Observe(packet,29)&&clock.Remaining(29)==0,"server terminal state closes clock");
    vote.Begin(2,choices,"zs_trap",30,10);packet=vote.View(0,0,30);
    Check(clock.Observe(packet,30)&&clock.Remaining(30)==10,"new generation gets independent deadline");
    Check(!clock.Observe(packet,29)&&clock.Remaining(31)==0,"client time rollback fails closed");
}
void CurrentMap()
{
    Vote vote;
    const bool opened=vote.Begin(1,choices,"zs_deadend",0,20);
    Check(opened,"validated current map participates alongside other destinations");
    if(!opened)return;
    Check(vote.Connect(1,101)&&vote.Connect(2,102),"current-map electorate registers");
    Check(vote.Cast(1,101,1,98,1)==Ballot::Accepted&&
        vote.Cast(2,102,1,74,1)==Ballot::Accepted,"current and alternative maps both receive votes");
    Check(vote.Cast(2,102,1,98,2)==Ballot::Accepted,"revote can select the current map");
    const auto snapshot=vote.View(1,101,2);
    Check(snapshot.selection==98&&snapshot.choices[2].votes==2&&snapshot.choices[0].votes==0,
        "current-map votes retain ordinary accounting");
    const auto winner=vote.Finish(20,0);
    Check(winner&&winner->id==98&&winner->mapName=="zs_deadend","current-map plurality wins");
    Check(!vote.Finish(21,0),"current-map result dispatches only once");
    const std::array one{Choice{98,"zs_deadend"}};
    Check(vote.Begin(2,one,"zs_deadend",30,20),"current map can be the only validated destination");
    Check(vote.Connect(1,101),"new round registers the continuing connection");
    Check(vote.Cast(1,101,1,98,31)==Ballot::Stale,"previous generation cannot vote in a same-map ballot");
    Check(vote.Cast(1,101,2,98,31)==Ballot::Accepted,"new generation can vote for its only map");
    const auto single=vote.Finish(50,99);
    Check(single&&single->id==98,"single current-map winner ignores tie random magnitude");
    const std::array duplicateName{Choice{98,"zs_deadend"},Choice{99,"zs_deadend"}};
    const std::array duplicateId{Choice{98,"zs_deadend"},Choice{98,"zs_lostcity"}};
    const std::array duplicateEntry{Choice{98,"zs_deadend"},Choice{98,"zs_deadend"}};
    Check(!vote.Begin(3,duplicateName,"zs_deadend",60,20),"repeated current map under different IDs is rejected");
    Check(!vote.Begin(3,duplicateId,"zs_deadend",60,20),"repeated current map ID under different names is rejected");
    Check(!vote.Begin(3,duplicateEntry,"zs_deadend",60,20),"repeated identical current-map entry is rejected");
    Check(vote.Begin(3,one,"zs_deadend",60,20),"rejected duplicate lists do not consume next generation");
    const auto zero=vote.Finish(80,UINT32_MAX);
    Check(zero&&zero->id==98,"zero-vote single current map still settles at deadline");
}
}
int main(){Lifecycle();Validation();Wire();ClientCountdown();CurrentMap();std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;}
