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
#include <type_traits>
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
void Inputs(const std::filesystem::path& original, const std::filesystem::path& clip,
    const std::filesystem::path& clipTexture, const auto& models, const auto& sounds,
    const auto& waves, const auto& events, ResourcePath inventory, ResourcePath particle, ResourcePath texture)
{
    for (const auto& p : models)
        fixture.files.emplace(p.host, Read(std::string_view(p.original) == "models/w_9mmclip.mdl" ?
            clip : original / p.original));
    for (const auto& p : sounds)
        fixture.files.emplace("sound/" + std::string(p.host), Read(original / "sound" / p.original));
    for (const auto& p : waves)
        fixture.files.emplace(p.host, Read(original / "sound" / p.original));
    for (const auto& p : events) fixture.files.emplace(p.host, Read(original / p.original));
    fixture.files.emplace(inventory.host, Read(original / inventory.original));
    fixture.files.emplace(particle.host, Read(original / particle.original));
    fixture.files.emplace(texture.host, Read(clipTexture));
}
std::uint32_t Word(const std::vector<byte>& bytes, std::size_t at)
{
    std::uint32_t value{}; std::memcpy(&value, bytes.data() + at, 4); return value;
}
template<class Resources>
bool Unchanged(const Resources& a, const Resources& b)
{
    if constexpr (requires { a.decalDownloads; })
        if (a.decalDownloads != b.decalDownloads) return false;
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
    if (argc != 4 && argc != 5) return 2;
    g_engfuncs.pfnGetFileSize = FileSize;
    g_engfuncs.pfnLoadFileForMe = Load; g_engfuncs.pfnFreeFile = Free;
    g_engfuncs.pfnPrecacheModel = Model; g_engfuncs.pfnPrecacheSound = Sound;
    g_engfuncs.pfnPrecacheGeneric = Generic; g_engfuncs.pfnPrecacheEvent = Event;
    g_engfuncs.pfnAllocString = String;
    static_assert(sizeof(studiohdr_t) == 244 && sizeof(mstudioseqdesc_t) == 176);
    static_assert(offsetof(studiohdr_t, numseq) == 164 && offsetof(studiohdr_t, seqindex) == 168);
    static_assert(offsetof(mstudioseqdesc_t, fps) == 32 && offsetof(mstudioseqdesc_t, numframes) == 56);
    static_assert(offsetof(studiohdr_t, numtextures) == 180 && offsetof(studiohdr_t, textureindex) == 184 &&
        offsetof(studiohdr_t, texturedataindex) == 188 && offsetof(studiohdr_t, numskinref) == 192 &&
        offsetof(studiohdr_t, numskinfamilies) == 196 && offsetof(studiohdr_t, skinindex) == 200);
    static_assert(sizeof(mstudiotexture_t) == 80 && offsetof(mstudiotexture_t, width) == 68 &&
        offsetof(mstudiotexture_t, height) == 72 && offsetof(mstudiotexture_t, index) == 76);
    const auto exercise = [&]<class Resources>(const auto& models, const auto& sounds, const auto& waves,
        const auto& events, ResourcePath inventory, ResourcePath particle, ResourcePath texturePath,
        auto prepare, unsigned sequenceCount, float expectedRight, float expectedUp,
        const char* classname, const char* upText, const char* family)
    {
        const auto checkStart = checks, failureStart = failures;
        ResourceAuthority authority{&fixture, Current, 17};
        Resources prepared{};
        const auto result = prepare(authority, prepared);
        Check(bool(result), "complete real resource preparation");
        if (!result) { std::printf("status%d path%s\n", int(result.status), result.path ? result.path : ""); return; }
        const int steps = fixture.steps;
        const auto calls = fixture.calls;
        Check(fixture.loads == 3 && fixture.frees == 3, "three cold reads, all released");
        constexpr auto decalCount = std::is_same_v<Resources, GlockResources> ? ShotDecals.size() : 0;
        Check(fixture.models.size() == 10 && fixture.generics.size() == waves.size() + 2 + decalCount &&
            !fixture.generics.contains(texturePath.host), "engine retains sole automatic companion registration");
        Check(std::string_view(models[8].original) == "models/flash1.mdl" &&
            std::string_view(models[9].original) == "sprites/gunsmoke.spr" &&
            prepared.models[8] > 0 && prepared.models[9] > 0,
            "real muzzle model and cheap smoke sprite admitted before input");
        Check(prepared.generation == 17, "generation retained");
        Check(std::bit_cast<std::uint32_t>(prepared.aimOffset.x) == 0x3d4ccccd &&
            prepared.aimOffset.y == expectedRight && prepared.aimOffset.z == expectedUp && prepared.aimZoom == 50,
            "original aim field order and exact float");
        const auto& view = fixture.files.at(models[0].host);
        const auto start = Word(view, 168);
        for (unsigned i = 0; i < sequenceCount; ++i)
        {
            const auto at = start + i * sizeof(mstudioseqdesc_t);
            const int frames = std::bit_cast<int>(Word(view, at + 56));
            const float fps = std::bit_cast<float>(Word(view, at + 32));
            const float expected = frames <= 1 ? 0.f : static_cast<float>(frames - 1) / fps;
            Check(std::bit_cast<std::uint32_t>(prepared.sequences.duration[i]) ==
                std::bit_cast<std::uint32_t>(expected), "full real view model durations");
            Check((prepared.sequences.present & (std::uint64_t{1} << i)) != 0, "sequence present");
        }
        for (std::size_t i = 0; i < models.size(); ++i)
            Check(fixture.models.at(models[i].host) == prepared.models[i], "actual model ID preserved");
        for (std::size_t i = 0; i < sounds.size(); ++i)
            Check(fixture.sounds.at(sounds[i].host) == prepared.sounds[i], "actual sound ID preserved");
        for (std::size_t i = 0; i < waves.size(); ++i)
            Check(fixture.generics.at(waves[i].host) == prepared.localWaveDownloads[i], "actual generic ID preserved");
        Check(prepared.localWaveDownloads[0] == 0, "zero based generic index accepted");
        if constexpr (requires { prepared.decalDownloads; })
            for (std::size_t i = 0; i < ShotDecals.size(); ++i)
                Check(prepared.decalDownloads[i] == fixture.generics.at(ShotDecals[i].host),
                    "actual five shot generic indices preserved");
        Check(fixture.strings.at(models[0].host) == prepared.viewModelString &&
            fixture.strings.at(models[1].host) == prepared.worldModelString &&
            fixture.strings.at(models[2].host) == prepared.playerModelString &&
            fixture.strings.at(inventory.host) == prepared.inventoryItemString &&
            fixture.strings.at(particle.host) == prepared.particleScriptString, "actual namespaced STRING IDs");
        if constexpr (std::is_same_v<Resources, P345Resources>)
        {
            Check(std::string_view(models[2].original) == "police/models/weapons/p345/p_p345.mdl",
                "full original police P identity");
            Check(std::string_view(sounds[12].original) == "player/police/Swing/Swing1.wav" &&
                std::string_view(sounds[6].original) == "weapons/p345/p345_fire.wav" &&
                std::string_view(waves[6].original) == "weapons_new/p345/p345_fire.wav",
                "voice remote local logical namespaces retained");
        }
        else if constexpr (std::is_same_v<Resources, GlockResources>)
        {
            Check(std::string_view(models[2].original) == "police/models/weapons/glock/p_glock.mdl",
                "full original Glock police P identity");
            Check(std::string_view(sounds[21].original) == "weapons/weapon_fire_empty.wav" &&
                std::string_view(sounds[22].original) == "weapons/weapon_fire_empty_noclick.wav" &&
                std::string_view(sounds[7].original) == "weapons/rifle/rifle_insert.wav" &&
                std::string_view(waves[0].original) == "weapons_new/glock/glock_switch.wav",
                "Glock empty virtual variants rifle and local keys retained");
        }
        if constexpr (std::is_same_v<Resources, RevolverResources>)
        {
            Check(std::string_view(models[2].original) == "police/models/weapons/revolver/p_revolver.mdl",
                "original Revolver police P identity");
            Check(events.size() == 3 && std::string_view(events[2].original) == "events/revolvershells.sc" &&
                std::string_view(sounds[1].original) == "weapons/revolver/revolver_shell.wav" && waves.size() == 4,
                "original delayed native shell sound is distinct from four local samples");
        }
        if constexpr (std::is_same_v<Resources, VP70Resources>)
        {
            Check(std::string_view(models[2].original) == "police/models/weapons/vp70/p_vp70.mdl" &&
                std::string_view(sounds[unsigned(VP70Sound::Fire)].original) == "weapons/vp70/vp70_fire.wav" &&
                std::string_view(waves[4].original) == "weapons_new/vp70/vp70_fire.wav" && events.size() == 2,
                "VP70 original multiplayer P and independent native/local sound identities");
        }
        for (std::size_t i = 0; i < events.size(); ++i)
            Check(fixture.events.at(events[i].host) == prepared.events[i], "actual event IDs preserved");
        Check(fixture.generics.at(inventory.host) == prepared.inventoryDownload &&
            fixture.generics.at(particle.host) == prepared.particleDownload, "actual metadata and particle generic IDs");
        for (const auto& [path, bytes] : fixture.files)
        {
            const auto registered=path.starts_with("sound/")&&fixture.sounds.contains(path.substr(6))?
                std::string_view(path).substr(6):std::string_view(path);
            Check(registered.size()<MAX_QPATH&&!bytes.empty(), "actual engine resource filename bound");
        }

        Resources sentinel;
        std::memset(static_cast<void*>(&sentinel), 0x5a, sizeof(sentinel));
        // Every missing static resource rejects before the first precache callback.
        for (const auto& [path, bytes] : fixture.files)
        {
            fixture.Reset(); fixture.missing = path; auto output = sentinel;
            const auto r = prepare(authority, output);
            Check(r.status == ResourceStatus::MissingFile && Unchanged(output, sentinel), "missing resource transactional rejection");
            Check(!fixture.precaches && !fixture.loads, "closure preflight before precache or body load");
        }
        // Revoke map/content authority from every engine edge, including FREE_FILE.
        for (int edge = 1; edge <= steps; ++edge)
        {
            fixture.Reset(); fixture.cancelAt = edge; auto output = sentinel;
            const auto r = prepare(authority, output);
            Check(r.status == ResourceStatus::Cancelled && Unchanged(output, sentinel), "every callback cancellation retains output");
            Check(fixture.frees <= fixture.loads && fixture.steps <= edge + 1, "no later engine operation except file release");
        }
        // Check actual engine failure conventions separately from revocation.
        for (int edge = 1; edge <= steps; ++edge)
        {
            if (calls[edge - 1].starts_with("free:")) continue;
            fixture.Reset(); fixture.rejectAt = edge; auto output = sentinel;
            const auto r = prepare(authority, output);
            Check(!r && Unchanged(output, sentinel), "engine failure retains output");
        }
        fixture.Reset();
        auto output = sentinel;
        Check(prepare({&fixture, nullptr, 17}, output).status == ResourceStatus::InvalidAuthority &&
            Unchanged(output, sentinel) && !fixture.steps, "absent authority rejected");
        Check(prepare({&fixture, Current, 0}, output).status == ResourceStatus::InvalidAuthority &&
            Unchanged(output, sentinel) && !fixture.steps, "zero generation rejected");
        const auto engine = g_engfuncs;
        g_engfuncs.pfnPrecacheSound = nullptr;
        Check(prepare(authority, output).status == ResourceStatus::MissingEngine &&
            Unchanged(output, sentinel), "absent actual engine rejected");
        g_engfuncs = engine;

        const auto metadata = fixture.files.at(inventory.host);
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
            replace(classname, "weapon_invalid"), replace("ironsight_up", "missing_up"),
            replace("data", "bad"), valid + std::string(1, '\0'), valid + "\ntrailing",
            replace(upText, "\"inf\"")
        };
        for (const auto& text : bad)
        {
            fixture.Reset(); fixture.files[inventory.host] = {text.begin(), text.end()};
            output = sentinel; const auto r = prepare(authority, output);
            Check((r.status == ResourceStatus::InvalidMetadata || (text.empty() && r.status == ResourceStatus::MissingFile)) &&
                Unchanged(output, sentinel) && !fixture.precaches, "strict malformed metadata rejects before precache");
            Check(fixture.frees == fixture.loads, "malformed metadata released");
        }
        fixture.files[inventory.host] = metadata;
        const auto model = fixture.files.at(models[0].host);
        for (const auto at : {0u, 4u, 72u, 164u, 168u})
        {
            fixture.Reset(); fixture.files[models[0].host] = model;
            std::fill_n(fixture.files[models[0].host].begin() + at, 4, 0);
            output = sentinel; const auto r = prepare(authority, output);
            Check(r.status == ResourceStatus::InvalidModel && Unchanged(output, sentinel) && !fixture.precaches,
                "malformed complete view model rejected before precache");
            Check(fixture.loads == fixture.frees, "malformed model released");
        }
        for (const auto size : {1u, 243u, unsigned(model.size() - 1)})
        {
            fixture.Reset(); fixture.files[models[0].host] = model;
            fixture.files[models[0].host].resize(size);
            output = sentinel;
            Check(prepare(authority, output).status == ResourceStatus::InvalidModel &&
                Unchanged(output, sentinel) && !fixture.precaches && fixture.loads == fixture.frees,
                "truncated full view model rejected and released before precache");
        }
        fixture.Reset(); fixture.files[models[0].host] = model;
        const auto tooFew = sequenceCount - 1;
        std::memcpy(fixture.files[models[0].host].data() + 164, &tooFew, sizeof(tooFew));
        output = sentinel;
        Check(prepare(authority, output).status == ResourceStatus::InvalidModel &&
            Unchanged(output, sentinel) && !fixture.precaches, "required complete sequence prefix rejected");
        fixture.files[models[0].host] = model;
        const auto texture = fixture.files.at(texturePath.host);
        const auto textureTable = Word(texture, 184);
        const auto rejectTexture = [&](std::vector<byte> bytes) {
            fixture.Reset(); fixture.files[texturePath.host] = std::move(bytes);
            output = sentinel; const auto r = prepare(authority, output);
            Check(r.status == ResourceStatus::InvalidModel && r.path == texturePath.host &&
                Unchanged(output, sentinel) && !fixture.precaches, "malformed texture closure rejects before precache");
            Check(fixture.loads == fixture.frees, "malformed texture closure released");
        };
        const auto changedWord = [&](std::size_t at, std::uint32_t value) {
            auto bytes = texture; std::memcpy(bytes.data() + at, &value, sizeof(value));
            rejectTexture(std::move(bytes));
        };
        for (const auto size : {1u, 243u, 244u, unsigned(texture.size() - 1)})
        { auto bytes = texture; bytes.resize(size); rejectTexture(std::move(bytes)); }
        for (const auto size : {unsigned(texture.size() - 1), unsigned(Word(texture, 188) + 3999)})
        {
            auto bytes = texture; bytes.resize(size);
            std::memcpy(bytes.data() + 72, &size, sizeof(size)); rejectTexture(std::move(bytes));
        }
        for (const auto at : {0u, 4u, 72u, 180u, 184u, 188u, 192u, 196u, 200u}) changedWord(at, 0);
        for (const auto at : {180u, 184u, 188u, 192u, 196u, 200u}) changedWord(at, 0xffffffffu);
        for (const auto at : {180u, 184u, 188u, 192u, 196u, 200u}) changedWord(at, 0x7fffffffu);
        changedWord(184, unsigned(texture.size() - 79));
        changedWord(200, unsigned(texture.size() - 1));
        changedWord(textureTable + 68, 0); changedWord(textureTable + 72, 0);
        changedWord(textureTable + 68, 0xffffffffu); changedWord(textureTable + 72, 0x7fffffffu);
        changedWord(textureTable + 68, 41); // Complete header/table, insufficient pixel/palette payload.
        changedWord(textureTable + 76, 243); changedWord(textureTable + 76, unsigned(texture.size() - 1));
        { auto bytes = texture; std::fill_n(bytes.begin() + textureTable, 64, 'x'); rejectTexture(std::move(bytes)); }
        { auto bytes = texture; const unsigned short invalid = 1;
          std::memcpy(bytes.data() + Word(texture, 200), &invalid, sizeof(invalid)); rejectTexture(std::move(bytes)); }
        // Valid user texture updates are not restricted to the candidate's SHA/size.
        fixture.Reset(); fixture.files[texturePath.host] = texture;
        fixture.files[texturePath.host].back() ^= 0x7f;
        fixture.files[texturePath.host].push_back(0);
        const auto updatedLength = int(fixture.files[texturePath.host].size());
        std::memcpy(fixture.files[texturePath.host].data() + 72, &updatedLength, sizeof(updatedLength));
        Check(bool(prepare(authority, output)), "structurally valid changed payload and complete size admitted");
        fixture.files[texturePath.host] = texture;
        fixture.Reset(); Resources second{};
        Check(bool(prepare(authority, second)), "repeat cold preparation");
        Check(second.models == prepared.models && second.sounds == prepared.sounds &&
            second.events == prepared.events && second.localWaveDownloads == prepared.localWaveDownloads &&
            second.inventoryItemString == prepared.inventoryItemString, "normal engine cache stable IDs");
        std::printf("%s: %u real SDK cold preparation checks, %u failures; %zu real files, %d callback edges; no game/backend/voice/grip admission\n",
            family, checks - checkStart, failures - failureStart, fixture.files.size(), steps);
    };
    try
    {
        Inputs(argv[1], argv[2], argv[3], P345Models, P345Sounds, P345LocalWaves, P345Events,
            P345Inventory, P345Particle, P345ClipTexture);
        exercise.template operator()<P345Resources>(P345Models, P345Sounds, P345LocalWaves, P345Events,
            P345Inventory, P345Particle, P345ClipTexture, PrepareP345Resources, 34, -6, 4.5f,
            "weapon_p345", "\"4.5\"", "P345");
        fixture = Fixture{};
        Inputs(argv[1], argv[2], argv[3], GlockModels, GlockSounds, GlockLocalWaves, GlockEvents,
            GlockInventory, GlockParticle, GlockClipTexture);
        for (const auto& resource : ShotDecals)
            fixture.files.emplace(resource.host, Read(std::filesystem::path(argv[1]) / resource.original));
        exercise.template operator()<GlockResources>(GlockModels, GlockSounds, GlockLocalWaves, GlockEvents,
            GlockInventory, GlockParticle, GlockClipTexture, PrepareGlockResources, 43, -20, 13.75f,
            "weapon_glock", "\"13.75\"", "Glock");
        fixture = Fixture{};
        Inputs(argv[1], argv[2], argv[3], RevolverModels, RevolverSounds, RevolverLocalWaves, RevolverEvents,
            RevolverInventory, RevolverParticle, RevolverClipTexture);
        exercise.template operator()<RevolverResources>(RevolverModels, RevolverSounds, RevolverLocalWaves, RevolverEvents,
            RevolverInventory, RevolverParticle, RevolverClipTexture, PrepareRevolverResources, 22, -6, 4.5f,
            "weapon_revolver", "\"4.5\"", "Revolver");
        fixture = Fixture{};
        Inputs(argv[1], argv[2], argv[3], VP70Models, VP70Sounds, VP70LocalWaves, VP70Events,
            VP70Inventory, VP70Particle, VP70ClipTexture);
        exercise.template operator()<VP70Resources>(VP70Models, VP70Sounds, VP70LocalWaves, VP70Events,
            VP70Inventory, VP70Particle, VP70ClipTexture, PrepareVP70Resources, 23, -3.6f, 3.1f,
            "weapon_vp70", "\"3.1\"", "VP70");
        if(argc==5)for(auto pistol:{guest::cof::Pistol::Glock,guest::cof::Pistol::P345})
        {
            fixture=Fixture{};
            const auto paths=guest::cof::PistolPresentationResources(pistol);
            for(const auto* path:paths)fixture.files.emplace(path,Read(std::filesystem::path(argv[4])/path));
            ResourceAuthority authority{&fixture,Current,19};
            Check(bool(PreparePistolPresentationResources(authority,pistol)),"actual three authored HUD dependencies registered");
            Check(fixture.steps==6&&fixture.precaches==3&&fixture.loads==0&&fixture.generics.size()==3,
                "HUD cold preflight precedes generic registration without hot reads");
            for(const auto* path:paths)
            {
                fixture.Reset();fixture.missing=path;
                Check(PreparePistolPresentationResources(authority,pistol).status==ResourceStatus::MissingFile&&fixture.precaches==0,
                    "missing HUD resource rejects before any generic registration");
            }
            for(int edge=1;edge<=6;++edge)
            {
                fixture.Reset();fixture.cancelAt=edge;
                Check(PreparePistolPresentationResources(authority,pistol).status==ResourceStatus::Cancelled,
                    "every HUD callback observes generation retirement");
            }
            for(int edge=4;edge<=6;++edge)
            {
                fixture.Reset();fixture.rejectAt=edge;
                Check(PreparePistolPresentationResources(authority,pistol).status==ResourceStatus::EngineRejected,
                    "engine generic rejection never reports ready");
            }
            fixture.Reset();auto invalid=authority;invalid.current=nullptr;
            Check(PreparePistolPresentationResources(invalid,pistol).status==ResourceStatus::InvalidAuthority,
                "HUD needs a real generation provider");
            fixture.Reset();g_engfuncs.pfnPrecacheGeneric=nullptr;
            Check(PreparePistolPresentationResources(authority,pistol).status==ResourceStatus::MissingEngine,
                "HUD missing actual generic consumer rejects");
            g_engfuncs.pfnPrecacheGeneric=Generic;
            Check(PreparePistolPresentationResources(authority,guest::cof::Pistol::Revolver).status==ResourceStatus::InvalidMetadata,
                "unregistered Revolver presentation is not reported available");
        }
    }
    catch (const std::exception& e) { std::printf("%s\n", e.what()); return 3; }
    std::printf("%u total checks, %u failures\n", checks, failures);
    return failures ? 5 : 0;
}
