// SPDX-License-Identifier: GPL-3.0-or-later
#include "controller.hpp"
#include <iostream>
#include <string>
#include <vector>

using namespace csol::scenario;
namespace
{
int checks{}, failures{};
void Check(bool value, const char* label)
{
    ++checks;
    if (!value) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
}
void Capture(void* state, const WaveRow& row, RowClock&)
{
    static_cast<std::vector<int>*>(state)->push_back(row.waveId);
}
}
int main()
{
    const std::string bytes = "waveID,spawnPoint,startTime,endTime,intervalTime,zombi,zombiRatio,eventmessage\n"
        "-1,0,0,0,5,40,1,\n65535,1,0,0,5,41,1,\n131071,1,0,0,5,42,1,\n65536,2,0,0,5,43,1,\n";
    WaveTable waves; RoundTable rounds; ConfigError error;
    Check(ReadWaves(bytes, waves, error), "signed and oversized wave IDs parse");
    Check(ReadRounds("round,waveTypes\n1,65535 -1 131071 0 65536 99\n", rounds, error), "round aliases parse without discarding source values");
    Check(waves.source.sourceBytes == bytes && waves.rows[0].waveId == -1 && waves.rows[2].waveId == 131071, "wave original full IDs and bytes retained");
    Check(waves.groups.size() == 2 && waves.groups.contains(65535) && waves.groups.contains(0), "parsed group keys use low sixteen bits");
    Check(waves.groups.at(65535).at(1) == std::vector<std::size_t>{1, 2}, "alias duplicate point rows retain original indices");
    SpawnPoints points;
    for (int id : {0, 2}) { SpawnPoint p; p.id = id; points.RegisterMonsterPoint(p, 0); }
    auto missing = FindMissingReferences(rounds, waves, points, 0);
    Check(missing.size() == 7, "canonical diagnostics finds aliased rows and one genuinely absent wave");
    if (missing.size() == 7)
    {
        const int refs[]{65535, 65535, -1, -1, 131071, 131071};
        for (std::size_t i = 0; i < 6; ++i)
            Check(missing[i].kind == MissingReferenceKind::MonsterPoint && missing[i].wave == refs[i] && missing[i].waveRecord == 2 + (i % 2), "diagnostic retains full reference ID and individual source row");
        Check(missing[6].kind == MissingReferenceKind::Wave && missing[6].wave == 99, "only actual missing canonical key diagnosed as wave");
    }
    SpawnPoint p; p.id = 1; points.RegisterMonsterPoint(p, 0);
    missing = FindMissingReferences(rounds, waves, points, 0);
    Check(missing.size() == 1 && missing[0].wave == 99, "resolved aliased point clears all repeated uses");
    for (int id : {-1, 65535, 131071})
    {
        WaveRunner runner;
        Check(runner.Load(waves, id, error) && runner.WaveId() == 65535 && runner.Rows().size() == 3, "direct runner uses same canonical key as parser and diagnostics");
        std::vector<int> capture; auto tick = runner.Tick(0, Capture, &capture);
        Check(tick.valid && tick.attempts == 3 && capture == std::vector<int>{-1, 65535, 131071}, "direct runner callbacks retain source IDs");
    }
    WaveController controller; const std::vector<std::uint16_t> ids{65535, 0};
    Check(controller.Activate(waves, ids, error) && controller.MissingWaves().empty(), "controller resolves same canonical keys");
    Check(controller.FindWave(65535)->Rows()[0].waveId == -1 && controller.FindWave(65535)->Rows()[2].waveId == 131071, "controller retains original row identity");
    std::vector<int> capture; const auto tick = controller.Tick(0, Capture, &capture);
    Check(tick.valid && tick.attempts == 4 && capture == std::vector<int>{-1, 65535, 131071, 65536}, "controller callback identity agrees with direct runner");
    std::cout << "canonical checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
