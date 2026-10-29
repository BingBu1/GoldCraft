// SPDX-License-Identifier: GPL-3.0-or-later
// Production MapResources with the exact GoldSrc load/free function signatures.
// The engine fixture appends a NUL outside the reported encrypted length.
#include "map_resources.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
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
std::string Unhex(std::string_view value)
{
    std::string result;
    for (std::size_t i = 0; i < value.size(); i += 2)
        result.push_back(static_cast<char>(std::stoi(std::string(value.substr(i, 2)), nullptr, 16)));
    return result;
}
const auto waveA = Unhex("9f845239e9cd77c152072cdd56d16d95e0baeb689e503038012b66fe5b5ad7ce7e5ad95b5caee67ed588dd40353c8d104ca9382948b74354c7573c2dce2b2e121350d2acd2c6e4759dc2d02b713bfb67e69c9835f9c475af3b7b7a8394964f8507e69ac8c805b47f");
const auto roundA = Unhex("ce4104ab4f2bae94b3d256606ff1c9d9a7b68d515090ae3b");
const auto waveB = Unhex("9f845239e9cd77c152072cdd56d16d95e0baeb689e503038012b66fe5b5ad7ce7e5ad95b5caee67ed588dd40353c8d104ca9382948b74354c7573c2dce2b2e121350d2acd2c6e4756e3e4ea781488b0e75948fc03bda4c76da10d6bde38b5bbf07e69ac8c805b47f");
const auto roundB = Unhex("ce4104ab4f2bae94b3d256606ff1c9d9e3bfbbfb8a793779");
const auto typeA = Unhex("835f82caaf900a240eaff32900440a42377b0d7ad75ecde9dc7fe3ec07863c63bd4665f8aa33619f189c9cd1afbd814e173b913cd667191c8d125dd2a445e0770e92aa59d836140356b317862101aa1940641a85e97373465edc2344e36d917e4e6be46d61e64441c5c9adaef38c5382ad976f3e32e594089913df8e4a4314e994eec02740ac5d8cf5ce3e66566c499385342efd0c81d8d93a3393667617be9e5b68ed1c079bdd9d9510cbe069fe39b892ae8cf5a2ec9327ae6a6323d105d95edde62e77eb0aaf06a1d8a957c8f95aea9eb737eb8c4cb218e34d225b9bd976250743b51188e51748");
const auto typeB = Unhex("835f82caaf900a240eaff32900440a42377b0d7ad75ecde9dc7fe3ec07863c63bd4665f8aa33619f189c9cd1afbd814e173b913cd667191c8d125dd2a445e0770e92aa59d836140356b317862101aa1940641a85e97373465edc2344e36d917e4e6be46d61e64441c5c9adaef38c5382ad976f3e32e594089913df8e4a4314e994eec02740ac5d8cf5ce3e66566c499385342efd0c81d8d93a3393667617be9ed789902d4c901b078ac5aea6e8277cb4774b34d1aed40aca1c523b0701721ff84fd90660aaa2b89e3a51ddec6ef37c8ed4fd69c05848a79a27a447e9b2880fd7f089c5141d3b9e622b4618b25ac5abe98df179728d8f322a6409dd34c1f8b2c0");
std::string WavePath(std::string_view map) { return "maps/zbs_wave_" + std::string(map) + ".cso"; }
std::string RoundPath(std::string_view map) { return "maps/zbs_" + std::string(map) + ".cso"; }
std::string TypePath(std::string_view map) { return "maps/ZBS_ZT_" + std::string(map) + ".cso"; }

struct File
{
    std::string bytes;
    std::optional<int> reported;
    bool present{true};
    bool writeLength{true};
};
struct Allocation
{
    std::unique_ptr<unsigned char[]> bytes;
    std::string original;
};
struct Engine
{
    static Engine* active;
    std::map<std::string, File> files;
    std::vector<std::string> loads;
    std::vector<int> inputLengths;
    std::map<void*, Allocation> allocated;
    std::vector<std::unique_ptr<unsigned char[]>> retired;
    std::size_t frees{};
    bool unchanged{true}, validRelease{true};

