// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "native/csol/server/weapons/guest_cof_resources.hpp"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

enginefuncs_t g_engfuncs{};
bool g_bAllowedCSBot{};
namespace
{
using namespace csol::server::cof;
struct Fixture
{
    std::map<std::string, std::vector<byte>> files;
    std::map<std::string, int> models, sounds, generics, events, strings;
    std::vector<std::string> calls;
    std::string missing, failPath;
    int steps{}, cancelAt{-1}, rejectAt{-1}, loads{}, frees{}, precaches{};
    bool active{true};
    bool Step(const char* kind, const char* path)
    {
        calls.push_back(std::string(kind) + ":" + path);
        ++steps;
        if (steps == cancelAt) active = false;
        return steps != rejectAt;
    }
    void Reset()
    {
        calls.clear(); steps = loads = frees = precaches = 0;
        cancelAt = rejectAt = -1; active = true; missing.clear();
    }
} fixture;
unsigned checks{}, failures{};
void Check(bool value, const char* message)
{
    ++checks;
    if (!value) { ++failures; std::printf("FAIL %s\n", message); }
}
bool Current(void* context) noexcept { return context == &fixture && fixture.active; }
int FileSize(const char* path)
{
    if (!fixture.Step("size", path) || fixture.missing == path) return -1;
    const auto found = fixture.files.find(path);
    return found == fixture.files.end() ? -1 : static_cast<int>(found->second.size());
}
byte* Load(const char* path, int* size)
{
    ++fixture.loads;
    if (!fixture.Step("load", path)) { *size = 0; return nullptr; }
    const auto found = fixture.files.find(path);
    if (found == fixture.files.end()) { *size = 0; return nullptr; }
    *size = static_cast<int>(found->second.size());
    auto* value = static_cast<byte*>(std::malloc(found->second.size()));
    if (!value) std::abort();
    std::memcpy(value, found->second.data(), found->second.size());
    return value;
}
void Free(void* bytes)
{
    ++fixture.frees;
    fixture.Step("free", "");
    std::free(bytes);
}
int Cache(const char* kind, const char* path, std::map<std::string, int>& cache, bool zero)
{
    ++fixture.precaches;
    if (!fixture.Step(kind, path)) return zero ? -1 : 0;
    const auto [it, added] = cache.try_emplace(path, static_cast<int>(cache.size()) + (zero ? 0 : 1));
    return it->second;
}
int Model(const char* path) { return Cache("model", path, fixture.models, false); }
int Sound(const char* path) { return Cache("sound", path, fixture.sounds, false); }
int Generic(const char* path) { return Cache("generic", path, fixture.generics, true); }
unsigned short Event(int type, const char* path)
{
    Check(type == 1, "actual event type1");
    return static_cast<unsigned short>(Cache("event", path, fixture.events, false));
}
int String(const char* path) { return Cache("string", path, fixture.strings, false); }

std::vector<byte> Read(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("missing real input: " + path.string());
    return {std::istreambuf_iterator<char>(file), {}};
}
void Inputs(const std::filesystem::path& original, const std::filesystem::path& clip)
{
    for (const auto& p : P345Models)
        fixture.files.emplace(p.host, Read(std::string_view(p.original) == "models/w_9mmclip.mdl" ?
            clip : original / p.original));
    for (const auto& p : P345Sounds)
        fixture.files.emplace("sound/" + std::string(p.host), Read(original / "sound" / p.original));
    for (const auto& p : P345LocalWaves)
        fixture.files.emplace(p.host, Read(original / "sound" / p.original));
    for (const auto& p : P345Events) fixture.files.emplace(p.host, Read(original / p.original));
    fixture.files.emplace(P345Inventory.host, Read(original / P345Inventory.original));
    fixture.files.emplace(P345Particle.host, Read(original / P345Particle.original));
}
std::uint32_t Word(const std::vector<byte>& bytes, std::size_t at)
{
    std::uint32_t value{}; std::memcpy(&value, bytes.data() + at, 4); return value;
}
bool Unchanged(const P345Resources& a, const P345Resources& b)
{
    // SDK Vector and the aggregate have padding; semantic transaction checks
    // must not depend on a compiler copying indeterminate padding bytes.
    return a.generation == b.generation && a.sequences.duration == b.sequences.duration &&
        a.sequences.present == b.sequences.present && a.aimOffset.x == b.aimOffset.x &&
        a.aimOffset.y == b.aimOffset.y && a.aimOffset.z == b.aimOffset.z && a.aimZoom == b.aimZoom &&
        a.models == b.models && a.sounds == b.sounds && a.localWaveDownloads == b.localWaveDownloads &&
        a.events == b.events && a.viewModelString == b.viewModelString &&
        a.playerModelString == b.playerModelString && a.worldModelString == b.worldModelString &&
        a.inventoryItemString == b.inventoryItemString && a.particleScriptString == b.particleScriptString &&
        a.inventoryDownload == b.inventoryDownload && a.particleDownload == b.particleDownload;
}
}

