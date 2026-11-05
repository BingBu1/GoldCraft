// SPDX-License-Identifier: GPL-3.0-or-later
#include <metahook.h>
#include <com_model.h>
#include "../../native/client/resource_model_cache.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

using namespace goldcraft::resource_model_cache;

namespace {
int checks{}, failures{};
void check(bool value, const char* description) {
    ++checks;
    if (!value) { ++failures; std::printf("FAIL: %s\n", description); }
}

struct Fixture;
Fixture* active{};
struct NativeModel {
    model_t model{};
    int enabled{1}, checked{1};
    std::uint32_t crc{0x11223344};
    std::array<cache_user_t, 16> sequences{};
    std::array<cache_user_t, 32> skins{};
    explicit NativeModel(modtype_t type, const char* name) {
        model.type = type;
        std::strcpy(model.name, name);
        model.needload = 0;
        model.numframes = 7;
        if (type == mod_studio) model.submodels = reinterpret_cast<dmodel_t*>(sequences.data());
    }
    Model reference() { return {&model, &checked, &crc, model.type == mod_studio ?
        std::span<cache_user_t>(skins) : std::span<cache_user_t>()}; }
};

struct Fixture {
    NativeModel studio{mod_studio, "models/test.mdl"};
    NativeModel texture{mod_studio, "models/testT.mdl"};
    NativeModel sprite{mod_sprite, "sprites/test.spr"};
    NativeModel unaffected{mod_studio, "models/other.mdl"};
    std::array<cache_user_t, 3> sounds{};
    std::array<std::array<std::uint8_t, 8>, 60> allocations{};
    unsigned next{};
    std::unordered_set<cache_user_t*> owned;
    std::vector<std::string> operations;
    const char* validation_error{};
    bool release_ok{true}, reenter{}, released{}, sound_stopped{};
    std::array<Model, 3> selected;
    std::array<cache_user_t*, 3> selected_sounds;
    Engine engine;
    Owners owners;

    Fixture() {
        active = this;
        allocate(studio.model.cache);
        allocate(texture.model.cache);
        allocate(unaffected.model.cache);
        for (auto& slot : studio.sequences) allocate(slot);
        for (auto& slot : studio.skins) allocate(slot);
        for (auto& sound : sounds) allocate(sound);
        // This header deliberately has no Cache_Alloc ownership.
        sprite.model.cache.data = allocations[next++].data();
        studio.model.texinfo = reinterpret_cast<mtexinfo_t*>(&texture.model);
        selected = {studio.reference(), texture.reference(), sprite.reference()};
        selected_sounds = {&sounds[0], &sounds[1], &sounds[2]};
        engine = {stop, free_cache, unload_sprite};
        owners = {this, validate, release};
    }
    void allocate(cache_user_t& slot) {
        slot.data = allocations[next++].data();
        owned.insert(&slot);
    }
    static const char* validate(void* context, std::span<const Model> models) {
        auto& self = *static_cast<Fixture*>(context);
        self.operations.emplace_back("validate");
        check(!self.released && !self.sound_stopped, "preflight precedes consumer release");
        check(models[0].value->cache.data != nullptr, "preflight sees old model header");
        if (self.reenter) {
            const auto nested = Invalidate(models, {}, self.engine, self.owners);
            check(!nested && !nested.consumers_released, "recursive preflight is rejected without mutation");
        }
        return self.validation_error;
    }
    static bool release(void* context, std::span<const Model> models) {
        auto& self = *static_cast<Fixture*>(context);
        self.operations.emplace_back("release");
        for (const auto& model : models)
            check(model.value->cache.data != nullptr, "all GPU readers retain valid headers through owner release");
        check(self.studio.sequences[15].data != nullptr, "external sequences retained while owners release");
        self.released = self.release_ok;
        return self.release_ok;
    }
    static void stop(int clear) {
        check(clear == 1, "native S_StopAllSounds clear parameter");
        active->operations.emplace_back("stop");
        active->sound_stopped = true;
        // Exercise actual stream-channel ownership: the native stop routine may
        // release a sample before the invalidation pass reaches that sound.
        free_cache(&active->sounds[0]);
    }
    static void free_cache(cache_user_t* slot) {
        check(active->owned.contains(slot), "Cache_Free only receives Cache_Alloc owners");
        check(slot->data != nullptr, "Cache_Free never receives already released stream/cache");
        const bool sound = std::find(active->selected_sounds.begin(), active->selected_sounds.end(), slot)
            != active->selected_sounds.end();
        check(sound ? active->sound_stopped : active->released, "consumers stop before their cache allocation is freed");
        if (slot == &active->studio.model.cache) {
            for (const auto& group : active->studio.sequences)
                check(group.data == nullptr, "all external sequence caches released before main header");
            for (const auto& skin : active->studio.skins)
                check(skin.data == nullptr, "all skin caches released before main header");
            active->operations.emplace_back("main");
        } else active->operations.emplace_back("cache");
        active->owned.erase(slot);
        slot->data = nullptr;
    }
    static void unload_sprite(model_t* model) {
        check(active->released, "sprite renderer references released before texture unload");
        check(model->type == mod_sprite && model->cache.data, "sprite unload retains its hunk header");
        active->operations.emplace_back("sprite");
    }
    void unchanged() {
        check(studio.model.cache.data && texture.model.cache.data && sprite.model.cache.data,
              "failed preflight preserves all model payloads");
        check(studio.sequences[15].data && studio.skins[31].data, "failed preflight preserves auxiliary caches");
        check(sounds[0].data && sounds[2].data, "failed preflight preserves sound caches");
        check(studio.checked == 1 && studio.crc == 0x11223344, "failed preflight preserves CRC state");
    }
};
}

