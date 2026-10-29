// SPDX-License-Identifier: GPL-3.0-or-later
// A count-independent inspection boundary for independently decoded resources.
// Emits the linked production consumers' data for an external Python CSV oracle.
#include "controller.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

using namespace csol::scenario;

namespace
{
std::string Read(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot open input");
    return {std::istreambuf_iterator<char>(file), {}};
}

void Hex(std::string_view bytes)
{
    constexpr char digits[] = "0123456789abcdef";
    std::cout << '"';
    for (unsigned char c : bytes) std::cout << digits[c >> 4] << digits[c & 15];
    std::cout << '"';
}
template<class Values> void Numbers(const Values& values)
{
    std::cout << '['; bool first = true;
    for (auto value : values)
    {
        if (!first) std::cout << ',';
        first = false; std::cout << +value;
    }
    std::cout << ']';
}
void Document(const CsvDocument& document)
{
    std::cout << "{\"sourceHex\":"; Hex(document.sourceBytes);
    std::cout << ",\"records\":["; bool first = true;
    for (const auto& record : document.records)
    {
        if (!first) std::cout << ','; first = false;
        std::cout << "{\"offset\":" << record.byteOffset << ",\"fieldsHex\":[";
        bool firstField = true;
        for (const auto& field : record.fields)
        {
            if (!firstField) std::cout << ','; firstField = false; Hex(field);
        }
        std::cout << "]}";
    }
    std::cout << "]}";
}
void Clock(const RowClock& clock)
{
    std::cout << '[' << clock.next << ',' << clock.end << ',' << clock.interval << ']';
}
struct Attempt { std::size_t record; int wave, point; RowClock clock; };
void Capture(void* context, const WaveRow& row, RowClock& clock)
{
    static_cast<std::vector<Attempt>*>(context)->push_back({row.recordIndex, row.waveId, row.spawnPoint, clock});
}
}

int main(int argc, char** argv)
{
    try
    {
        if (argc != 4) throw std::runtime_error("usage: consumer_inspect wave-decoded round-decoded point-list");
        ConfigError error; WaveTable waves; RoundTable rounds; SpawnPoints points;
        if (!ReadWaves(Read(argv[1]), waves, error))
            throw std::runtime_error("ReadWaves: " + std::to_string(error.byteOffset) + " " + error.message);
        if (!ReadRounds(Read(argv[2]), rounds, error))
            throw std::runtime_error("ReadRounds: " + std::to_string(error.byteOffset) + " " + error.message);
        std::ifstream pointInput(argv[3]);
        if (!pointInput) throw std::runtime_error("cannot open point input");
        SpawnPoint point; int maximum;
        while (pointInput >> point.team >> point.id >> point.entityIndex >> maximum)
            points.RegisterMonsterPoint(point, maximum);
        if (!pointInput.eof()) throw std::runtime_error("malformed point input");

        std::cout << "{\"waveDocument\":"; Document(waves.source);
        std::cout << ",\"roundDocument\":"; Document(rounds.source);
        std::cout << ",\"waveRows\":["; bool first = true;
        for (const auto& row : waves.rows)
        {
            if (!first) std::cout << ','; first = false;
            std::cout << "{\"record\":" << row.recordIndex << ",\"wave\":" << row.waveId
                << ",\"point\":" << row.spawnPoint << ",\"start\":" << row.startTime
                << ",\"end\":" << row.endTime << ",\"interval\":" << row.interval << ",\"types\":";
            Numbers(row.types); std::cout << ",\"weights\":"; Numbers(row.weights);
            std::cout << ",\"weightTotal\":" << +row.weightTotal << ",\"eventHex\":"; Hex(row.eventMessage);
            std::cout << ",\"isEvent\":" << (row.IsEvent() ? "true" : "false") << '}';
        }
        std::cout << "],\"waveGroups\":["; first = true;
        for (const auto& [wave, grouped] : waves.groups)
            for (const auto& [id, indices] : grouped)
            {
                if (!first) std::cout << ','; first = false;
                std::cout << "{\"wave\":" << wave << ",\"point\":" << id << ",\"rows\":";
                Numbers(indices); std::cout << '}';
            }
        std::cout << "],\"roundHeaderRecord\":" << rounds.headerRecord << ",\"roundRows\":["; first = true;
        for (const auto& row : rounds.rows)
        {
            if (!first) std::cout << ','; first = false;
            std::cout << "{\"record\":" << row.recordIndex << ",\"round\":" << row.round << ",\"waves\":";
            Numbers(row.waves); std::cout << '}';
        }
        std::cout << "],\"roundTicks\":["; first = true;
        for (const auto& round : rounds.rows)
        {
            WaveController controller; std::vector<std::uint16_t> ids;
            for (auto id : round.waves) ids.push_back(WaveKey(id));
            if (!controller.Activate(waves, ids, error)) throw std::runtime_error("Activate: " + error.message);
            if (!first) std::cout << ','; first = false;
            std::cout << "{\"record\":" << round.recordIndex << ",\"round\":" << round.round << ",\"activeBefore\":";
            Numbers(controller.ActiveWaves()); std::cout << ",\"missing\":"; Numbers(controller.MissingWaves());
            std::vector<Attempt> attempts;
            const auto tick = controller.Tick(0, Capture, &attempts);
            std::cout << ",\"activeAfter\":"; Numbers(controller.ActiveWaves());
            std::cout << ",\"valid\":" << (tick.valid ? "true" : "false")
                << ",\"finished\":" << (tick.finished ? "true" : "false") << ",\"attempts\":" << tick.attempts << ",\"dispatch\":[";
            bool firstAttempt = true;
            for (const auto& attempt : attempts)
            {
                if (!firstAttempt) std::cout << ','; firstAttempt = false;
                std::cout << "{\"record\":" << attempt.record << ",\"wave\":" << attempt.wave << ",\"point\":" << attempt.point << ",\"clock\":";
                Clock(attempt.clock); std::cout << '}';
            }
            std::cout << "],\"programs\":["; bool firstProgram = true;
            std::vector<std::uint16_t> visited;
            for (const auto id : ids)
            {
                bool seen = false; for (auto prior : visited) seen = seen || prior == id;
                if (seen) continue;
                visited.push_back(id);
                const auto* runner = controller.FindWave(id); if (!runner) continue;
                if (!firstProgram) std::cout << ','; firstProgram = false;
                std::cout << "{\"wave\":" << id << ",\"clocks\":[";
                bool firstClock = true;
                for (const auto& clock : runner->Clocks())
                {
                    if (!firstClock) std::cout << ','; firstClock = false; Clock(clock);
                }
                std::cout << "]}";
            }
            std::cout << "]}";
        }
        std::cout << "],\"missingReferences\":["; first = true;
        for (const auto& ref : FindMissingReferences(rounds, waves, points, 0))
        {
            if (!first) std::cout << ','; first = false;
            std::cout << "{\"kind\":" << static_cast<int>(ref.kind) << ",\"roundRecord\":" << ref.roundRecord
                << ",\"waveRecord\":" << ref.waveRecord << ",\"round\":" << ref.round << ",\"wave\":" << ref.wave
                << ",\"point\":" << ref.spawnPoint << '}';
        }
        std::cout << "]}\n";
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    return 0;
}