int main(int argc, char** argv)
{
    if (argc != 3) return 2;
    try { Inputs(argv[1], argv[2]); }
    catch (const std::exception& e) { std::printf("%s\n", e.what()); return 3; }
    g_engfuncs.pfnGetFileSize = FileSize;
    g_engfuncs.pfnLoadFileForMe = Load; g_engfuncs.pfnFreeFile = Free;
    g_engfuncs.pfnPrecacheModel = Model; g_engfuncs.pfnPrecacheSound = Sound;
    g_engfuncs.pfnPrecacheGeneric = Generic; g_engfuncs.pfnPrecacheEvent = Event;
    g_engfuncs.pfnAllocString = String;
    static_assert(sizeof(studiohdr_t) == 244 && sizeof(mstudioseqdesc_t) == 176);
    static_assert(offsetof(studiohdr_t, numseq) == 164 && offsetof(studiohdr_t, seqindex) == 168);
    static_assert(offsetof(mstudioseqdesc_t, fps) == 32 && offsetof(mstudioseqdesc_t, numframes) == 56);
    ResourceAuthority authority{&fixture, Current, 17};
    P345Resources prepared{};
    const auto result = PrepareP345Resources(authority, prepared);
    Check(bool(result), "complete real resource preparation");
    if (!result) { std::printf("status%d path%s\n", int(result.status), result.path ? result.path : ""); return 4; }
    const int steps = fixture.steps;
    const auto calls = fixture.calls;
    Check(fixture.loads == 2 && fixture.frees == 2, "only two cold reads, both released");
    Check(prepared.generation == 17, "generation retained");
    Check(std::bit_cast<std::uint32_t>(prepared.aimOffset.x) == 0x3d4ccccd &&
        prepared.aimOffset.y == -6 && prepared.aimOffset.z == 4.5f && prepared.aimZoom == 50,
        "original aim field order and exact float");
    const auto& view = fixture.files.at(P345Models[0].host);
    const auto start = Word(view, 168);
    for (unsigned i = 0; i < 34; ++i)
    {
        const auto at = start + i * sizeof(mstudioseqdesc_t);
        const int frames = std::bit_cast<int>(Word(view, at + 56));
        const float fps = std::bit_cast<float>(Word(view, at + 32));
        const float expected = frames <= 1 ? 0.f : static_cast<float>(frames - 1) / fps;
        Check(std::bit_cast<std::uint32_t>(prepared.sequences.duration[i]) ==
            std::bit_cast<std::uint32_t>(expected), "full real view model durations");
        Check((prepared.sequences.present & (std::uint64_t{1} << i)) != 0, "sequence present");
    }
    for (std::size_t i = 0; i < P345Models.size(); ++i)
        Check(fixture.models.at(P345Models[i].host) == prepared.models[i], "actual model ID preserved");
    for (std::size_t i = 0; i < P345Sounds.size(); ++i)
        Check(fixture.sounds.at(P345Sounds[i].host) == prepared.sounds[i], "actual sound ID preserved");
    for (std::size_t i = 0; i < P345LocalWaves.size(); ++i)
        Check(fixture.generics.at(P345LocalWaves[i].host) == prepared.localWaveDownloads[i], "actual generic ID preserved");
    Check(prepared.localWaveDownloads[0] == 0, "zero based generic index accepted");
    Check(fixture.strings.at(P345Models[0].host) == prepared.viewModelString &&
        fixture.strings.at(P345Models[1].host) == prepared.worldModelString &&
        fixture.strings.at(P345Models[2].host) == prepared.playerModelString &&
        fixture.strings.at(P345Inventory.host) == prepared.inventoryItemString &&
        fixture.strings.at(P345Particle.host) == prepared.particleScriptString, "actual namespaced STRING IDs");
    Check(std::string_view(P345Models[2].original) == "police/models/weapons/p345/p_p345.mdl",
        "full original police P identity");
    Check(std::string_view(P345Sounds[12].original) == "player/police/Swing/Swing1.wav" &&
        std::string_view(P345Sounds[6].original) == "weapons/p345/p345_fire.wav" &&
        std::string_view(P345LocalWaves[6].original) == "weapons_new/p345/p345_fire.wav",
        "voice remote local logical namespaces retained");
    for (const auto& [path, bytes] : fixture.files)
        Check(path.size() < 64 && !bytes.empty(), "bounded actual VFS dependency");

    P345Resources sentinel;
    std::memset(static_cast<void*>(&sentinel), 0x5a, sizeof(sentinel));
    // Every missing static resource rejects before the first precache callback.
    for (const auto& [path, bytes] : fixture.files)
    {
        fixture.Reset(); fixture.missing = path; auto output = sentinel;
        const auto r = PrepareP345Resources(authority, output);
        Check(r.status == ResourceStatus::MissingFile && Unchanged(output, sentinel), "missing resource transactional rejection");
        Check(!fixture.precaches && !fixture.loads, "closure preflight before precache or body load");
    }
    // Revoke map/content authority from every engine edge, including FREE_FILE.
    for (int edge = 1; edge <= steps; ++edge)
    {
        fixture.Reset(); fixture.cancelAt = edge; auto output = sentinel;
        const auto r = PrepareP345Resources(authority, output);
        Check(r.status == ResourceStatus::Cancelled && Unchanged(output, sentinel), "every callback cancellation retains output");
        Check(fixture.frees <= fixture.loads && fixture.steps <= edge + 1, "no later engine operation except file release");
    }
    // Check actual engine failure conventions separately from revocation.
    for (int edge = 1; edge <= steps; ++edge)
    {
        if (calls[edge - 1].starts_with("free:")) continue;
        fixture.Reset(); fixture.rejectAt = edge; auto output = sentinel;
        const auto r = PrepareP345Resources(authority, output);
        Check(!r && Unchanged(output, sentinel), "engine failure retains output");
    }
    fixture.Reset();
    auto output = sentinel;
    Check(PrepareP345Resources({&fixture, nullptr, 17}, output).status == ResourceStatus::InvalidAuthority &&
        Unchanged(output, sentinel) && !fixture.steps, "absent authority rejected");
    Check(PrepareP345Resources({&fixture, Current, 0}, output).status == ResourceStatus::InvalidAuthority &&
        Unchanged(output, sentinel) && !fixture.steps, "zero generation rejected");
    const auto engine = g_engfuncs;
    g_engfuncs.pfnPrecacheSound = nullptr;
    Check(PrepareP345Resources(authority, output).status == ResourceStatus::MissingEngine &&
        Unchanged(output, sentinel), "absent actual engine rejected");
    g_engfuncs = engine;

    const auto metadata = fixture.files.at(P345Inventory.host);
    const std::string valid(metadata.begin(), metadata.end());
    const auto replace = [&](const char* from, const char* to) {
        auto text = valid; const auto at = text.find(from);
        if (at == text.npos) std::abort(); text.replace(at, std::strlen(from), to); return text;
    };
    const std::vector<std::string> bad{
        "", valid + "\"unfinished", valid + "\n\"ironsight_zoom\"{data \"50\"}",
        replace("\"0.05\"", "\"nan\""), replace("\"0.05\"", "\"1e999\""),
        replace("\"0.05\"", "\"0.05junk\""), replace("\"50\"", "\"256\""),
        replace("\"50\"", "\"-1\""), replace("\"50\"", "\"50.0\""),
        replace("weapon_p345", "weapon_glock"), replace("ironsight_up", "missing_up"),
        replace("data", "bad"), valid + std::string(1, '\0'), valid + "\ntrailing",
        replace("\"4.5\"", "\"inf\"")
    };
    for (const auto& text : bad)
    {
        fixture.Reset(); fixture.files[P345Inventory.host] = {text.begin(), text.end()};
        output = sentinel; const auto r = PrepareP345Resources(authority, output);
        Check((r.status == ResourceStatus::InvalidMetadata || (text.empty() && r.status == ResourceStatus::MissingFile)) &&
            Unchanged(output, sentinel) && !fixture.precaches, "strict malformed metadata rejects before precache");
        Check(fixture.frees == fixture.loads, "malformed metadata released");
    }
    fixture.files[P345Inventory.host] = metadata;
    const auto model = fixture.files.at(P345Models[0].host);
    for (const auto at : {0u, 4u, 72u, 164u, 168u})
    {
        fixture.Reset(); fixture.files[P345Models[0].host] = model;
        std::fill_n(fixture.files[P345Models[0].host].begin() + at, 4, 0);
        output = sentinel; const auto r = PrepareP345Resources(authority, output);
        Check(r.status == ResourceStatus::InvalidModel && Unchanged(output, sentinel) && !fixture.precaches,
            "malformed complete view model rejected before precache");
        Check(fixture.loads == fixture.frees, "malformed model released");
    }
    fixture.files[P345Models[0].host] = model;
    fixture.Reset(); P345Resources second{};
    Check(bool(PrepareP345Resources(authority, second)), "repeat cold preparation");
    Check(second.models == prepared.models && second.sounds == prepared.sounds &&
        second.events == prepared.events && second.localWaveDownloads == prepared.localWaveDownloads &&
        second.inventoryItemString == prepared.inventoryItemString, "normal engine cache stable IDs");
    std::printf("%u real SDK cold preparation checks, %u failures; %zu real files, %d callback edges; no game/backend/voice/grip admission\n",
        checks, failures, fixture.files.size(), steps);
    return failures ? 5 : 0;
}