int main() {
    {
        Fixture f;
        auto* sequences = f.studio.model.submodels;
        auto* unaffected = f.unaffected.model.cache.data;
        const auto result = Invalidate(f.selected, f.selected_sounds, f.engine, f.owners);
        check(bool(result) && result.consumers_released, "combined model and sound invalidation succeeds");
        check(result.models == 3 && result.sprites == 1 && result.cache_blocks == 52,
              "counts exclude the native stream release and hunk header");
        check(!f.studio.model.cache.data && !f.texture.model.cache.data && !f.sprite.model.cache.data,
              "all selected payloads unavailable until verified reload");
        check(f.studio.model.submodels == sequences, "native sequence-array ownership retained");
        check(f.studio.model.texinfo == nullptr, "companion texture pointer reset for normal loader");
        check(f.studio.enabled == 1 && f.studio.checked == 0 && f.studio.crc == 0,
              "CRC enabled retained while checked and initial start a new version");
        check(f.texture.checked == 0 && f.sprite.checked == 0, "every selected model CRC reset");
        check(f.studio.model.needload == 1 && f.sprite.model.needload == 1, "normal native reload requested");
        check(f.unaffected.model.cache.data == unaffected && f.unaffected.checked == 1,
              "unselected models and CRC are preserved");
        check(f.operations[0] == "validate" && f.operations[1] == "release" && f.operations[2] == "stop",
              "preflight and owners finish before sound and native caches");
    }
    for (int missing = 0; missing < 5; ++missing) {
        Fixture f;
        if (missing == 0) f.engine.cache_free = nullptr;
        if (missing == 1) f.engine.stop_sounds = nullptr;
        if (missing == 2) f.engine.unload_sprite_textures = nullptr;
        if (missing == 3) f.owners.validate = nullptr;
        if (missing == 4) f.owners.release = nullptr;
        const auto result = Invalidate(f.selected, f.selected_sounds, f.engine, f.owners);
        check(!result && !result.consumers_released, "missing real owner API fails before mutation");
        check(f.operations.empty(), "missing API never calls partial release");
        f.unchanged();
    }
    {
        Fixture f;
        f.validation_error = "renderer is drawing";
        const auto result = Invalidate(f.selected, f.selected_sounds, f.engine, f.owners);
        check(!result && !result.consumers_released && result.error == f.validation_error,
              "renderer preflight rejection is returned");
        check(f.operations.size() == 1, "owner validation rejection stops all release operations");
        f.unchanged();
    }
    {
        Fixture f;
        f.release_ok = false;
        const auto result = Invalidate(f.selected, f.selected_sounds, f.engine, f.owners);
        check(!result && result.consumers_released, "partial owner failure requires admission cancellation");
        check(f.operations.size() == 2, "owner failure never frees native caches or starts sounds");
        f.unchanged();
    }
    {
        Fixture f;
        f.reenter = true;
        check(bool(Invalidate(f.selected, f.selected_sounds, f.engine, f.owners)),
              "outer release survives rejected recursion");
    }
    for (int invalid = 0; invalid < 6; ++invalid) {
        Fixture f;
        if (invalid == 0) f.selected[1] = f.selected[0];
        if (invalid == 1) f.selected[1].value = nullptr;
        if (invalid == 2) f.selected[1].crc_checked = nullptr;
        if (invalid == 3) f.texture.model.type = mod_brush;
        if (invalid == 4) f.texture.model.type = mod_alias;
        if (invalid == 5) f.selected[2].skins = f.studio.skins;
        const auto result = Invalidate(f.selected, f.selected_sounds, f.engine, f.owners);
        check(!result && !result.consumers_released && f.operations.empty(),
              "all model inputs validated before owner release");
        f.unchanged();
    }
    {
        Fixture f;
        f.sounds[2].data = f.studio.model.cache.data;
        check(!Invalidate(f.selected, f.selected_sounds, f.engine, f.owners),
              "distinct native cache owners cannot share an allocation");
        check(f.operations.empty(), "aliased allocation rejected before mutation");
        f.unchanged();
    }
    {
        Fixture f;
        f.sounds[2].data = f.sprite.model.cache.data;
        check(!Invalidate(f.selected, f.selected_sounds, f.engine, f.owners),
              "hunk pointer cannot be freed through sound ownership");
        check(f.operations.empty(), "hunk alias rejected before mutation");
    }
    {
        Fixture f;
        // Duplicate references are common in a resource table; the stable
        // cache_user, not the table index, is the allocation owner.
        std::array<cache_user_t*, 4> repeated{&f.sounds[0], &f.sounds[1], &f.sounds[1], &f.sounds[2]};
        const auto result = Invalidate({}, repeated, f.engine, {});
        check(bool(result) && result.cache_blocks == 2 && result.models == 0,
              "sound-only update deduplicates references and permits no model API");
        check(f.studio.model.cache.data && f.studio.checked == 1, "sound-only update preserves model state");
    }
    {
        Fixture f;
        std::array<cache_user_t*, 1> null{nullptr};
        check(!Invalidate({}, null, f.engine, {}), "null sound ownership rejected");
        check(f.operations.empty(), "invalid sound ownership rejected before stopping consumers");
        check(bool(Invalidate({}, {}, {}, {})), "empty verified update requires no cache work");
    }
    std::printf("resource model cache: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
