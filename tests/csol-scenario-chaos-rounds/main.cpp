// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/scenario/config.hpp"
#include "../../native/csol/scenario/round_settings.hpp"
#include "../../native/csol/scenario/monster_attributes.hpp"
#include "../../native/csol/config_codec.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>

using namespace csol::scenario;
namespace
{
int checks{}, failures{};
void Check(bool value, const char* message)
{
    ++checks;
    if (!value) { ++failures; std::cerr << message << '\n'; }
}
std::string Read(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("missing fixture");
    return {std::istreambuf_iterator<char>(input), {}};
}
bool Same(const RoundTable& a, const RoundTable& b)
{
    if (a.source.sourceBytes != b.source.sourceBytes || a.headerRecord != b.headerRecord ||
        a.source.records.size() != b.source.records.size() || a.rows.size() != b.rows.size()) return false;
    for (std::size_t i = 0; i < a.source.records.size(); ++i)
        if (a.source.records[i].fields != b.source.records[i].fields ||
            a.source.records[i].byteOffset != b.source.records[i].byteOffset) return false;
    for (std::size_t i = 0; i < a.rows.size(); ++i)
        if (a.rows[i].recordIndex != b.rows[i].recordIndex || a.rows[i].round != b.rows[i].round ||
            a.rows[i].waves != b.rows[i].waves) return false;
    return true;
}
struct ExpectedRow
{
    std::vector<int> groups;
    std::array<int, 9> values; // columns3..11 via original HW short/byte getters
};
struct ExpectedMap { const char* name; std::vector<ExpectedRow> rows; };
struct ScannerCase { const char* text; bool valid; std::vector<int> groups; };
struct ModeCase { int mode; std::size_t first; };
#include "expected.inc"

const std::string Prefix = "gametype,4\r\nroundTime,0\r\nwallBonusDmg,500\r\n"
    "wallBonusPoint,800\r\nwallexp,400\r\nRewardRound,0\r\n";

void Boundaries()
{
    ConfigError error;
    RoundTable table;
    for (const auto& item : Modes)
    {
        const auto input = Prefix + "room,123\r\nlabel,456\r\n";
        Check(ReadPositionalRounds(input, item.mode, table, error), "original mode selector accepted");
        Check(table.headerRecord == item.first - 1 && table.rows.front().recordIndex == item.first &&
            table.rows.front().waves == std::vector<int>{item.first == 7 ? 456 : 123}, "original mode row selection");
    }
    for (const auto& item : Scanners)
    {
        const auto input = Prefix + "room,trigger id\r\n\"nonnumeric label\",\"" + item.text + "\"\r\n";
        const auto before = table;
        const bool result = ReadPositionalRounds(input, 15, table, error);
        Check(result == item.valid, "original scanner validity");
        if (result)
            Check(table.rows.size() == 1 && table.rows[0].waves == item.groups && table.rows[0].round == 0,
                "original scanner list and ignored column0");
        else Check(Same(table, before), "scanner failure transaction preserves output");
    }
    const auto shortRows = Prefix + "anything,anything\r\n\r\nlabel\r\n,\r\n999,8/8-9\r\n999,1\r\n";
    Check(ReadPositionalRounds(shortRows, 15, table, error), "short physical rows accepted");
    Check(table.rows.size() == 5 && table.rows[0].waves.empty() && table.rows[1].waves.empty() &&
        table.rows[2].waves.empty() && table.rows[3].waves == std::vector<int>{8,8,9} &&
        table.rows[4].waves == std::vector<int>{1}, "empty rows and duplicate diagnostic IDs preserve physical order");
    for (std::size_t i = 0; i < table.rows.size(); ++i)
    {
        Check(table.rows[i].recordIndex == i + 7, "empty physical row index retained");
        RoundSettings settings{99,99}; RoundMonsterDeltas deltas;
        Check(ReadRoundSettings(table, i, settings, error) && settings.music == 0 && settings.playerSpawn == 0,
            "padded empty setting cells use existing consumer");
        Check(ReadRoundMonsterDeltas(table, i, deltas, error) && deltas.health == 0 && deltas.speed == 0,
            "padded empty monster cells use existing consumer");
    }
    const auto before = table;
    for (const auto& input : std::vector<std::string>{"", "a,b\n", "a\nb\nc\nd\ne\nf\ng\nh\n",
            Prefix + "a,b\nlabel,1234567890\n", Prefix + "a,b\n\"unterminated",
            Prefix + "a,b\nlabel," + std::string(65537, 'x'), std::string("a\0b", 3)})
    {
        Check(!ReadPositionalRounds(input, 15, table, error), "unsafe positional input rejected");
        Check(Same(table, before) && !error.message.empty(), "unsafe input leaves source rows and provenance untouched");
    }
    Check(ReadPositionalRounds(Prefix + "header,list\r\n", 15, table, error) && table.rows.empty(),
        "zero physical data records remain empty");
    Check(ReadPositionalRounds(Prefix + "label,1\r\n", 0, table, error), "six-row layout retained");
    RoundSettings settings{99,99};
    Check(!ReadRoundSettings(table, 0, settings, error) && settings.music == 99,
        "unadapted non-mode15 setting consumer does not silently accept different metadata");
    Check(ReadPositionalRounds(Prefix + "header,list\r\nlabel,1\r\n", 15, table, error), "source-boundary fixture");
    Check(!ReadRoundSettings(table, table.rows.size(), settings, error), "past-last consumer index rejected");
    table.rows[0].recordIndex = 99999;
    Check(!ReadRoundSettings(table, 0, settings, error), "tampered source reference rejected");

    RoundTable ordinary;
    Check(ReadRounds("round,waveTypes\n17,2 3\n", ordinary, error), "ordinary named loader retained");
    WaveTable waves; SpawnPoints points;
    const auto missing = FindMissingReferences(ordinary, waves, points, 0);
    Check(missing.size() == 2 && missing[0].round == 17 && missing[0].roundRecord == 1 &&
        missing[0].wave == 2 && missing[1].wave == 3, "ordinary missing references keep original diagnostic identity");
    Check(!ReadRounds(Prefix + "room,trigger id\r\n1,1\r\n", ordinary, error),
        "named loader is not silently relaxed for Chaos");
}

void Actual(const std::filesystem::path& directory)
{
    ConfigError error;
    for (const auto& expected : Maps)
    {
        const auto plain = Read(directory / (std::string(expected.name) + ".csv"));
        auto encoded = Read(directory / (std::string(expected.name) + ".cso"));
        Check(csol::DecodeConfiguration(std::as_writable_bytes(std::span(encoded.data(), encoded.size()))),
            "real encrypted file decodes with production codec");
        while (!encoded.empty() && encoded.back() == '\0') encoded.pop_back();
        Check(encoded == plain, "complete native decode matches unchanged original HW decoder");
        RoundTable table;
        Check(ReadPositionalRounds(encoded, 15, table, error), "actual room/round table accepted");
        Check(table.source.sourceBytes == plain && table.headerRecord == 6 && table.rows.size() == expected.rows.size(),
            "actual source identity and physical row count");
        for (std::size_t i = 0; i < expected.rows.size() && i < table.rows.size(); ++i)
        {
            const auto& want = expected.rows[i]; const auto& row = table.rows[i];
            Check(row.recordIndex == i + 7 && row.round == 0 && row.waves == want.groups,
                "all actual physical group lists match original instructions");
            RoundSettings settings; RoundMonsterDeltas delta;
            Check(ReadRoundSettings(table, i, settings, error), "actual BGM/spawn source consumer");
            Check(ReadRoundMonsterDeltas(table, i, delta, error), "actual monster source consumer");
            const std::array<int,9> got{delta.damage,delta.range,delta.health,delta.armor,delta.speed,
                delta.interval,delta.money,settings.music,settings.playerSpawn};
            Check(got == want.values, "all actual scalar consumers match original HW getters");
        }
        RoundTable named;
        if (std::string_view(expected.name) == "zbs_zs_chaos")
        {
            Check(!ReadRounds(plain, named, error), "old named loader reproduces Chaos header failure");
            Check(table.rows.size() == 7 && table.source.records[6].fields[0] == "room" &&
                table.source.records[6].fields[1] == "trigger id", "real Chaos room header unchanged");
        }
        else
        {
            Check(ReadRounds(plain, named, error), "ordinary real map still accepts named API");
            Check(named.rows.size() == table.rows.size(), "ordinary map old/new row counts match");
            for (std::size_t i = 0; i < named.rows.size() && i < table.rows.size(); ++i)
                Check(named.rows[i].recordIndex == table.rows[i].recordIndex && named.rows[i].waves == table.rows[i].waves,
                    "ordinary real map groups and provenance remain equal");
        }
    }
}
}
int main(int argc, char** argv)
{
    try
    {
        if (argc != 2) throw std::runtime_error("usage: test fixture-directory");
        Boundaries(); Actual(argv[1]);
    }
    catch (const std::exception& error) { ++failures; std::cerr << error.what() << '\n'; }
    std::cout << "positional rounds checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
