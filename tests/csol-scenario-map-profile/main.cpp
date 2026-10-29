// SPDX-License-Identifier: GPL-3.0-or-later
// Fourth-table profile transaction only; existing three-table suites are separate.
#include "map_resources.hpp"
#include "fixtures.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <tuple>

using namespace csol::scenario;
namespace
{
int checks{}, failures{};
void Check(bool value, const char* label)
{ ++checks; if (!value) { ++failures; std::cerr << "FAIL: " << label << '\n'; } }
std::string Unhex(std::string_view value)
{
    std::string result;
    for (std::size_t i = 0; i < value.size(); i += 2) result.push_back(static_cast<char>(std::stoi(std::string(value.substr(i, 2)), nullptr, 16)));
    return result;
}
std::vector<std::string> Paths(std::string_view map)
{ const std::string name(map); return {"maps/zbs_wave_" + name + ".cso", "maps/zbs_" + name + ".cso", "maps/ZBS_ZT_" + name + ".cso", "maps/ZBS_TRG_" + name + ".cso"}; }
bool Same(const CsvDocument& a, const CsvDocument& b)
{
    if (a.sourceBytes != b.sourceBytes || a.records.size() != b.records.size()) return false;
    for (std::size_t i = 0; i < a.records.size(); ++i) if (a.records[i].byteOffset != b.records[i].byteOffset || a.records[i].fields != b.records[i].fields) return false;
    return true;
}
bool Same(const MapTables& a, const MapTables& b)
{
    if (a.mapName != b.mapName || a.scenarioType != b.scenarioType || !Same(a.waves.source, b.waves.source) || !Same(a.rounds.source, b.rounds.source) ||
        !Same(a.monsterTypes.source, b.monsterTypes.source) || a.waves.groups != b.waves.groups || a.rounds.headerRecord != b.rounds.headerRecord ||
        a.monsterTypes.byId != b.monsterTypes.byId || a.triggerRules.has_value() != b.triggerRules.has_value() || a.waves.rows.size() != b.waves.rows.size() ||
        a.rounds.rows.size() != b.rounds.rows.size() || a.monsterTypes.rows.size() != b.monsterTypes.rows.size()) return false;
    for (std::size_t i = 0; i < a.waves.rows.size(); ++i)
    {
        const auto& x = a.waves.rows[i]; const auto& y = b.waves.rows[i];
        if (std::tie(x.recordIndex, x.waveId, x.spawnPoint, x.startTime, x.endTime, x.interval, x.types, x.weights, x.weightTotal, x.eventMessage) !=
            std::tie(y.recordIndex, y.waveId, y.spawnPoint, y.startTime, y.endTime, y.interval, y.types, y.weights, y.weightTotal, y.eventMessage)) return false;
    }
    for (std::size_t i = 0; i < a.rounds.rows.size(); ++i)
    {
        const auto& x = a.rounds.rows[i]; const auto& y = b.rounds.rows[i];
        if (std::tie(x.recordIndex, x.round, x.waves) != std::tie(y.recordIndex, y.round, y.waves)) return false;
    }
    for (std::size_t i = 0; i < a.monsterTypes.rows.size(); ++i)
    {
        const auto& x = a.monsterTypes.rows[i]; const auto& y = b.monsterTypes.rows[i];
        if (std::tie(x.recordIndex, x.id, x.appearance, x.attackDamage, x.attackRange, x.attackInterval, x.health, x.armor, x.speed, x.bonusMoney,
            x.priority, x.skill, x.color, x.killType, x.eventClass, x.propensity, x.clearPoint, x.dropItems, x.experience, x.column18, x.column19, x.column20, x.column21, x.column22) !=
            std::tie(y.recordIndex, y.id, y.appearance, y.attackDamage, y.attackRange, y.attackInterval, y.health, y.armor, y.speed, y.bonusMoney,
            y.priority, y.skill, y.color, y.killType, y.eventClass, y.propensity, y.clearPoint, y.dropItems, y.experience, y.column18, y.column19, y.column20, y.column21, y.column22)) return false;
    }
    if (a.triggerRules)
    {
        const auto& x = *a.triggerRules; const auto& y = *b.triggerRules;
        if (!Same(x.source, y.source) || x.groups != y.groups || x.rows.size() != y.rows.size()) return false;
        for (std::size_t i = 0; i < x.rows.size(); ++i)
        {
            const auto& p = x.rows[i]; const auto& q = y.rows[i];
            if (std::tie(p.recordIndex, p.group, p.command, p.suffix, p.trigger, p.waves, p.cooldown, p.destinationRatio, p.endWaves) !=
                std::tie(q.recordIndex, q.group, q.command, q.suffix, q.trigger, q.waves, q.cooldown, q.destinationRatio, q.endWaves)) return false;
        }
    }
    return true;
}
struct File
{
    std::string bytes;
    std::optional<int> length{};
    bool present{true}, writeLength{true};
    explicit File(std::string value = {}) : bytes(std::move(value)) {}
};
struct Allocation { std::unique_ptr<unsigned char[]> memory; std::string original; };
struct Engine
{
    static Engine* active;
    std::map<std::string, File> files;
    std::vector<std::string> loads;
    std::vector<int> inputLengths;
    std::map<void*, Allocation> owned;
    std::vector<std::unique_ptr<unsigned char[]>> poisoned;
    std::size_t releases{};
    bool correctRelease{true}, intact{true};
    Engine() { if (active) throw std::runtime_error("nested fixture"); active = this; }
    ~Engine() { active = nullptr; }
    ResourceFileApi Api() { return {Load, Free}; }
    void Configure(std::string_view name, bool changed = false)
    {
        const auto paths = Paths(name);
        files[paths[0]] = File(Unhex(changed ? fixtures::waveB : fixtures::waveA));
        files[paths[1]] = File(Unhex(changed ? fixtures::roundB : fixtures::roundA));
        files[paths[2]] = File(Unhex(changed ? fixtures::typeB : fixtures::typeA));
        files[paths[3]] = File(Unhex(changed ? fixtures::triggerB : fixtures::triggerA));
    }
    void Reset()
    { Check(owned.empty(), "all previous callback allocations freed"); loads.clear(); inputLengths.clear(); releases = 0; correctRelease = intact = true; }
    static unsigned char* Load(const char* path, int* length)
    {
        auto& e = *active; e.loads.emplace_back(path); e.inputLengths.push_back(*length);
        const auto at = e.files.find(path); if (at == e.files.end()) return nullptr;
        const auto& file = at->second;
        if (file.writeLength) *length = file.length.value_or(static_cast<int>(file.bytes.size()));
        if (!file.present) return nullptr;
        auto memory = std::make_unique<unsigned char[]>(file.bytes.size() + 1);
        std::memcpy(memory.get(), file.bytes.data(), file.bytes.size()); memory[file.bytes.size()] = 0;
        auto* pointer = memory.get(); e.owned.emplace(pointer, Allocation{std::move(memory), file.bytes + '\0'}); return pointer;
    }
    static void Free(void* pointer)
    {
        auto& e = *active; ++e.releases; const auto at = e.owned.find(pointer);
        if (at == e.owned.end()) { e.correctRelease = false; return; }
        auto& item = at->second;
        e.intact &= std::memcmp(pointer, item.original.data(), item.original.size()) == 0;
        std::memset(pointer, 0xdd, item.original.size()); e.poisoned.push_back(std::move(item.memory)); e.owned.erase(at);
    }
};
Engine* Engine::active{};
void Calls(const Engine& engine, const std::vector<std::string>& paths, std::size_t releases)
{
    Check(engine.loads == paths, "exact required relative paths and original case in order");
    Check(std::all_of(engine.inputLengths.begin(), engine.inputLengths.end(), [](int n) { return n == 0; }), "every load receives initialized zero reported length");
    Check(engine.releases == releases && engine.owned.empty() && engine.correctRelease, "nonnull engine memory released exactly once, including fourth-table failure");
    Check(engine.intact, "all reported ciphertext plus engine NUL bytes stay unchanged before free");
}
void Profiles()
{
    for (int type : {1, 3, 4, 10})
    {
        Engine engine; engine.Configure("zs_profile"); MapResources resources; ResourceError error{"old", {33, "old"}};
        Check(resources.BeginMap("zs_profile", engine.Api(), error, type), "all four supported resolved subtypes require TRG");
        Calls(engine, Paths("zs_profile"), 4);
        Check(error.path.empty() && error.detail.byteOffset == 0 && error.detail.message.empty(), "successful fourth-table load clears old diagnostic");
        Check(resources.Tables() && resources.Tables()->scenarioType == type && resources.Tables()->triggerRules && resources.Tables()->triggerRules->rows[0].waves == std::vector<std::uint16_t>{1}, "profile and owned fourth table committed together");
        engine.Reset(); Check(resources.Reload(engine.Api(), error), "reload retains explicit subtype"); Calls(engine, Paths("zs_profile"), 4);
    }
    for (int type : {-1, 0, 2, 5, 9, 11, 15, 65537, INT32_MAX})
    {
        Engine engine; engine.Configure("zs_profile"); const auto paths = Paths("zs_profile");
        engine.files[paths[3]].length = -8; MapResources resources; ResourceError error;
        const bool loaded = type == 0 ? resources.BeginMap("zs_profile", engine.Api(), error) : resources.BeginMap("zs_profile", engine.Api(), error, type);
        Check(loaded, "non-trigger subtype and default zero ignore even an invalid TRG file");
        Calls(engine, {paths.begin(), paths.begin() + 3}, 3);
        Check(resources.Tables() && resources.Tables()->scenarioType == type && !resources.Tables()->triggerRules, "non-trigger exact integer subtype retained without optional table");
        Check(resources.Tables()->rounds.source.records[0].fields[0] == "10", "row-zero metadata does not override explicitly resolved subtype");
    }
}
void FourthFailures()
{
    for (int failure = 0; failure < 11; ++failure)
    {
        Engine engine; engine.Configure("zs_test"); MapResources resources; ResourceError error;
        Check(resources.BeginMap("zs_test", engine.Api(), error, 1), "fourth-table failure fixture starts with valid complete profile");
        const auto before = *resources.Tables(); const auto* pointer = resources.Tables(); const auto paths = Paths("zs_test");
        engine.Reset(); engine.Configure("zs_test", true); auto& file = engine.files[paths[3]];
        if (failure == 0) { file.present = false; file.length = 999; }
        if (failure == 1) file.length = 0;
        if (failure == 2) file.length = -8;
        if (failure == 3) file.length = 7;
        if (failure == 4) file.length = 8 * 1024 * 1024 + 8;
        if (failure == 5) file.writeLength = false;
        if (failure == 6) file.bytes = Unhex(fixtures::empty);
        if (failure == 7) file.bytes = Unhex(fixtures::waveA);
        if (failure == 8) file.bytes = Unhex(fixtures::invalidRule);
        if (failure == 9) file.bytes = Unhex(fixtures::embeddedNul);
        if (failure == 10) file.bytes.assign(8, '\0');
        Check(!resources.Reload(engine.Api(), error), "missing malformed size corrupt content and schema errors reject fourth table");
        Calls(engine, paths, failure == 0 ? 3 : 4);
        Check(resources.Tables() == pointer && Same(*resources.Tables(), before), "failed last table preserves complete old four-table bytes fields indexes profile and identity");
        Check(error.path == paths[3] && !error.detail.message.empty(), "fourth-file failure reports exact TRG path");
        engine.Reset(); engine.files[paths[3]] = File(Unhex(fixtures::triggerB));
        Check(resources.Reload(engine.Api(), error), "repair fourth file allows one complete candidate commit"); Calls(engine, paths, 4);
        const auto& now = *resources.Tables();
        Check(now.waves.rows[0].waveId == 2 && now.rounds.rows[0].round == 2 && now.monsterTypes.rows[0].id == 2 &&
            now.triggerRules && now.triggerRules->rows[0].trigger == 2 && now.triggerRules->rows[0].command == 'g' && now.scenarioType == 1,
            "all four new datasets commit together after final-file repair");
    }
}
void Lifecycle()
{
    Engine engine; engine.Configure("zs_same"); MapResources resources; ResourceError error; const auto paths = Paths("zs_same");
    Check(resources.BeginMap("zs_same", engine.Api(), error, 1) && resources.Tables()->triggerRules, "same-map subtype transition starts with TRG");
    engine.Reset(); engine.files.erase(paths[3]);
    Check(resources.BeginMap(resources.MapName(), engine.Api(), error, 0) && resources.Tables()->scenarioType == 0 && !resources.Tables()->triggerRules, "same-map restart to zero clears optional table and preserves alias safely");
    Calls(engine, {paths.begin(), paths.begin() + 3}, 3);
    engine.Reset();
    Check(!resources.BeginMap(resources.MapName(), engine.Api(), error, 3) && !resources.Tables() && resources.MapName() == "zs_same", "restart to trigger subtype drops old session before required TRG fails");
    Calls(engine, paths, 3);
    engine.Reset(); engine.files[paths[3]] = File(Unhex(fixtures::triggerB));
    Check(resources.Reload(engine.Api(), error) && resources.Tables()->scenarioType == 3 && resources.Tables()->triggerRules->rows[0].trigger == 2, "failed new session retains requested subtype for subsequent recovery");
    Calls(engine, paths, 4);
    resources.Clear(); engine.Reset();
    Check(resources.MapName().empty() && !resources.Tables() && !resources.Reload(engine.Api(), error), "Clear removes profile session and prevents reload without map"); Calls(engine, {}, 0);
    engine.Reset();
    Check(resources.BeginMap("zs_same", engine.Api(), error) && resources.Tables()->scenarioType == 0 && !resources.Tables()->triggerRules, "new default session after Clear does not inherit prior trigger subtype");
    Calls(engine, {paths.begin(), paths.begin() + 3}, 3);
    engine.Reset(); engine.Configure("zs_other", true);
    Check(resources.BeginMap("zs_other", engine.Api(), error, 10) && resources.Tables()->mapName == "zs_other" && resources.Tables()->triggerRules->rows[0].command == 'g', "new map binds its own explicit subtype and fourth table"); Calls(engine, Paths("zs_other"), 4);
}
std::string Read(const char* path)
{ std::ifstream file(path, std::ios::binary); if (!file) throw std::runtime_error("cannot read actual resource"); return {std::istreambuf_iterator<char>(file), {}}; }
void Actual(char** paths)
{
    Engine engine; const auto names = Paths("zs_deadend");
    for (std::size_t i = 0; i < 4; ++i) engine.files[names[i]] = File(Read(paths[i]));
    MapResources resources; ResourceError error; Check(resources.BeginMap("zs_deadend", engine.Api(), error, 1), "actual deadend subtype one loads required four resources");
    Calls(engine, names, 4);
    if (!resources.Tables()) return;
    const auto before = *resources.Tables(); const auto* pointer = resources.Tables();
    Check(before.scenarioType == 1 && before.waves.rows.size() == 288 && before.rounds.rows.size() == 2 && before.monsterTypes.rows.size() == 38 && before.triggerRules && before.triggerRules->rows.size() == 29, "actual common has 288 wave 2 round 38 monster 29 trigger rows");
    Check(before.triggerRules && before.triggerRules->source.sourceBytes == Read(paths[4]), "fourth resource preserves exact independently decoded original bytes after callback free");
    const auto saved = engine.files[names[3]]; engine.Reset(); engine.files.erase(names[3]);
    Check(!resources.Reload(engine.Api(), error) && error.path == names[3], "actual required TRG missing fails with exact path"); Calls(engine, names, 3);
    Check(resources.Tables() == pointer && Same(*resources.Tables(), before), "actual failed fourth load retains every field of all previous tables");
    engine.Reset(); engine.files[names[3]] = saved;
    Check(resources.Reload(engine.Api(), error) && Same(*resources.Tables(), before), "actual reload commits complete identical owned four-table profile"); Calls(engine, names, 4);
    engine.Reset();
    Check(resources.BeginMap(resources.MapName(), engine.Api(), error, 0) && !resources.Tables()->triggerRules && resources.Tables()->scenarioType == 0, "actual same map explicit subtype zero ignores available TRG"); Calls(engine, {names.begin(), names.begin() + 3}, 3);
    resources.Clear(); Check(!resources.Tables() && resources.MapName().empty(), "actual map profile clears completely");
}
}
int main(int argc, char** argv)
{
    try
    {
        if (argc == 1) { Profiles(); FourthFailures(); Lifecycle(); }
        else if (argc == 7 && std::string_view(argv[1]) == "--actual") Actual(argv + 2);
        else throw std::runtime_error("usage: map_profile_tests [--actual commonWave commonRound commonZT commonTRG independentDecodedTRG]");
    }
    catch (const std::exception& error) { ++failures; std::cerr << "EXCEPTION: " << error.what() << '\n'; }
    std::cout << "map_profile checks=" << checks << " failures=" << failures << '\n'; return failures ? 1 : 0;
}
