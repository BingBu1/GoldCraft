// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/scenario/core/resources.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

using namespace csol::scenario;
namespace
{
int checks{}, failures{};
void Check(bool value, const char* label)
{
    ++checks;
    if (!value) { ++failures; std::cerr << label << '\n'; }
}
std::string Read(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("fixture file missing");
    return {std::istreambuf_iterator<char>(stream), {}};
}
auto Bytes(std::string_view text) { return std::as_bytes(std::span(text.data(), text.size())); }
bool Same(const RoundTable& a, const RoundTable& b)
{
    if (a.source.sourceBytes != b.source.sourceBytes || a.headerRecord != b.headerRecord ||
        a.source.records.size() != b.source.records.size() || a.rows.size() != b.rows.size()) return false;
    for (std::size_t i = 0; i < a.source.records.size(); ++i)
        if (a.source.records[i].byteOffset != b.source.records[i].byteOffset ||
            a.source.records[i].fields != b.source.records[i].fields) return false;
    for (std::size_t i = 0; i < a.rows.size(); ++i)
        if (a.rows[i].recordIndex != b.rows[i].recordIndex || a.rows[i].round != b.rows[i].round ||
            a.rows[i].waves != b.rows[i].waves) return false;
    return true;
}
void Actual(const std::filesystem::path& root)
{
    ConfigError error;
    for (const char* name : {"chaos", "lostcity", "trap", "2gate", "lastclue", "nightmare"})
    {
        const auto stem = std::string("zbs_zs_") + name;
        const auto raw = Read(root / (stem + ".cso"));
        const auto plain = Read(root / (stem + ".csv"));
        const auto before = raw;
        for (int mode : {15, 48, 0, 4, 59})
        {
            RoundTable expected, actual;
            Check(ReadPositionalRounds(plain, mode, expected, error), "original plaintext fixture imports");
            Check(ReadPositionalRoundResource(Bytes(raw), mode, actual, error), "encrypted positional entry imports");
            Check(Same(expected, actual), "encrypted wrapper preserves exact source, columns, offsets and rows");
            const std::size_t first = mode == 15 || mode == 48 ? 7 : 6;
            Check(actual.headerRecord == first - 1 && actual.rows.front().recordIndex == first,
                "explicit service mode reaches physical parser without subtype coercion");
            Check(raw == before && error.message.empty(), "original ciphertext unchanged and error cleared");
        }
        RoundTable named, baseline;
        Check(ReadRounds("round,waveTypes\n17,2 3\n", baseline, error), "named baseline");
        named = baseline;
        if (std::string_view(name) == "chaos")
            Check(!ReadRoundResource(Bytes(raw), named, error) && Same(named, baseline),
                "old named resource API still refuses Chaos without changing the old output");
        else
        {
            Check(ReadRounds(plain, baseline, error) && ReadRoundResource(Bytes(raw), named, error),
                "ordinary encrypted named API remains supported");
            Check(Same(named, baseline), "ordinary named round identities remain unchanged");
        }
    }
}
void FailureTransactions(const std::filesystem::path& root)
{
    const auto valid = Read(root / "zbs_zs_chaos.cso");
    ConfigError error;
    RoundTable table;
    Check(ReadPositionalRoundResource(Bytes(valid), 15, table, error), "transaction baseline");
    const auto before = table;
    for (const auto& bad : std::vector<std::string>{"", valid.substr(0,valid.size()-1),
            std::string(8 * 1024 * 1024 + 8,'x'), Read(root / "embedded-nul.cso"),
            Read(root / "late-scanner.cso"), Read(root / "missing-column.cso"),
            Read(root / "broken-quote.cso"), Read(root / "short-metadata.cso")})
    {
        const auto input = bad;
        Check(!ReadPositionalRoundResource(Bytes(bad), 15, table, error), "invalid encrypted candidate rejected");
        Check(Same(table,before) && bad == input && !error.message.empty(),
            "whole previous table and encrypted input survive failure");
    }
    const auto padded = Read(root / "padded.cso");
    Check(ReadPositionalRoundResource(Bytes(padded), 15, table, error), "terminal encrypted padding accepted");
    Check(table.rows.size() == 2 && table.rows[0].waves == std::vector<int>{1,2,1} && table.rows[1].waves.empty(),
        "padded data keeps list order and physical empty room");
    Check(table.source.sourceBytes == Read(root / "padded.csv"), "only terminal NUL padding trimmed");
}
}
int main(int argc,char** argv)
{
    try
    {
        if (argc != 2) throw std::runtime_error("usage: test fixture-directory");
        Actual(argv[1]); FailureTransactions(argv[1]);
    }
    catch (const std::exception& error) { ++failures; std::cerr << error.what() << '\n'; }
    std::cout << "positional resource checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