    Engine() { if (active) throw std::runtime_error("nested engine fixture"); active = this; }
    ~Engine() { active = nullptr; }
    ResourceFileApi Api() { return {Load, Release}; }
    void Triple(std::string_view map, const std::string& wave = waveA, const std::string& round = roundA, const std::string& types = typeA)
    {
        files[WavePath(map)] = {wave}; files[RoundPath(map)] = {round}; files[TypePath(map)] = {types};
    }
    void ResetCalls()
    {
        Check(allocated.empty(), "no live engine allocations between operations");
        loads.clear(); inputLengths.clear(); frees = 0; unchanged = validRelease = true;
    }
    static unsigned char* Load(const char* path, int* length)
    {
        auto& self = *active;
        self.loads.emplace_back(path); self.inputLengths.push_back(*length);
        const auto found = self.files.find(path);
        if (found == self.files.end()) return nullptr;
        const auto& file = found->second;
        if (file.writeLength) *length = file.reported.value_or(static_cast<int>(file.bytes.size()));
        if (!file.present) return nullptr;
        auto data = std::make_unique<unsigned char[]>(file.bytes.size() + 1);
        std::memcpy(data.get(), file.bytes.data(), file.bytes.size());
        data[file.bytes.size()] = 0; // Engine adds this byte but does not count it.
        auto* raw = data.get();
        self.allocated.emplace(raw, Allocation{std::move(data), file.bytes + '\0'});
        return raw;
    }
    static void Release(void* bytes)
    {
        auto& self = *active; ++self.frees;
        const auto found = self.allocated.find(bytes);
        if (found == self.allocated.end()) { self.validRelease = false; return; }
        auto& allocation = found->second;
        self.unchanged = self.unchanged && std::memcmp(bytes, allocation.original.data(), allocation.original.size()) == 0;
        // Poison released memory and retain it so later table checks detect any
        // borrowed engine data instead of accidentally reading unchanged bytes.
        std::memset(bytes, 0xdd, allocation.original.size());
        self.retired.push_back(std::move(allocation.bytes));
        self.allocated.erase(found);
    }
};
Engine* Engine::active{};

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
        if (x.recordIndex != y.recordIndex || x.waveId != y.waveId || x.spawnPoint != y.spawnPoint || x.startTime != y.startTime ||
            x.endTime != y.endTime || x.interval != y.interval || x.types != y.types || x.weights != y.weights ||
            x.weightTotal != y.weightTotal || x.eventMessage != y.eventMessage) return false;
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
bool Same(const MapTables& a, const MapTables& b)
{
    const auto sameTypes = [](const MonsterTypeTable& x, const MonsterTypeTable& y)
    {
        if (!Same(x.source, y.source) || x.byId != y.byId || x.rows.size() != y.rows.size()) return false;
        for (std::size_t i = 0; i < x.rows.size(); ++i)
        {
            const auto& p = x.rows[i]; const auto& q = y.rows[i];
            if (p.recordIndex != q.recordIndex || p.id != q.id || p.appearance != q.appearance ||
                p.attackDamage != q.attackDamage || p.attackRange != q.attackRange || p.attackInterval != q.attackInterval ||
                p.health != q.health || p.armor != q.armor || p.speed != q.speed || p.bonusMoney != q.bonusMoney ||
                p.priority != q.priority || p.skill != q.skill || p.color != q.color || p.killType != q.killType ||
                p.eventClass != q.eventClass || p.propensity != q.propensity || p.clearPoint != q.clearPoint ||
                p.dropItems != q.dropItems || p.experience != q.experience || p.column18 != q.column18 ||
                p.column19 != q.column19 || p.column20 != q.column20 || p.column21 != q.column21 || p.column22 != q.column22) return false;
        }
        return true;
    };
    return a.mapName == b.mapName && Same(a.waves, b.waves) && Same(a.rounds, b.rounds) && sameTypes(a.monsterTypes, b.monsterTypes);
}
void Calls(const Engine& engine, const std::vector<std::string>& paths, std::size_t frees)
{
    Check(engine.loads == paths, "exact relative file paths and order");
    Check(std::all_of(engine.inputLengths.begin(), engine.inputLengths.end(), [](int n) { return n == 0; }), "length initialized before every load callback");
    Check(engine.frees == frees && engine.allocated.empty() && engine.validRelease, "every nonnull load released exactly once and null never released");
    Check(engine.unchanged, "engine ciphertext and appended NUL never modified");
}
void Lifecycle()
{
    Engine engine; MapResources resources; ResourceError error{"stale", {99, "stale"}};
    Check(!resources.Reload(engine.Api(), error) && error.path.empty() && error.detail.message == "no scenario map session", "reload without session fails with fresh error");
    Calls(engine, {}, 0);
    engine.Triple("zs_alpha");
    Check(resources.BeginMap("zs_alpha", engine.Api(), error), "first map imports all three tables");
    Check(error.path.empty() && error.detail.message.empty() && error.detail.byteOffset == 0, "successful import clears previous error");
    Calls(engine, {WavePath("zs_alpha"), RoundPath("zs_alpha"), TypePath("zs_alpha")}, 3);
    Check(resources.MapName() == "zs_alpha" && resources.Tables() && resources.Tables()->mapName == "zs_alpha", "session and table map identities match");
    if (!resources.Tables()) return;
    Check(resources.Tables()->waves.rows[0].waveId == 1 && resources.Tables()->rounds.rows[0].waves == std::vector<int>{1} &&
        resources.Tables()->monsterTypes.rows.size() == 1 && resources.Tables()->monsterTypes.Find(1)->appearance == "zombi_host", "all three owned tables remain correct after release poisoning");
    const auto before = *resources.Tables(); const auto* address = resources.Tables();
    engine.ResetCalls(); engine.files[WavePath("zs_alpha")].bytes = waveB; engine.files.erase(RoundPath("zs_alpha"));
    Check(!resources.Reload(engine.Api(), error), "second table missing prevents commit");
    Calls(engine, {WavePath("zs_alpha"), RoundPath("zs_alpha")}, 1);
    Check(resources.Tables() == address && Same(*resources.Tables(), before), "wave-success round-failure retains complete old triple and table identity");
    Check(error.path == RoundPath("zs_alpha") && error.detail.message == "scenario configuration file is missing", "failure diagnostic identifies exact round path");
    engine.ResetCalls(); engine.files[RoundPath("zs_alpha")] = {roundB};
    engine.files.erase(TypePath("zs_alpha"));
    Check(!resources.Reload(engine.Api(), error), "third table missing prevents committing first two changed tables");
    Calls(engine, {WavePath("zs_alpha"), RoundPath("zs_alpha"), TypePath("zs_alpha")}, 2);
    Check(resources.Tables() == address && Same(*resources.Tables(), before), "type failure retains complete old triple and identity");
    Check(error.path == TypePath("zs_alpha") && error.detail.message == "scenario configuration file is missing", "type failure reports case-preserved original resource path");
    engine.ResetCalls(); engine.files[TypePath("zs_alpha")] = {typeB};
    Check(resources.Reload(engine.Api(), error), "successful same-session reload replaces triple");
    Calls(engine, {WavePath("zs_alpha"), RoundPath("zs_alpha"), TypePath("zs_alpha")}, 3);
    Check(resources.Tables()->waves.rows[0].waveId == 2 && resources.Tables()->rounds.rows[0].waves == std::vector<int>{2} &&
        resources.Tables()->monsterTypes.Find(22) && resources.Tables()->monsterTypes.Find(22)->id == 2 &&
        resources.Tables()->monsterTypes.Find(22)->dropItems == std::vector<std::int32_t>{2, 3} &&
        resources.Tables()->monsterTypes.Find(22)->column21 == std::array<float, 3>{4, 5, 6}, "reload commits all three new tables and owns complete type extensions");
    engine.ResetCalls(); engine.files.erase(WavePath("zs_alpha"));
    const auto alias = resources.MapName();
    Check(!resources.BeginMap(alias, engine.Api(), error) && resources.MapName() == "zs_alpha" && !resources.Tables(), "same-map restart via MapName alias clears previous tables before failure");
    Calls(engine, {WavePath("zs_alpha")}, 0);
    engine.ResetCalls(); engine.Triple("zs_alpha");
    Check(resources.Reload(engine.Api(), error) && resources.Tables(), "failed new session can recover via Reload");
    engine.ResetCalls(); engine.Triple("zs_beta", waveB, roundB, typeB);
    Check(resources.BeginMap("zs_beta", engine.Api(), error) && resources.MapName() == "zs_beta" && resources.Tables()->mapName == "zs_beta", "new valid map replaces session");
    Calls(engine, {WavePath("zs_beta"), RoundPath("zs_beta"), TypePath("zs_beta")}, 3);
    engine.ResetCalls();
    Check(!resources.BeginMap("zs_missing", engine.Api(), error) && resources.MapName() == "zs_missing" && !resources.Tables(), "new missing map never reuses last map tables");
    Calls(engine, {WavePath("zs_missing")}, 0);
    resources.Clear(); resources.Clear();
    Check(resources.MapName().empty() && !resources.Tables(), "Clear is complete and repeatable");
    engine.ResetCalls();
    Check(!resources.Reload(engine.Api(), error), "Clear ends Reload session"); Calls(engine, {}, 0);
}
void Failures()
{
    for (std::size_t failingFile = 0; failingFile < 3; ++failingFile)
    {
        for (int scenario = 0; scenario < 9; ++scenario)
        {
            Engine engine; MapResources resources; ResourceError error;
            engine.Triple("zs_test"); Check(resources.BeginMap("zs_test", engine.Api(), error), "failure fixture begins valid session");
            const auto before = *resources.Tables(); const auto* address = resources.Tables();
            engine.ResetCalls();
            const std::vector paths{WavePath("zs_test"), RoundPath("zs_test"), TypePath("zs_test")};
            const auto& path = paths[failingFile];
            auto& file = engine.files[path];
            if (scenario == 0) { file.present = false; file.reported = 123; }
            if (scenario == 1) { file.present = false; file.writeLength = false; }
            if (scenario == 2) file.reported = 0;
            if (scenario == 3) file.reported = -8;
            if (scenario == 4) file.reported = 7;
            if (scenario == 5) file.reported = 8 * 1024 * 1024 + 8;
            if (scenario == 6) { file.bytes.clear(); file.writeLength = false; }
            if (scenario == 7) file.bytes = Unhex("f15d9c83a14c4e63"); // Decodes to only NULs.
            if (scenario == 8) file.bytes = failingFile ? waveA : roundA; // Wrong table header.
            Check(!resources.Reload(engine.Api(), error), "missing size and parser failures reject load");
            Check(error.path == path && !error.detail.message.empty(), "each failure reports its exact resource path");
            Check(resources.Tables() == address && Same(*resources.Tables(), before), "every failure preserves all three complete previous tables");
            Calls(engine, std::vector<std::string>(paths.begin(), paths.begin() + failingFile + 1), failingFile + (scenario < 2 ? 0U : 1U));
        }
    }
}
void NamesAndApi()
{
    for (const std::string& name : std::vector<std::string>{"", ".", "..", "../zs_test", "maps/zs_test", "zs\\test", "C:zs_test", "zs test", "zs_test.cso", std::string(64, 'a'), std::string("zs\0test", 7), "\xe6\xb5\x8b\xe8\xaf\x95"})
    {
        Engine engine; MapResources resources; ResourceError error;
        engine.Triple("prior"); Check(resources.BeginMap("prior", engine.Api(), error), "invalid-name fixture begins populated session"); engine.ResetCalls();
        Check(!resources.BeginMap(name, engine.Api(), error) && resources.MapName().empty() && !resources.Tables(), "invalid basename clears old session and is rejected");
        Check(error.path.empty() && !error.detail.message.empty(), "invalid basename has no requested file path"); Calls(engine, {}, 0);
    }
    for (const std::string& name : std::vector<std::string>{"a", "AZaz_09-", std::string(63, 'b')})
    {
        Engine engine; MapResources resources; ResourceError error; engine.Triple(name);
        Check(resources.BeginMap(name, engine.Api(), error) && resources.MapName() == name, "valid ASCII basename lengths and case preserved");
        Calls(engine, {WavePath(name), RoundPath(name), TypePath(name)}, 3);
    }
    for (int missing = 0; missing < 3; ++missing)
    {
        Engine engine; MapResources resources; ResourceError error; engine.Triple("valid");
        Check(resources.BeginMap("valid", engine.Api(), error), "invalid-api fixture loads valid triple");
        const auto before = *resources.Tables(); engine.ResetCalls();
        auto api = engine.Api(); if (missing != 1) api.load = nullptr; if (missing != 0) api.release = nullptr;
        Check(!resources.Reload(api, error) && Same(*resources.Tables(), before), "invalid API Reload preserves complete triple"); Calls(engine, {}, 0);
        Check(!resources.BeginMap("new", api, error) && resources.MapName() == "new" && !resources.Tables(), "invalid API BeginMap still discards previous session"); Calls(engine, {}, 0);
    }
}
std::string Read(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot open actual common resource");
    return {std::istreambuf_iterator<char>(file), {}};
}
void Actual(const char* mapName, const char* wavePath, const char* roundPath, const char* typePath,
    std::size_t waveRows, std::size_t roundRows, std::size_t typeRows)
{
    const auto wave = Read(wavePath), round = Read(roundPath), types = Read(typePath);
    MapTables expected; expected.mapName = mapName; ConfigError detail;
    Check(ReadWaveResource(std::as_bytes(std::span(wave.data(), wave.size())), expected.waves, detail), "actual raw wave baseline imports");
    Check(ReadRoundResource(std::as_bytes(std::span(round.data(), round.size())), expected.rounds, detail), "actual raw round baseline imports");
    Check(ReadMonsterTypeResource(std::as_bytes(std::span(types.data(), types.size())), expected.monsterTypes, detail), "actual raw type baseline imports");
    Engine engine; MapResources resources; ResourceError error; engine.Triple(mapName, wave, round, types);
    Check(resources.BeginMap(mapName, engine.Api(), error), "actual common triple loads with engine extra NUL excluded");
    Calls(engine, {WavePath(mapName), RoundPath(mapName), TypePath(mapName)}, 3);
    Check(resources.Tables() && resources.Tables()->waves.rows.size() == waveRows && resources.Tables()->rounds.rows.size() == roundRows &&
        resources.Tables()->monsterTypes.rows.size() == typeRows, "actual common triple matches independently counted wave round and type rows");
    Check(resources.Tables() && Same(*resources.Tables(), expected), "engine-read whole source bytes fields typed rows and groups remain exact after release");
    const auto* address = resources.Tables(); engine.ResetCalls(); engine.files.erase(TypePath(mapName));
    Check(!resources.Reload(engine.Api(), error) && error.path == TypePath(mapName), "actual common missing type refuses incomplete reload");
    Calls(engine, {WavePath(mapName), RoundPath(mapName), TypePath(mapName)}, 2);
    Check(resources.Tables() == address && Same(*resources.Tables(), expected), "actual common failed reload retains every field of all three tables");
    engine.files[TypePath(mapName)] = {types};
    engine.ResetCalls();
    Check(resources.BeginMap(resources.MapName(), engine.Api(), error), "actual common same-map restart safely accepts MapName alias");
    Calls(engine, {WavePath(mapName), RoundPath(mapName), TypePath(mapName)}, 3);
    Check(Same(*resources.Tables(), expected), "actual restarted triple still owns full parsed content");
    resources.Clear(); Check(!resources.Tables() && resources.MapName().empty(), "actual resources clear without retaining engine buffers");
}
}
int main(int argc, char** argv)
{
    try
    {
        if (argc == 1) { Lifecycle(); Failures(); NamesAndApi(); }
        else if (argc == 9 && std::string_view(argv[1]) == "--actual")
            Actual(argv[2], argv[3], argv[4], argv[5], std::stoul(argv[6]), std::stoul(argv[7]), std::stoul(argv[8]));
        else throw std::runtime_error("usage: map_resources_tests [--actual map-name common-wave common-round common-types expected-waves expected-rounds expected-types]");
    }
    catch (const std::exception& error) { ++failures; std::cerr << "EXCEPTION: " << error.what() << '\n'; }
    std::cout << "map_resources checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
