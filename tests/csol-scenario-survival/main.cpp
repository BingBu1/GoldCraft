// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/scenario/trigger_round.hpp"
#include "../../native/csol/scenario/round_settings.hpp"
#include "../../native/csol/scenario/resources.hpp"
#include "../../native/csol/scenario/monster_types.hpp"
#include "../../native/csol/scenario/map_support.hpp"
#include "../../native/csol/scenario/automatic_respawn.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>

using namespace csol::scenario;
int checks{}, failures{};
void Check(bool value, const char* label)
{ ++checks; if (!value) { ++failures; std::cerr << "FAIL: " << label << '\n'; } }
std::vector<std::byte> Read(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("missing resource: " + path.string());
    const std::string data{std::istreambuf_iterator<char>(input), {}};
    return {reinterpret_cast<const std::byte*>(data.data()), reinterpret_cast<const std::byte*>(data.data()+data.size())};
}
struct Events final : TriggerWaveEvents
{
    bool allowed{true}; int summaries{}, goals{};
    std::vector<std::uint16_t> active;
    bool Check(ConfigError& error) const override { error = {}; return allowed; }
    void WaveActivated(std::uint16_t) override {}
    void BeforeSpatialWaveEnd(std::uint16_t) override {}
    void WavesChanged(std::span<const std::uint16_t> ids) override
    { ++summaries; active.assign(ids.begin(), ids.end()); }
    void GoalReached() override { ++goals; }
};
struct DispatchState { std::size_t calls{}; bool valid{true}; const MonsterTypeTable* types{}; };
void Dispatch(void* context, const WaveRow& row, RowClock&)
{
    auto& state = *static_cast<DispatchState*>(context); ++state.calls;
    if (!row.IsEvent())
        for (const auto id : row.types)
        {
            const auto* type = state.types->Find(id);
            state.valid &= type && type->skill < 1000 && type->appearance != "elite_forceC" && type->appearance != "elite_forceT";
        }
}
void Actual(const std::filesystem::path& root)
{
    WaveTable waves; RoundTable rounds; MonsterTypeTable types; ConfigError error;
    Check(ReadWaveResource(Read(root/"maps/zbs_wave_zs_lostcity.cso"),waves,error), "actual common waves decode");
    Check(ReadRoundResource(Read(root/"maps/zbs_zs_lostcity.cso"),rounds,error), "actual common rounds decode");
    Check(ReadMonsterTypeResource(Read(root/"maps/zbs_zt_zs_lostcity.cso"),types,error), "actual common types decode");
    Check(rounds.rows.size()==5 && waves.rows.size()==209 && types.rows.size()==44, "pinned common has five rounds and complete rows");
    std::int32_t seconds{};
    Check(ReadRoundTime(rounds,seconds,error)&&seconds==180,"actual positional survival time is180 seconds");
    Check(rounds.rows[0].waves==std::vector<int>{2,3,19,820,821},"first common round waves are ordinary IDs");
    TriggerRound round; SpawnPoints points; Events events;
    points.RegisterMonsterPoint({{}, {},1,1,1}, 3);
    for (std::size_t i=0;i<rounds.rows.size();++i)
    {
        points.ConsumePointQuota(1,1);
        const float previousThink=round.Schedule().nextThink;
        bool committed{};
        Check(round.BeginOrdinary(waves,rounds.rows[i],1000+float(i)*210,20,points,events,error,&committed)&&committed,"ordinary round commits");
        Check(!round.Group()&&round.Triggers().UsedTriggers().empty(),"ordinary round has no trigger group or used TRG program");
        std::vector<std::uint16_t> expected;
        for(auto id:rounds.rows[i].waves)expected.push_back(WaveKey(id));
        Check(events.active==expected,"all row wave IDs active in original order");
        Check(round.Schedule().nextThink==previousThink,"formal round preserves half-second deadline");
        DispatchState state{0,true,&types};
        for(int tick=0;tick<=360;++tick)
        {
            auto result=round.Waves().Tick(float(tick)*0.5f,Dispatch,&state);
            Check(result.valid,"actual ordinary controller dispatch remains valid");
        }
        Check(state.calls>0&&state.valid,"actual waves only request implemented ordinary behaviors");
        Check(events.goals==0,"wave completion cannot substitute for survival timeout");
    }
    auto duplicate=rounds.rows.front(); duplicate.waves={2,2,65538,-1};
    Check(round.BeginOrdinary(waves,duplicate,5000,20,points,events,error),"ordinary duplicate and absent wave IDs retained");
    Check(events.active==std::vector<std::uint16_t>{2,2,2,65535},"ordinary IDs use exact low16 key bits");
    const auto old=events.active;const auto start=round.Schedule().start;const int notifications=events.summaries;
    bool committed=true;
    Check(!round.BeginOrdinary(waves,duplicate,std::numeric_limits<float>::infinity(),20,points,events,error,&committed)&&!committed,"invalid origin fails before commit");
    Check(events.active==old&&events.summaries==notifications&&round.Schedule().start==start,"invalid origin leaves prior round and callbacks intact");
    events.allowed=false;
    Check(!round.BeginOrdinary(waves,duplicate,6000,20,points,events,error),"retired callback owner cannot start round");
    Check(!respawn::TimedSubtype(0)&&respawn::TimedSubtype(1),"original type0 waits for next round; type1 retains timed revival");
    Check(HasMapGameplay("zs_lostcity",0)&&HasMapGameplay("zs_deadend",1),"implemented manual map identities");
    Check(!HasMapGameplay("zs_lostcity",1)&&!HasMapGameplay("zs_trap",0),"wrong subtype and unimplemented map rejected");
    auto invalid=rounds;invalid.source.records[1].fields[1]="2147483648";seconds=180;
    Check(!ReadRoundTime(invalid,seconds,error)&&seconds==180,"overflowing duration cannot replace old metadata");
    invalid.source.records[1].fields[1].clear();
    Check(!ReadRoundTime(invalid,seconds,error),"missing duration rejected");
}
int main(int argc,char** argv)
{
    try { if(argc!=2)throw std::runtime_error("usage: survival_tests common-root"); Actual(argv[1]); }
    catch(const std::exception& error){++failures;std::cerr<<error.what()<<'\n';}
    std::cout<<"survival core: "<<checks<<" checks, "<<failures<<" failures\n";
    return failures?1:0;
}
