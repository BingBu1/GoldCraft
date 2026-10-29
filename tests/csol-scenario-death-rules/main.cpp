// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <climits>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include "../csol-scenario-point-manager/engine_fixture.h"
#include "../../native/csol/server/scenario_map.h"
#include "../csol-scenario-map-session/fixtures.hpp"
#include "death_fixtures.hpp"

using namespace csol::scenario;
namespace
{
int checks{}, failures{};
void Check(bool condition, const char* label)
{ ++checks; if (!condition) { ++failures; std::cerr << "FAIL: " << label << '\n'; } }
void Require(bool condition, const char* label)
{ Check(condition, label); if (!condition) throw std::runtime_error(label); }
std::string Hex(std::string_view text)
{
    const auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    std::string result;
    for (std::size_t i = 0; i < text.size(); i += 2)
        result.push_back(static_cast<char>(digit(text[i]) * 16 + digit(text[i + 1])));
    return result;
}
std::span<const std::byte> Bytes(const std::string& data)
{ return std::as_bytes(std::span(data.data(), data.size())); }
std::string Quote(std::string_view text)
{
    std::string result = "\"";
    for (char c : text) { result += c; if (c == '"') result += c; }
    return result + '"';
}
std::string Read(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("fixture file missing");
    return {std::istreambuf_iterator<char>(stream), {}};
}
std::string Catalog(int subtype = 1)
{
    std::vector<std::string> header(59), row(59, "0");
    header[0] = ";map_id"; header[1] = "map_name";
    header[2] = "map_filename"; header[58] = "SubType";
    row[0] = "1"; row[1] = "test"; row[2] = "map_session_test";
    row[58] = std::to_string(subtype);
    std::string result;
    for (const auto& fields : {header, row})
    {
        for (std::size_t i = 0; i < fields.size(); ++i)
        { if (i) result += ','; result += fields[i]; }
        result += "\r\n";
    }
    return result;
}
constexpr std::string_view DeathPath = "maps/ZBS_DIE_EVENT_map_session_test.cso";

// The same callback contract drives pure resource ownership and the real SDK
// CCSOLScenarioMap. No alternate BeginMap/EndMap implementation is supplied.
struct FileEngine : Engine
{
    static FileEngine* current;
    struct Allocation { std::string bytes, path; };
    std::map<std::string, std::string> files;
    std::map<void*, Allocation> allocations;
    std::vector<std::string> loads, frees;
    std::function<void(std::string_view)> onLoad, onFree;
    std::optional<int> lengthOverride;
    FileEngine()
    {
        current = this;
        g_engfuncs.pfnLoadFileForMe = Load;
        g_engfuncs.pfnFreeFile = Free;
    }
    ~FileEngine()
    {
        Check(allocations.empty(), "all returned buffers were released");
        for (const auto& [p, unused] : allocations) delete[] static_cast<unsigned char*>(p);
        current = nullptr;
    }
    static unsigned char* Load(const char* path, int* length)
    {
        auto& engine = *current;
        Check(*length == 0, "file callback receives initialized length");
        engine.loads.emplace_back(path);
        if (engine.onLoad) engine.onLoad(path);
        const auto found = engine.files.find(path);
        if (found == engine.files.end()) return nullptr;
        auto* result = new unsigned char[found->second.size() + 1];
        std::memcpy(result, found->second.data(), found->second.size());
        result[found->second.size()] = 0;
        *length = engine.lengthOverride.value_or(static_cast<int>(found->second.size()));
        engine.allocations.emplace(result, Allocation{found->second, path});
        return result;
    }
    static void Free(void* buffer)
    {
        auto& engine = *current;
        const auto found = engine.allocations.find(buffer);
        Require(found != engine.allocations.end(), "release owns exact buffer once");
        const auto bytes = found->second.bytes;
        const auto path = found->second.path;
        Check(std::memcmp(buffer, bytes.data(), bytes.size()) == 0 &&
            static_cast<unsigned char*>(buffer)[bytes.size()] == 0,
            "parser preserves original engine ciphertext and extra NUL");
        engine.frees.push_back(path);
        if (engine.onFree) engine.onFree(path);
        std::memset(buffer, 0xcc, bytes.size() + 1);
        delete[] static_cast<unsigned char*>(buffer);
        engine.allocations.erase(found);
    }
    ResourceFileApi Api() { return {Load, Free}; }
    void BaseFiles()
    {
        files["resource/maplist.csv"] = Catalog();
        files["maps/zbs_wave_map_session_test.cso"] = Hex(fixtures::waves);
        files["maps/zbs_map_session_test.cso"] = Hex(fixtures::rounds);
        files["maps/ZBS_ZT_map_session_test.cso"] = Hex(fixtures::types);
        files["maps/ZBS_TRG_map_session_test.cso"] = Hex(fixtures::triggers);
    }
};
FileEngine* FileEngine::current{};

void Parsing()
{
    MonsterDeathRuleTable table; ConfigError error;
    const std::string csv = "任意翻译,不按名称,取列\r\n70000,-3,0,extra\r\n70000,0,1,duplicate\r\n4464,0,2\r\n-2147483648,1,-1\r\n2147483647,0,0\r\n";
    Require(ReadMonsterDeathRules(csv, table, error), "full-width duplicate table parses");
    Check(table.rows.size() == 5 && table.byType.size() == 4 && table.source.sourceBytes == csv,
        "all source rows and original bytes retained");
    Check(table.Find(70000) == &table.rows[0] && table.rows[0].recordIndex == 1 &&
        table.rows[0].drainMonsters && !table.rows[0].stopWaves, "first duplicate wins");
    Check(table.Find(4464) == &table.rows[2] && table.Find(INT_MIN) == &table.rows[3] &&
        table.Find(INT_MAX) == &table.rows[4] && !table.Find(-1), "keys retain full signed int32 identity");
    Check(table.source.records[1].fields[3] == "extra" && table.source.records[2].fields[3] == "duplicate",
        "extra source columns and ignored duplicate remain inspectable");
    auto broken = table; broken.byType[123] = 999;
    Check(!broken.Find(123), "out-of-range map index cannot escape rows");
    broken.byType[123] = 0;
    Check(!broken.Find(123), "mismatched map key cannot return another type");

    for (const auto& fixture : death_fixtures::numbers)
    {
        const auto text = Quote(fixture.text);
        Require(ReadMonsterDeathRules("header\n" + text + ',' + text + ',' + text + "\n", table, error),
            "defined atoi-like three-column input parses");
        Check(table.rows.size() == 1 && table.rows[0].typeId == fixture.value &&
            table.rows[0].drainMonsters == (fixture.value != 0) &&
            table.rows[0].stopWaves == (fixture.value != 0) && error.message.empty(),
            "type and both booleans follow signed decimal-prefix conversion");
    }
    const std::vector<std::string> invalid = {"", "header\n1,1\n", "header\n,1,1\n", "header\n1,,1\n",
        "header\n1,1,\n", "header\n\n", "header\n\"unclosed", "header\n\"1\"x,1,1\n",
        "header\n2147483648,1,1\n", "header\n-2147483649,1,1\n", "header\n1,2147483648,1\n",
        "header\n1,1,-2147483649\n", std::string("header\n1,1,1\0tail", 17)};
    Require(ReadMonsterDeathRules(csv, table, error), "seed transaction before invalid inputs");
    for (const auto& input : invalid)
    {
        Check(!ReadMonsterDeathRules(input, table, error) && !error.message.empty(), "unsafe input rejected");
        Check(table.source.sourceBytes == csv && table.rows.size() == 5 && table.Find(70000)->drainMonsters,
            "pure parse failure preserves previous table");
    }
    Check(ReadMonsterDeathRules("single nonstandard header", table, error) && table.rows.empty() && error.message.empty(),
        "header-only resource is a valid empty rule table");
    Check(ReadMonsterDeathRules("123,1,1\n456,0,1", table, error) && table.rows.size() == 1 && !table.Find(123) && table.Find(456),
        "row zero is always skipped regardless of content");
    for (int priority : {-128, -1, 0, 1, 127})
        for (int column : {INT_MIN, -1, 0, 1, INT_MAX})
            for (int actual : {INT_MIN, -1, 0, 1, 65536, INT_MAX})
            {
                MonsterType type; type.priority = static_cast<std::int8_t>(priority); type.column19 = column;
                std::optional<std::int32_t> expected;
                if (column > 0) expected = column;
                else if (priority && actual >= 0) expected = actual;
                Check(MonsterDeathTarget(type, actual) == expected, "target override priority and negative suppression");
            }
}

void Encoded()
{
    MonsterDeathRuleTable table; ConfigError error;
    for (const auto& fixture : death_fixtures::encoded)
    {
        Require(ReadMonsterDeathRules("old\n777,1,1", table, error), "seed encoded transaction");
        const auto cipher = Hex(fixture.hex);
        const bool result = ReadMonsterDeathRuleResource(Bytes(cipher), table, error);
        Check(result == fixture.valid, "independently encrypted resource expected validity");
        if (result) Check(table.source.sourceBytes == fixture.plain, "decoded source excludes only trailing NUL padding");
        else Check(table.Find(777) && table.source.sourceBytes == "old\n777,1,1" && !error.message.empty(),
            "invalid encrypted input preserves pure table");
    }
    for (std::size_t size : {std::size_t(0), std::size_t(1), std::size_t(7), std::size_t(9), std::size_t(8388616)})
    {
        const std::string cipher(size, '\0');
        Check(!ReadMonsterDeathRuleResource(Bytes(cipher), table, error) && error.message.find("size") != std::string::npos,
            "invalid encrypted bounds rejected before decoding");
    }
}

void Resources()
{
    FileEngine engine; MonsterDeathRuleResources resource; ResourceError error;
    engine.files[std::string(DeathPath)] = Hex(death_fixtures::good);
    Require(resource.BeginMap("map_session_test", engine.Api(), error), "pure resource loads valid table");
    Check(resource.Present() && resource.Rules().Find(500) && engine.frees.size() == 1,
        "valid file publishes after one release");
    const auto ownView = resource.MapName();
    Check(resource.BeginMap(ownView, engine.Api(), error) && resource.MapName() == "map_session_test",
        "resource copies own map name before clear");
    Check(resource.BeginMap("no_optional", engine.Api(), error) && !resource.Present() && resource.Rules().rows.empty() &&
        resource.MapName() == "no_optional" && error.detail.message.empty(), "missing optional file clears preceding map");
    Check(engine.loads.back() == "maps/ZBS_DIE_EVENT_no_optional.cso", "explicit cso path without csv fallback");
    for (const std::string& name : {std::string(), std::string("../bad"), std::string("a/b"), std::string(64, 'a')})
    {
        const auto count = engine.loads.size();
        Check(!resource.BeginMap(name, engine.Api(), error) && resource.MapName().empty() && !resource.Present() &&
            engine.loads.size() == count, "invalid names clear old resource without callbacks");
    }
    for (const auto api : {ResourceFileApi{nullptr, FileEngine::Free}, ResourceFileApi{FileEngine::Load, nullptr}})
    {
        const auto count = engine.loads.size();
        Check(!resource.BeginMap("map_session_test", api, error) && engine.loads.size() == count && error.path == DeathPath,
            "missing callback pair is rejected before reading");
    }
    for (int length : {-1, 0, 1, 7, 9, 8388616, INT_MAX})
    {
        engine.lengthOverride = length; const auto frees = engine.frees.size();
        Check(!resource.BeginMap("map_session_test", engine.Api(), error) && error.path == DeathPath &&
            error.detail.message.find("size") != std::string::npos && engine.frees.size() == frees + 1 &&
            engine.allocations.empty() && !resource.Present(), "invalid reported length still releases exactly once");
    }
    engine.lengthOverride.reset();
    for (bool valid : {true, false})
    {
        engine.files[std::string(DeathPath)] = Hex(valid ? death_fixtures::good : death_fixtures::missingColumn);
        const auto nested = [&](std::string_view)
        {
            const auto loads = engine.loads.size();
            Check(!resource.Clear() && !resource.BeginMap("nested", engine.Api(), error) &&
                engine.loads.size() == loads, "load and free callbacks reject destructive resource reentry");
            Check(!resource.Present() && resource.Rules().rows.empty() && resource.MapName() == "map_session_test",
                "callback cannot observe unpublished candidate");
        };
        engine.onLoad = nested; engine.onFree = nested;
        const bool result = resource.BeginMap("map_session_test", engine.Api(), error);
        Check(result == valid && resource.Present() == valid && engine.allocations.empty(), "outer resource result survives callbacks");
        Check(valid ? error.detail.message.empty() : error.path == DeathPath && error.detail.message.find("three columns") != std::string::npos,
            "outer success or parse failure overwrites nested shared-error diagnostic");
        engine.onLoad = {}; engine.onFree = {};
    }
    Check(resource.Clear() && resource.MapName().empty() && resource.Rules().rows.empty() && !resource.Present(),
        "clear succeeds again once callbacks finish");
}

void SdkMap()
{
    FileEngine engine; engine.BaseFiles(); CCSOLScenarioMap map;
    engine.files[std::string(DeathPath)] = Hex(death_fixtures::good);
    Require(map.BeginMap("map_session_test"), "actual SDK BeginMap accepts optional valid rule table");
    Check(map.State() == CSOLScenarioMapState::ResourcesLoaded && map.DeathRules().Find(500) &&
        map.DeathRules().rows.size() == 1 && map.Error().detail.message.empty(), "SDK owner publishes valid death rules");
    Check(engine.loads.size() == 6 && engine.loads.back() == DeathPath && engine.frees.size() == 6,
        "SDK owner loads optional death rules after five required resources");
    const auto previousName = std::string_view(map.Tables()->mapName);
    Check(map.BeginMap(previousName) && map.DeathRules().Find(500), "SDK map handles own aliased map name");
    engine.files.erase(std::string(DeathPath));
    Check(map.BeginMap("map_session_test") && map.DeathRules().rows.empty() &&
        map.State() == CSOLScenarioMapState::ResourcesLoaded, "SDK missing optional table clears previous death rules");
    engine.files[std::string(DeathPath)] = Hex(death_fixtures::good);
    Require(map.BeginMap("map_session_test"), "restore previous SDK table before corrupt replacement");
    engine.files[std::string(DeathPath)] = Hex(death_fixtures::missingColumn);
    Check(!map.BeginMap("map_session_test") && map.State() == CSOLScenarioMapState::Failed &&
        map.DeathRules().rows.empty() && map.Error().path == DeathPath && engine.allocations.empty(),
        "SDK malformed optional file explicitly fails and cannot retain old rules");
    Check(!map.CollectEntities(), "failed SDK resource session cannot collect entities");
    const auto calls = engine.calls.size();
    Check(map.EndMap() && map.State() == CSOLScenarioMapState::Empty && !map.Tables() && map.DeathRules().rows.empty() &&
        map.Error().detail.message.empty() && engine.calls.size() == calls, "SDK EndMap clears failure without engine entity side effects");
    for (bool valid : {true, false})
    {
        engine.files[std::string(DeathPath)] = Hex(valid ? death_fixtures::good : death_fixtures::missingColumn);
        const auto reenter = [&](std::string_view path)
        {
            const auto loads = engine.loads.size();
            Check(!map.BeginMap("nested") && !map.EndMap() && !map.CollectEntities() && engine.loads.size() == loads,
                "actual SDK callbacks reject nested begin end and collect");
            if (path == DeathPath) Check(map.State() == CSOLScenarioMapState::Empty && map.DeathRules().rows.empty(),
                "actual SDK optional callback never observes partially published death table");
        };
        engine.onLoad = reenter; engine.onFree = reenter;
        Check(map.BeginMap("map_session_test") == valid, "actual SDK outer load retains result after callback reentry");
        Check(valid ? map.Error().detail.message.empty() : map.Error().detail.message.find("three columns") != std::string::npos,
            "actual SDK owner retains its own final parse diagnostic");
        engine.onLoad = {}; engine.onFree = {};
    }
    for (bool missingBase : {false, true})
    {
        engine.BaseFiles(); engine.files[std::string(DeathPath)] = Hex(death_fixtures::good);
        Require(map.BeginMap("map_session_test"), "seed SDK rules before prerequisite refusal");
        if (missingBase) engine.files.erase("maps/zbs_wave_map_session_test.cso");
        else engine.files["resource/maplist.csv"] = Catalog(0);
        const auto before = std::count(engine.loads.begin(), engine.loads.end(), std::string(DeathPath));
        Check(!map.BeginMap("map_session_test") && map.State() == CSOLScenarioMapState::Failed && map.DeathRules().rows.empty(),
            "SDK prerequisite failure clears previous death rules");
        Check(std::count(engine.loads.begin(), engine.loads.end(), std::string(DeathPath)) == before,
            "SDK death resource is not loaded after required-resource or subtype failure");
    }
    Check(map.EndMap() && map.EndMap(), "SDK EndMap is repeatable");
}

void Actual(const std::filesystem::path& common)
{
    const auto cipher = Read(common / "maps/zbs_die_event_zs_lastride2.cso");
    const std::string expected = "ZombieType,KillAllZombiesWithoutBoss,StopWave\r\n500,1,1\r\n";
    MonsterDeathRuleTable table; ConfigError error;
    Require(ReadMonsterDeathRuleResource(Bytes(cipher), table, error), "real lastride2 encrypted resource parses");
    Check(cipher.size() == 56 && table.source.sourceBytes == expected && table.rows.size() == 1 &&
        table.rows[0].recordIndex == 1 && table.rows[0].typeId == 500 && table.rows[0].drainMonsters && table.rows[0].stopWaves,
        "real resource complete source and all three values match independent decode");
    Check(!std::filesystem::exists(common / "maps/zbs_die_event_zs_deadend.cso"),
        "optional deadend death file is absent in this frozen common directory only");
}

void Oracle(const std::filesystem::path& input)
{
    std::ifstream stream(input); if (!stream) throw std::runtime_error("oracle input missing");
    std::string line; std::size_t index{};
    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto text = Quote(Hex(line)); MonsterDeathRuleTable table; ConfigError error;
        const bool ok = ReadMonsterDeathRules("header\n" + text + ',' + text + ',' + text + "\n", table, error);
        std::cout << index++ << ' ' << int(ok);
        if (ok) std::cout << ' ' << table.rows.at(0).typeId << ' ' << int(table.rows[0].drainMonsters) << ' ' << int(table.rows[0].stopWaves);
        std::cout << '\n';
    }
}
}
int main(int argc, char** argv)
{
    try
    {
        if (argc == 3 && std::string_view(argv[1]) == "--oracle") { Oracle(argv[2]); return 0; }
        if (argc == 3 && std::string_view(argv[1]) == "--actual") Actual(argv[2]);
        else if (argc == 1) { Parsing(); Encoded(); Resources(); SdkMap(); }
        else throw std::runtime_error("unexpected arguments");
    }
    catch (const std::exception& error) { ++failures; std::cerr << "EXCEPTION: " << error.what() << '\n'; }
    std::cout << "death_rules checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
