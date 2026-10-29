// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise production encrypted-resource readers. Ciphertext fixtures were
// generated independently; original HW instruction comparison is separate.
#include "resources.hpp"
#include "../../native/csol/config_codec.hpp"

#include <algorithm>
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
    if (!value) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
}
std::string Unhex(std::string_view value)
{
    if (value == "-") return {};
    if (value.size() % 2) throw std::runtime_error("odd hex length");
    std::string result;
    const auto digit = [](char c) -> unsigned
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        throw std::runtime_error("bad hex character");
    };
    for (std::size_t i = 0; i < value.size(); i += 2)
        result.push_back(static_cast<char>((digit(value[i]) << 4) | digit(value[i + 1])));
    return result;
}
std::string Hex(std::string_view value)
{
    if (value.empty()) return "-";
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (unsigned char c : value) { result += digits[c >> 4]; result += digits[c & 15]; }
    return result;
}
auto Bytes(std::string_view value) { return std::as_bytes(std::span(value.data(), value.size())); }
auto Mutable(std::string& value) { return std::as_writable_bytes(std::span(value.data(), value.size())); }
std::string Read(const char* path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open resource/decoded input");
    return {std::istreambuf_iterator<char>(input), {}};
}
bool Same(const CsvDocument& a, const CsvDocument& b)
{
    if (a.sourceBytes != b.sourceBytes || a.records.size() != b.records.size()) return false;
    for (std::size_t i = 0; i < a.records.size(); ++i)
        if (a.records[i].byteOffset != b.records[i].byteOffset || a.records[i].fields != b.records[i].fields) return false;
    return true;
}
bool Same(const WaveTable& a, const WaveTable& b)
{
    if (!Same(a.source, b.source) || a.groups != b.groups || a.rows.size() != b.rows.size()) return false;
    for (std::size_t i = 0; i < a.rows.size(); ++i)
    {
        const auto& x = a.rows[i]; const auto& y = b.rows[i];
        if (x.recordIndex != y.recordIndex || x.waveId != y.waveId || x.spawnPoint != y.spawnPoint ||
            x.startTime != y.startTime || x.endTime != y.endTime || x.interval != y.interval ||
            x.types != y.types || x.weights != y.weights || x.weightTotal != y.weightTotal ||
            x.eventMessage != y.eventMessage) return false;
    }
    return true;
}
bool Same(const RoundTable& a, const RoundTable& b)
{
    if (!Same(a.source, b.source) || a.headerRecord != b.headerRecord || a.rows.size() != b.rows.size()) return false;
    for (std::size_t i = 0; i < a.rows.size(); ++i)
        if (a.rows[i].recordIndex != b.rows[i].recordIndex || a.rows[i].round != b.rows[i].round || a.rows[i].waves != b.rows[i].waves) return false;
    return true;
}
void JsonHex(std::string_view bytes)
{
    std::cout << '"' << (bytes.empty() ? "" : Hex(bytes)) << '"';
}
template<class Values> void JsonNumbers(const Values& values)
{
    std::cout << '['; bool first = true;
    for (auto value : values)
    {
        if (!first) std::cout << ',';
        first = false; std::cout << +value;
    }
    std::cout << ']';
}
void JsonDocument(const CsvDocument& document)
{
    std::cout << "{\"sourceHex\":"; JsonHex(document.sourceBytes);
    std::cout << ",\"records\":["; bool first = true;
    for (const auto& record : document.records)
    {
        if (!first) std::cout << ','; first = false;
        std::cout << "{\"offset\":" << record.byteOffset << ",\"fieldsHex\":[";
        bool firstField = true;
        for (const auto& field : record.fields)
        {
            if (!firstField) std::cout << ','; firstField = false; JsonHex(field);
        }
        std::cout << "]}";
    }
    std::cout << "]}";
}
int Inspect(std::string_view kind, const char* path)
{
    const auto raw = Read(path); ConfigError error; WaveTable waves; RoundTable rounds;
    bool success;
    if (kind == "wave") success = ReadWaveResource(Bytes(raw), waves, error);
    else if (kind == "round") success = ReadRoundResource(Bytes(raw), rounds, error);
    else throw std::runtime_error("unknown inspection kind");
    std::cout << "{\"success\":" << (success ? "true" : "false") << ",\"errorOffset\":" << error.byteOffset << ",\"errorHex\":";
    JsonHex(error.message); std::cout << ",\"document\":";
    JsonDocument(kind == "wave" ? waves.source : rounds.source);
    std::cout << ",\"rows\":["; bool first = true;
    if (kind == "wave")
    {
        for (const auto& row : waves.rows)
        {
            if (!first) std::cout << ','; first = false;
            std::cout << "{\"record\":" << row.recordIndex << ",\"wave\":" << row.waveId << ",\"point\":" << row.spawnPoint
                << ",\"start\":" << row.startTime << ",\"end\":" << row.endTime << ",\"interval\":" << row.interval << ",\"types\":";
            JsonNumbers(row.types); std::cout << ",\"weights\":"; JsonNumbers(row.weights);
            std::cout << ",\"weightTotal\":" << +row.weightTotal << ",\"eventHex\":"; JsonHex(row.eventMessage);
            std::cout << ",\"isEvent\":" << (row.IsEvent() ? "true" : "false") << '}';
        }
        std::cout << "],\"groups\":["; first = true;
        for (const auto& [wave, group] : waves.groups)
            for (const auto& [point, indices] : group)
            {
                if (!first) std::cout << ','; first = false;
                std::cout << "{\"wave\":" << wave << ",\"point\":" << point << ",\"rows\":";
                JsonNumbers(indices); std::cout << '}';
            }
        std::cout << "]}";
    }
    else
    {
        for (const auto& row : rounds.rows)
        {
            if (!first) std::cout << ','; first = false;
            std::cout << "{\"record\":" << row.recordIndex << ",\"round\":" << row.round << ",\"waves\":";
            JsonNumbers(row.waves); std::cout << '}';
        }
        std::cout << "],\"headerRecord\":" << rounds.headerRecord << '}';
    }
    std::cout << '\n';
    return 0;
}
const std::string wavePlain = "waveID,spawnPoint,startTime,endTime,intervalTime,zombi,zombiRatio,eventmessage\n1,13,0,10,5,40,1,\n";
const std::string roundPlain = "round,waveTypes\n1,1\n";
const auto waveCipher = Unhex("9f845239e9cd77c152072cdd56d16d95e0baeb689e503038012b66fe5b5ad7ce7e5ad95b5caee67ed588dd40353c8d104ca9382948b74354c7573c2dce2b2e121350d2acd2c6e4759dc2d02b713bfb67e69c9835f9c475af3b7b7a8394964f8507e69ac8c805b47f");
const auto roundCipher = Unhex("ce4104ab4f2bae94b3d256606ff1c9d9a7b68d515090ae3b");

