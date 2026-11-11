// SPDX-License-Identifier: GPL-3.0-or-later
#include "native/guest/cof/player_actions.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

using vec3_t = float[3];
using byte = unsigned char;
using int32 = std::int32_t;
#include "external/ReGameDLL_CS/regamedll/engine/studio.h"

static std::size_t allocations{};
void* operator new(std::size_t size)
{
    ++allocations;
    if (auto* value = std::malloc(size ? size : 1)) return value;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

namespace
{
using namespace guest::cof;
static_assert(sizeof(void*) == 4 && sizeof(studiohdr_t) == 244 && sizeof(mstudioseqdesc_t) == 176);
int failures{}, checks{};
std::size_t comparisons{};
void Check(bool value, const char* message)
{
    ++checks;
    if (!value) { ++failures; std::cerr << message << '\n'; }
}
int HostCompare(const char* left, const char* right)
{
    ++comparisons;
    return _stricmp(left, right);
}
using Model = std::vector<std::byte>;
Model Read(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot read required real model");
    const std::vector<char> bytes{std::istreambuf_iterator<char>(file), {}};
    Model result(bytes.size());
    std::memcpy(result.data(), bytes.data(), bytes.size());
    return result;
}
template<class T> T Object(const Model& model, std::size_t at)
{
    T value;
    if (at > model.size() || sizeof(T) > model.size() - at) throw std::runtime_error("Bad oracle offset");
    std::memcpy(&value, model.data() + at, sizeof(T));
    return value;
}
template<class T> void Put(Model& model, std::size_t at, T value)
{
    if (at > model.size() || sizeof(T) > model.size() - at) throw std::runtime_error("Bad mutation offset");
    std::memcpy(model.data() + at, &value, sizeof(T));
}
std::size_t At(const Model& model, int index)
{
    return Object<studiohdr_t>(model, 0).seqindex + index * sizeof(mstudioseqdesc_t);
}
int Lookup(const Model& model, const char* label)
{
    const auto header = Object<studiohdr_t>(model, 0);
    for (int i = 0; i < header.numseq; ++i)
        if (!_stricmp(Object<mstudioseqdesc_t>(model, At(model, i)).label, label)) return i;
    return -1;
}
bool SameFloat(float left, float right)
{
    return std::bit_cast<std::uint32_t>(left) == std::bit_cast<std::uint32_t>(right);
}
void Metadata(const Model& model, const PlayerActionSequence& actual, const char* label)
{
    const int expected = Lookup(model, label);
    Check(expected >= 0 && actual.sequence == expected, "Actual SDK first-match index differs");
    if (expected < 0) return;
    const auto desc = Object<mstudioseqdesc_t>(model, At(model, expected));
    const float rate = desc.numframes <= 1 ? 256.0f : desc.fps * 256.0f / (desc.numframes - 1);
    Check(actual.frames == desc.numframes && actual.flags == desc.flags && SameFloat(actual.fps, desc.fps),
          "Actual descriptor metadata changed");
    Check(SameFloat(actual.sdkDuration, desc.numframes / desc.fps), "SDK duration must be frames/fps");
    Check(SameFloat(actual.normalizedFrameRate, rate), "SDK normalized frame rate differs");
}
constexpr PlayerModelGeneration Generation{7, 11};
constexpr PlayerActionLabels Pistol[]{
    {"ref_aim_onehanded", "crouch_aim_onehanded"},
    {"ref_shoot_onehanded", "crouch_shoot_onehanded"},
    {"ref_reload_onehanded", "crouch_reload_onehanded"}};
void RealModels(const Model& urban, const Model& male, const Model& female)
{
    PlayerActionBindings actions;
    std::string error;
    for (const auto* model : {&urban, &male, &female})
    {
        Check(actions.Bind(*model, Generation, Pistol, HostCompare, error) && error.empty(), "Real pistol labels rejected");
        Check(actions.Size() == 3, "Wrong bound action count");
        for (std::size_t slot = 0; slot < 3; ++slot)
            for (bool crouching : {false, true})
            {
                const auto result = actions.Select(Generation, slot, crouching);
                Check(result.has_value(), "Bound real action missing");
                if (result) Metadata(*model, *result, (crouching ? Pistol[slot].crouching : Pistol[slot].standing).data());
            }
        const auto reload = actions.Select(Generation, 2, false);
        Check(reload && !SameFloat(reload->sdkDuration, (reload->frames - 1) / reload->fps), "Duration confused with last frame time");
    }
    constexpr PlayerActionLabels extended[]{
        {"ref_aim_katanad", "crouch_aim_katanad"},
        {"ref_shoot_katanad", "crouch_shoot_katanad"},
        {"ref_aim_tomahawk", "crouch_aim_tomahawk"},
        {"ref_shoot_tomahawk", "crouch_shoot_tomahawk"},
        {"ref_shoot2_m16a1ep", "crouch_shoot2_m16a1ep"}};
    for (const auto* model : {&male, &female})
    {
        Check(actions.Bind(*model, Generation, extended, HostCompare, error), "Real extended labels rejected");
        for (std::size_t slot = 0; slot < std::size(extended); ++slot)
            for (bool crouching : {false, true})
            {
                const auto result = actions.Select(Generation, slot, crouching);
                Check(result.has_value(), "Extended action missing");
                if (result) Metadata(*model, *result, (crouching ? extended[slot].crouching : extended[slot].standing).data());
            }
    }
    Check(Lookup(male, "ref_aim_katanad") == 230 && Lookup(female, "ref_aim_katanad") == 232 &&
          Lookup(male, "crouch_aim_katanad") == 232 && Lookup(female, "crouch_aim_katanad") == 230,
          "Pinned real katana stand/crouch reversal changed");
    Check(Lookup(male, "ref_aim_tomahawk") == 216 && Lookup(female, "ref_aim_tomahawk") == 218 &&
          Lookup(male, "crouch_aim_tomahawk") == 218 && Lookup(female, "crouch_aim_tomahawk") == 216,
          "Pinned real tomahawk stand/crouch reversal changed");
    Check(!actions.Bind(urban, Generation, extended, HostCompare, error) && !actions.Select(Generation, 0, false),
          "Stock role accepted unsupported extension or retained old cache");
    constexpr PlayerActionLabels guilotine[]{{"ref_aim_guilotine", "crouch_aim_guilotine"}};
    Check(actions.Bind(male, Generation, guilotine, HostCompare, error), "Male guilotine absent");
    Check(!actions.Bind(female, Generation, guilotine, HostCompare, error) && actions.Size() == 0,
          "Female missing guilotine used a fallback");
    Check(!actions.Bind(urban, Generation, guilotine, HostCompare, error), "Stock missing guilotine accepted");
    constexpr PlayerActionLabels secondary[]{{"ref_shoot2_onehanded", "crouch_shoot2_onehanded"}};
    for (const auto* model : {&urban, &male, &female})
        Check(!actions.Bind(*model, Generation, secondary, HostCompare, error) && !actions.Select(Generation, 0, false),
              "Missing pistol shoot2 fell back to sequence zero or PLAYER_ATTACK2");
}
void LifecycleAndCase(const Model& urban)
{
    PlayerActionBindings actions;
    std::string error;
    constexpr PlayerActionLabels uppercase[]{{"REF_RELOAD_ONEHANDED", "CrOuCh_ReLoAd_OnEhAnDeD"}};
    Check(actions.Bind(urban, Generation, uppercase, HostCompare, error), "Host case-insensitive full labels rejected");
    auto result = actions.Select(Generation, 0, false);
    Check(result && result->sequence == Lookup(urban, "ref_reload_onehanded"), "Case-insensitive index differs");
    Check(!actions.Select({8, 11}, 0, false) && !actions.Select({7, 12}, 0, false) &&
          !actions.Select({}, 0, false) && !actions.Select(Generation, 1, false), "Stale generation or slot accepted");
    Model duplicate = urban;
    const int first = Lookup(urban, "ref_aim_onehanded");
    std::memcpy(duplicate.data() + At(duplicate, first + 1), "ref_aim_onehanded", sizeof("ref_aim_onehanded"));
    Check(actions.Bind(duplicate, Generation, std::span(Pistol, 1), HostCompare, error),
          "Duplicate label model rejected");
    result = actions.Select(Generation, 0, false);
    Check(result && result->sequence == first, "Duplicate label selected later match");
    {
        Model released = urban;
        std::string standing = "ref_reload_onehanded", crouching = "crouch_reload_onehanded";
        const PlayerActionLabels labels[]{{standing, crouching}};
        Check(actions.Bind(released, Generation, labels, HostCompare, error), "Owned numeric cache setup failed");
        std::fill(released.begin(), released.end(), std::byte{0});
        standing.assign(standing.size(), 'x'); crouching.assign(crouching.size(), 'x');
    }
    result = actions.Select(Generation, 0, true);
    Check(result && result->sequence == 18, "Binding retained source model or label lifetime");
    const auto beforeAllocations = allocations, beforeComparisons = comparisons;
    std::uint64_t sum{};
    for (std::size_t i = 0; i < 200000; ++i)
    {
        const auto value = actions.Select(Generation, 0, (i & 1) != 0);
        if (value) sum += value->sequence;
    }
    Check(sum == 3900000 && allocations == beforeAllocations && comparisons == beforeComparisons,
          "Hot selection allocated, compared labels or changed cached results");
    actions.Reset();
    Check(actions.Size() == 0 && !actions.Select(Generation, 0, false), "Reset retained usable cache");
}
void Invalid(const Model& urban)
{
    PlayerActionBindings actions;
    std::string error;
    const auto fail = [&](const Model& model)
    {
        Check(actions.Bind(urban, Generation, Pistol, HostCompare, error), "Failure test initial cache setup failed");
        Check(!actions.Bind(model, Generation, Pistol, HostCompare, error) && !error.empty() &&
              actions.Size() == 0 && !actions.Select(Generation, 0, false), "Malformed model accepted or retained old bindings");
    };
    fail({}); fail(Model(urban.begin(), urban.begin() + 243));
    Model changed = urban;
    const auto mutateHeader = [&](std::size_t offset, int value)
    { changed = urban; Put(changed, offset, value); fail(changed); };
    mutateHeader(offsetof(studiohdr_t, id), 0);
    mutateHeader(offsetof(studiohdr_t, version), 11);
    mutateHeader(offsetof(studiohdr_t, length), int(urban.size() - 1));
    mutateHeader(offsetof(studiohdr_t, numbones), 129);
    mutateHeader(offsetof(studiohdr_t, boneindex), -1);
    mutateHeader(offsetof(studiohdr_t, boneindex), int(urban.size()));
    mutateHeader(offsetof(studiohdr_t, numseq), 0);
    mutateHeader(offsetof(studiohdr_t, numseq), 2049);
    mutateHeader(offsetof(studiohdr_t, seqindex), -1);
    mutateHeader(offsetof(studiohdr_t, seqindex), int(urban.size() - 1));
    for (float fps : {0.0f, -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
    { changed = urban; Put(changed, At(changed, 0) + offsetof(mstudioseqdesc_t, fps), fps); fail(changed); }
    for (int frames : {0, -1})
    { changed = urban; Put(changed, At(changed, 0) + offsetof(mstudioseqdesc_t, numframes), frames); fail(changed); }
    changed = urban; std::memset(changed.data() + At(changed, 0), 'x', 32); fail(changed);
    const int reload = Lookup(urban, "ref_reload_onehanded");
    changed = urban;
    Put(changed, At(changed, reload) + offsetof(mstudioseqdesc_t, numframes), std::numeric_limits<int>::max());
    Check(actions.Bind(changed, Generation, Pistol, HostCompare, error), "Unsupported arbitrary frame-count cap introduced");
    if (const auto large = actions.Select(Generation, 2, false)) Metadata(changed, *large, "ref_reload_onehanded");
    else Check(false, "Large finite SDK duration unavailable");
    changed = urban; Put(changed, At(changed, reload) + offsetof(mstudioseqdesc_t, fps), std::numeric_limits<float>::denorm_min()); fail(changed);
    const auto invalidBind = [&](PlayerModelGeneration generation, std::span<const PlayerActionLabels> labels,
                                 PlayerActionLabelCompare compare)
    {
        Check(actions.Bind(urban, Generation, Pistol, HostCompare, error), "Bad argument initial cache setup failed");
        Check(!actions.Bind(urban, generation, labels, compare, error) && actions.Size() == 0, "Bad bind arguments accepted");
    };
    invalidBind({0, 1}, Pistol, HostCompare); invalidBind({1, 0}, Pistol, HostCompare);
    invalidBind(Generation, Pistol, nullptr); invalidBind(Generation, {}, HostCompare);
    std::array<PlayerActionLabels, PlayerActionBindings::Capacity + 1> tooMany;
    tooMany.fill(Pistol[0]); invalidBind(Generation, tooMany, HostCompare);
    for (auto bad : {"", "onehanded", "crouch_aim_onehanded", "ref_", "ref_aaaaaaaaaaaaaaaaaaaaaaaaaaaa"})
    { const PlayerActionLabels labels[]{{bad, "crouch_aim_onehanded"}}; invalidBind(Generation, labels, HostCompare); }
    const PlayerActionLabels embedded[]{{std::string_view("ref_aim\0_onehanded", 18), "crouch_aim_onehanded"}};
    invalidBind(Generation, embedded, HostCompare);
}
}
int main(int argc, char** argv)
{
    if (argc != 4) return 2;
    try
    {
        const auto urban = Read(argv[1]), male = Read(argv[2]), female = Read(argv[3]);
        Check(Object<studiohdr_t>(urban, 0).numseq == 111 && Object<studiohdr_t>(male, 0).numseq == 361 &&
              Object<studiohdr_t>(female, 0).numseq == 361, "Pinned real model sequence counts changed");
        RealModels(urban, male, female); LifecycleAndCase(urban); Invalid(urban);
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 3; }
    std::cout << "checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
