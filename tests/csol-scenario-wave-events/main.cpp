// SPDX-License-Identifier: GPL-3.0-or-later
// Tests actual wave scheduling and message encoding. The monster query and
// network sink are controlled interfaces, not claims about live engine wiring.
#include "wave_events.hpp"
#include "waves.hpp"
#include "../config_codec.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

using namespace csol::scenario;
namespace
{
int checks{}, failures{};
void Check(bool value, const char* label)
{ ++checks; if (!value) { ++failures; std::cerr << "FAIL: " << label << '\n'; } }
using Message = std::array<std::uint8_t, 4>;
struct Monster { int type, solid, health, deadflag; };
struct Capture final : WaveEventActions
{
    std::vector<Monster> monsters;
    std::vector<int> queries;
    std::vector<Message> messages;
    bool HasSolidMonster(int type) override
    {
        queries.push_back(type);
        return std::any_of(monsters.begin(), monsters.end(), [type](const auto& m)
        { return m.type == type && m.solid != 0; });
    }
    void BroadcastEvent(std::span<const std::uint8_t, 4> bytes) override
    { Message value{}; std::copy(bytes.begin(), bytes.end(), value.begin()); messages.push_back(value); }
};
WaveRow Event(int type = 9999, std::string message = "193", int interval = 0)
{
    WaveRow row; row.waveId = 7; row.spawnPoint = 9999; row.startTime = 0;
    row.interval = static_cast<std::int16_t>(interval); row.types = {type};
    row.weights = {1}; row.weightTotal = 1; row.eventMessage = std::move(message); return row;
}
void Numbers()
{
    struct Case { std::string text; std::uint8_t low, high; };
    const std::vector<Case> cases{{"193",193,0},{"43",43,0},{"204",204,0},
        {"32767",255,127},{"32768",0,128},{"65535",255,255},{"65536",0,0},
        {"65537",1,0},{"-1",255,255},{"-32768",0,128},{"-65537",255,255},
        {"2147483647",255,255},{"-2147483648",0,0},{"",0,0},{" \t\r\n\v\f",0,0},
        {" \t+65537tail",1,0},{"-2 trailing",254,255},{"193.9",193,0},{"0xC1",0,0},
        {"text193",0,0},{"+",0,0},{"-",0,0},{"+-1",0,0},{"--1",0,0},{"+ 1",0,0},
        {"  -000193suffix",63,255},{std::string("193\0ignored",11),193,0},
        {"000000000000000000000000000193",193,0}};
    for (const auto& item : cases)
    {
        Capture sink; ConfigError error{99,"stale"};
        Check(DispatchWaveEvent(Event(9999,item.text),sink,error) == WaveEventResult::Sent,
            "CSV atoi prefix / low16 number accepted");
        Check(error.message.empty() && error.byteOffset == 0 && sink.queries.empty(),
            "unconditional event clears prior diagnostics and skips registry query");
        Check(sink.messages == std::vector<Message>{{0x1b,7,item.low,item.high}},
            "exact four-byte little-endian message, including unsigned high-bit IDs");
    }
    for (const auto* bad : {"2147483648","-2147483649","99999999999999999999999","+2147483648text"})
    {
        Capture sink; ConfigError error;
        Check(DispatchWaveEvent(Event(40,bad),sink,error) == WaveEventResult::Invalid && !error.message.empty(),
            "unestablished int32 overflow rejected as local input policy");
        Check(sink.messages.empty() && sink.queries.empty(), "invalid number has no query or network side effect");
    }
    Capture sink; ConfigError error; auto row = Event(); row.spawnPoint=8;
    Check(DispatchWaveEvent(row,sink,error) == WaveEventResult::Invalid, "ordinary spawn row is not a hint event");
    row=Event(); row.types.clear();
    Check(DispatchWaveEvent(row,sink,error) == WaveEventResult::Invalid && sink.messages.empty(), "event needs first type");
    const std::string header="waveID,spawnPoint,startTime,endTime,intervalTime,zombi,zombiRatio,eventmessage\n";
    for (const std::string suffix : {"",","})
    {
        WaveTable table;
        Check(ReadWaves(header+"7,9999,0,0,0,9999,1"+suffix+"\n",table,error), "real CSV parser accepts missing or empty last cell");
        Capture parsed;
        Check(table.rows.size()==1 && DispatchWaveEvent(table.rows[0],parsed,error)==WaveEventResult::Sent &&
            parsed.messages==std::vector<Message>{{0x1b,7,0,0}}, "missing or empty cell sends ID zero through production parser");
    }
}
void Gating()
{
    ConfigError error;
    for (const int type : {9999,30000})
    {
        Capture sink; auto row=Event(type); row.types.push_back(40); row.weights={0,127}; row.weightTotal=127;
        Check(DispatchWaveEvent(row,sink,error)==WaveEventResult::Sent && sink.queries.empty() && sink.messages.size()==1,
            "first sentinel bypasses query regardless of other types or weights");
    }
    auto row=Event(40); row.types.push_back(9999); row.types.push_back(30000); row.weights={0,1,127}; row.weightTotal=-128;
    Capture empty;
    Check(DispatchWaveEvent(row,empty,error)==WaveEventResult::Suppressed && empty.queries==std::vector<int>{40} && empty.messages.empty(),
        "only first type gates; later sentinels and bad weights do not permit dispatch");
    Capture other; other.monsters={{41,1,100,0},{40,0,100,0}};
    Check(DispatchWaveEvent(row,other,error)==WaveEventResult::Suppressed && other.queries==std::vector<int>{40},
        "controlled registry returns false for mismatching type or nonsolid match");
    for (const Monster monster : {Monster{40,1,100,0},{40,2,0,2},{40,-1,-100,3}})
    {
        Capture sink; sink.monsters={{40,0,100,0},monster,monster};
        Check(DispatchWaveEvent(row,sink,error)==WaveEventResult::Sent && sink.queries==std::vector<int>{40} && sink.messages.size()==1,
            "one true type-query result sends once; controlled registry uses nonzero solid without health/deadflag strengthening");
    }
}
struct Run
{
    Capture sink;
    std::vector<WaveEventResult> results;
    std::vector<RowClock> clockAtCallback;
    std::vector<std::size_t> records;
    std::vector<std::string> errors;
    std::size_t ordinary{};
};
void Dispatch(void* context, const WaveRow& row, RowClock& clock)
{
    auto& run=*static_cast<Run*>(context);
    if (!row.IsEvent()) { ++run.ordinary; return; }
    run.clockAtCallback.push_back(clock); run.records.push_back(row.recordIndex);
    ConfigError error;
    run.results.push_back(DispatchWaveEvent(row,run.sink,error)); run.errors.push_back(error.message);
}
void Schedule()
{
    ConfigError error; WaveRunner runner; WaveTable table; Run run;
    table.rows={Event(40)};
    Check(runner.Load(table,7,error) && runner.Clocks()[0].next==2, "conditional event receives original load plus-two clock");
    auto tick=runner.Tick(1,Dispatch,&run);
    Check(tick.valid && !tick.finished && tick.attempts==0, "conditional event waits for delayed start");
    tick=runner.Tick(2,Dispatch,&run);
    Check(tick.valid && tick.finished && tick.attempts==1 && run.results==std::vector{WaveEventResult::Suppressed}, "one-shot conditional event is consumed when no registered match exists");
    Check(run.clockAtCallback[0].next==-1 && runner.Clocks()[0].next==-1 && run.sink.messages.empty(), "clock ended before gate query and stays ended after suppression");
    run.sink.monsters={{40,1,100,0}}; tick=runner.Tick(3,Dispatch,&run);
    Check(tick.valid && tick.finished && tick.attempts==0 && run.results.size()==1 && run.sink.messages.empty(), "later monster does not retry consumed one-shot hint");
    table.rows={Event(40,"197",2)}; table.rows[0].endTime=6; run={};
    Check(runner.Load(table,7,error), "periodic conditional event loaded");
    tick=runner.Tick(2,Dispatch,&run);
    Check(tick.valid && !tick.finished && run.results.back()==WaveEventResult::Suppressed && run.clockAtCallback.back().next==4, "suppressed periodic event keeps advanced clock");
    run.sink.monsters={{40,1,100,0}}; tick=runner.Tick(3,Dispatch,&run);
    Check(tick.attempts==0 && run.sink.messages.empty(), "periodic rejection does not become per-frame retry");
    tick=runner.Tick(4,Dispatch,&run);
    Check(tick.valid && !tick.finished && run.results.back()==WaveEventResult::Sent && run.clockAtCallback.back().next==6 && run.sink.messages.size()==1, "next scheduled period can send after registry changes");
    run.sink.monsters.clear(); tick=runner.Tick(6,Dispatch,&run);
    Check(tick.valid && tick.finished && run.results.back()==WaveEventResult::Suppressed && run.clockAtCallback.back().next==8 && runner.Clocks()[0].next==-1, "end-time check follows consumed suppression without extra message");
    table.rows={Event(30000)}; run={};
    Check(runner.Load(table,7,error) && runner.Clocks()[0].next==2, "30000 bypasses gate but retains non9999 load delay");
    tick=runner.Tick(2,Dispatch,&run);
    Check(tick.finished && run.sink.messages.size()==1 && run.sink.queries.empty(), "30000 event emits at delayed time without registered monster");
    table.rows={Event(9999)}; run={};
    Check(runner.Load(table,7,error) && runner.Clocks()[0].next==0, "9999 sentinel begins without load delay");
    tick=runner.Tick(0,Dispatch,&run);
    Check(tick.finished && run.clockAtCallback[0].next==-1 && run.sink.messages.size()==1, "unconditional event consumes before four-byte dispatch");
    table.rows={Event(40,"2147483648")}; run={};
    Check(runner.Load(table,7,error), "invalid payload schedule accepted independently of event decoding");
    tick=runner.Tick(2,Dispatch,&run);
    Check(tick.valid && tick.finished && run.results==std::vector{WaveEventResult::Invalid} && !run.errors[0].empty() && run.sink.queries.empty(), "invalid local message is reported while consumed schedule is not rolled back");
    table.rows={Event(40)}; run={};
    Check(runner.Load(table,7,error) && runner.RestartAt(10), "real reactivation restores original unshifted event time");
    tick=runner.Tick(10,Dispatch,&run);
    Check(tick.finished && run.results==std::vector{WaveEventResult::Suppressed} && run.clockAtCallback[0].next==-1, "reactivated one-shot retains consumption before gate");
    table.rows={Event()}; run={};
    Check(runner.Load(table,7,error), "missed window fixture loaded"); tick=runner.Tick(2,Dispatch,&run);
    Check(tick.finished && tick.attempts==0 && run.sink.messages.empty(), "late one-shot remains skipped by real wave scheduler");
}
std::string Read(const char* path)
{
    std::ifstream file(path,std::ios::binary); if (!file) throw std::runtime_error("input open failed");
    return {std::istreambuf_iterator<char>(file),{}};
}
void Actual(const char* encoded, const char* expectedPath)
{
    auto raw=Read(encoded); std::vector<std::byte> bytes;
    for (const auto c:raw) bytes.push_back(std::byte(static_cast<unsigned char>(c)));
    Check(csol::DecodeConfiguration(bytes), "actual CSO resource decrypted using production codec");
    while (!bytes.empty() && bytes.back()==std::byte{}) bytes.pop_back();
    const std::string decoded(reinterpret_cast<const char*>(bytes.data()),bytes.size());
    WaveTable table; ConfigError error;
    Check(ReadWaves(decoded,table,error) && table.rows.size()==288, "actual common wave parsed with all 288 rows");
    struct Expected { std::size_t record; int wave,start,end,interval,type,message; };
    std::vector<Expected> expected; std::istringstream input(Read(expectedPath)); Expected item{};
    while (input>>item.record>>item.wave>>item.start>>item.end>>item.interval>>item.type>>item.message) expected.push_back(item);
    Check(expected.size()==20, "independent Python CSV oracle contains 20 real event rows");
    const auto count=std::count_if(table.rows.begin(),table.rows.end(),[](const auto& row){return row.IsEvent();});
    Check(count==20, "production parser retains all 20 event rows");
    std::map<int,std::vector<Expected>> groups; std::set<int> ids;
    for (const auto& e:expected)
    {
        const auto row=std::find_if(table.rows.begin(),table.rows.end(),[&](const auto& r){return r.recordIndex==e.record;});
        Check(row!=table.rows.end(), "oracle source record exists in actual parsed table");
        if (row==table.rows.end()) continue;
        Check(row->IsEvent() && row->waveId==e.wave && row->startTime==e.start && row->endTime==e.end && row->interval==e.interval && row->types==std::vector<int>{e.type}, "event provenance and schedule match independent CSV fields");
        Check(row->eventMessage==std::to_string(e.message), "actual event identifier matches independent CSV");
        groups[e.wave].push_back(e); ids.insert(e.message);
    }
    Check(ids==std::set<int>{43,193,194,195,196,197,199,200,201,202,203,204}, "actual deadend contains exactly 12 distinct event IDs");
    std::size_t sent=0;
    for (const auto& [wave,rows]:groups)
    {
        WaveRunner runner; Run run;
        Check(runner.Load(table,wave,error), "full actual wave loaded including ordinary monster rows");
        const int last=std::max_element(rows.begin(),rows.end(),[](const auto&a,const auto&b){return a.start<b.start;})->start;
        for (int t=0;t<=last+2;++t) Check(runner.Tick(static_cast<float>(t),Dispatch,&run).valid, "actual wave advances at each whole second through last message");
        Check(run.records.size()==rows.size() && run.sink.messages.size()==rows.size(), "each actual event sends exactly once in full wave");
        Check(run.sink.queries.empty(), "all actual deadend events bypass monster query by first type9999");
        for (std::size_t n=0;n<rows.size() && n<run.records.size();++n)
        {
            Check(run.records[n]==rows[n].record && run.results[n]==WaveEventResult::Sent && run.errors[n].empty(), "source event order retained through real runner dispatch");
            Check(run.clockAtCallback[n].next==-1 && run.sink.messages[n]==Message{0x1b,7,static_cast<std::uint8_t>(rows[n].message),0}, "actual one-shot ended before exact four-byte message capture");
        }
        sent+=run.sink.messages.size();
    }
    Check(sent==20 && groups.size()==7, "all 20 events across seven real waves completed");
    std::cout<<"actual_event_rows="<<sent<<" unique_ids="<<ids.size()<<" waves="<<groups.size()<<'\n';
}
}
int main(int argc,char** argv)
{
    try
    {
        if (argc==1) {Numbers();Gating();Schedule();}
        else if (argc==4 && std::string_view(argv[1])=="--actual") Actual(argv[2],argv[3]);
        else throw std::runtime_error("usage: wave_event_tests [--actual encryptedWave independentExpectedTSV]");
    }
    catch(const std::exception& error) { ++failures; std::cerr<<"EXCEPTION: "<<error.what()<<'\n'; }
    std::cout<<"wave_events checks="<<checks<<" failures="<<failures<<'\n';
    return failures ? 1:0;
}