void RawTests()
{
    std::string empty;
    Check(csol::DecodeConfiguration(Mutable(empty)) && empty.empty(), "raw empty span succeeds without bytes");
    for (const auto& cipher : {waveCipher, roundCipher})
    {
        const auto& plain = cipher.size() == waveCipher.size() ? wavePlain : roundPlain;
        const auto padded = plain + std::string((-plain.size()) % 8, '\0');
        for (std::size_t offset = 0; offset < 8; ++offset)
        {
            std::string guarded(offset, '\x5a'); guarded += cipher; guarded += "guard";
            Check(csol::DecodeConfiguration(Mutable(guarded).subspan(offset, cipher.size())), "raw accepts unaligned address");
            Check(guarded.substr(offset, cipher.size()) == padded, "raw exact decoded bytes retain terminal padding");
            Check(guarded.substr(0, offset) == std::string(offset, '\x5a') && guarded.substr(offset + cipher.size()) == "guard", "raw preserves prefix/suffix guards");
        }
    }
    for (std::size_t size = 1; size < 32; ++size)
    {
        if (size % 8 == 0) continue;
        std::string value(size, '\xa5'); const auto original = value;
        Check(!csol::DecodeConfiguration(Mutable(value)) && value == original, "raw rejects incomplete blocks before mutation");
    }
}
void ResourceTests()
{
    ConfigError error{999, "old"}; WaveTable waves, expectedWaves; RoundTable rounds, expectedRounds;
    Check(ReadWaves(wavePlain, expectedWaves, error) && ReadRounds(roundPlain, expectedRounds, error), "decoded baseline fixtures import");
    Check(ReadWaveResource(Bytes(waveCipher), waves, error) && Same(waves, expectedWaves), "encrypted wave imports complete typed/source state");
    Check(error.message.empty() && error.byteOffset == 0, "wave success clears stale error");
    Check(ReadRoundResource(Bytes(roundCipher), rounds, error) && Same(rounds, expectedRounds), "encrypted round imports complete typed/source state");
    Check(error.message.empty() && error.byteOffset == 0, "round success clears stale error");
    const auto oldWaves = waves; const auto oldRounds = rounds;
    const auto rejectBoth = [&](const std::string& value)
    {
        const auto original = value;
        Check(!ReadWaveResource(Bytes(value), waves, error) && !error.message.empty(), "invalid encrypted wave rejected with diagnostic");
        Check(Same(waves, oldWaves) && value == original, "failed wave preserves source rows indices and input");
        Check(!ReadRoundResource(Bytes(value), rounds, error) && !error.message.empty(), "invalid encrypted round rejected with diagnostic");
        Check(Same(rounds, oldRounds) && value == original, "failed round preserves source rows and input");
    };
    rejectBoth({});
    for (std::size_t size : {1U, 7U, 9U, 23U, 25U})
    {
        auto partial = roundCipher; partial.resize(size, '\x42'); rejectBoth(partial);
    }
    rejectBoth(std::string(8 * 1024 * 1024 + 8, '\x42'));
    Check(error.message == "invalid encrypted scenario configuration size" && error.byteOffset == 0, "oversized aligned ciphertext rejected before allocation/decode");
    rejectBoth(Unhex("f15d9c83a14c4e63"));
    rejectBoth(Unhex("ce4104ab4f2bae94b3d256606ff1c9d96500937c451b5c4a"));
    Check(error.message == "embedded NUL in scenario configuration" && error.byteOffset == 19, "embedded NUL preserved and precisely diagnosed");
    rejectBoth(Unhex("ce4104ab4f2bae94b3d256606ff1c9d945a3db9dc9c5aa4d1cc817445873da4b"));
    Check(!ReadWaveResource(Bytes(roundCipher), waves, error) && Same(waves, oldWaves), "round data is not silently accepted as wave data");
    Check(!ReadRoundResource(Bytes(waveCipher), rounds, error) && Same(rounds, oldRounds), "wave data is not silently accepted as round data");
    Check(ReadWaveResource(Bytes(waveCipher), waves, error) && Same(waves, oldWaves) && error.message.empty(), "wave reload recovers after decode/CSV errors");
    Check(ReadRoundResource(Bytes(roundCipher), rounds, error) && Same(rounds, oldRounds) && error.message.empty(), "round reload recovers after decode/CSV errors");
}
void Actual(std::string_view kind, const char* rawPath, const char* decodedPath, int rowCount)
{
    const auto raw = Read(rawPath); const auto before = raw; const auto decoded = Read(decodedPath);
    auto plain = raw;
    Check(csol::DecodeConfiguration(Mutable(plain)), "actual aligned raw decrypt succeeds");
    Check(plain == decoded + std::string(raw.size() - decoded.size(), '\0'), "actual raw output exactly equals independent decoder plus original NUL padding");
    ConfigError error;
    if (kind == "wave")
    {
        WaveTable expected, actual;
        Check(ReadWaves(decoded, expected, error), "independent actual wave CSV imports");
        Check(ReadWaveResource(Bytes(raw), actual, error), "direct actual encrypted wave imports");
        Check(actual.rows.size() == static_cast<std::size_t>(rowCount), "actual wave row count matches independent CSV count");
        Check(Same(actual, expected) && actual.source.sourceBytes == decoded, "actual wave all source bytes fields offsets typed rows and groups match");
    }
    else if (kind == "round")
    {
        RoundTable expected, actual;
        Check(ReadRounds(decoded, expected, error), "independent actual round CSV imports");
        Check(ReadRoundResource(Bytes(raw), actual, error), "direct actual encrypted round imports");
        Check(actual.rows.size() == static_cast<std::size_t>(rowCount), "actual round row count matches independent CSV count");
        Check(Same(actual, expected) && actual.source.sourceBytes == decoded, "actual round all source bytes fields offsets and typed rows match");
    }
    else if (kind == "reject-round")
    {
        RoundTable actual; Check(ReadRounds(roundPlain, actual, error), "unsupported-schema baseline loads");
        const auto previous = actual;
        Check(!ReadRounds(decoded, actual, error) && Same(actual, previous), "decoded unsupported room schema rejected transactionally");
        const auto diagnostic = error;
        Check(!ReadRoundResource(Bytes(raw), actual, error) && Same(actual, previous), "encrypted unsupported room schema rejected transactionally");
        Check(error.message == diagnostic.message && error.byteOffset == diagnostic.byteOffset, "unsupported schema retains parser diagnostic after successful decryption");
        std::cout << "unsupported-schema: " << error.message << '\n';
    }
    else throw std::runtime_error("unknown resource kind");
    Check(raw == before, "actual encrypted input remains unchanged");
}
int Oracle()
{
    std::string operation, hex;
    while (std::cin >> operation >> hex)
    {
        if (operation != "D") throw std::runtime_error("unknown raw oracle command");
        auto bytes = Unhex(hex);
        const bool success = csol::DecodeConfiguration(Mutable(bytes));
        std::cout << (success ? 1 : 0) << ' ' << Hex(bytes) << '\n';
    }
    return 0;
}
}
int main(int argc, char** argv)
{
    try
    {
        if (argc == 2 && std::string_view(argv[1]) == "--oracle") return Oracle();
        if (argc == 4 && std::string_view(argv[1]) == "--inspect") return Inspect(argv[2], argv[3]);
        if (argc == 6 && std::string_view(argv[1]) == "--resource") Actual(argv[2], argv[3], argv[4], std::stoi(argv[5]));
        else if (argc == 1) { RawTests(); ResourceTests(); }
        else throw std::runtime_error("usage: resources_tests [--oracle | --inspect wave|round raw | --resource wave|round|reject-round raw decoded rowCount]");
    }
    catch (const std::exception& error) { ++failures; std::cerr << "EXCEPTION: " << error.what() << '\n'; }
    std::cout << "resources checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
