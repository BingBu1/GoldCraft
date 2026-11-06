#include <metahook.h>
#include "resource_native_hud.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace goldcraft::resource_native_hud {
namespace {
static_assert(sizeof(void*) == 4 && sizeof(client_sprite_t) == 152);
static_assert(offsetof(client_sprite_t, iRes) == 132);
static_assert(offsetof(client_sprite_t, rc) == 136 && sizeof(wrect_t) == 16);

template<class T> T load(std::span<const std::byte> bytes, std::size_t offset) {
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}
template<class T> void save(std::span<std::byte> bytes, std::size_t offset, T value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}
struct Busy {
    bool& value;
    explicit Busy(bool& ref) : value(ref) { value = true; }
    ~Busy() { value = false; }
};
bool same_arrays(const std::array<void*, 3>& a, const std::array<void*, 3>& b) {
    return a == b;
}
constexpr std::array<std::size_t, 3> array_offsets{0x178, 0x17c, 0x180};
constexpr std::array<std::size_t, 5> scope_offsets{0x40f0, 0x40f4, 0x40f8, 0x40fc, 0x4100};

bool bytes_equal(std::span<const std::byte> image, std::size_t offset,
                 std::initializer_list<unsigned char> expected) {
    if (offset > image.size() || expected.size() > image.size() - offset) return false;
    for (auto value : expected)
        if (image[offset++] != static_cast<std::byte>(value)) return false;
    return true;
}
}

const char* ValidateImage(std::span<const std::byte> image, std::uintptr_t base,
                          std::string_view client_identity,
                          std::string_view engine_identity) {
    if (client_identity != client_sha256 || engine_identity != engine_sha256)
        return "native HUD module identity mismatch";
    if (!base || base > std::numeric_limits<std::uint32_t>::max() - hud_rva ||
        image.size() < hud_rva + hud_size) return "incomplete native HUD image";
    // HUD_VidInit: mov ecx,gHUD; call CHud::VidInit; mov eax,1; ret.
    if (!bytes_equal(image, 0x47790, {0xb9}) ||
        load<std::uint32_t>(image, 0x47791) != base + hud_rva ||
        !bytes_equal(image, 0x47795, {0xe8,0x06,0x99,0x00,0x00,0xb8,1,0,0,0,0xc3}))
        return "native HUD export layout mismatch";
    if (!bytes_equal(image, 0x51104, {0x8b,0x7e,0x0c,0x85,0xff,0x0f,0x85,0x57,1,0,0}) ||
        !bytes_equal(image, 0x50424, {0xff,0xb7,0x78,1,0,0,0xe8,0xea,0x44,0x06,0x00}) ||
        !bytes_equal(image, 0x5042f, {0xff,0xb7,0x7c,1,0,0,0xe8,0xdf,0x44,0x06,0x00}) ||
        !bytes_equal(image, 0x5043a, {0xff,0xb7,0x80,1,0,0,0xe8,0xd4,0x44,0x06,0x00}))
        return "native HUD array ownership layout mismatch";
    if (!bytes_equal(image, 0xb4919, {0xe9,0xb6,0x0b,0,0}) ||
        !bytes_equal(image, 0xb54d4, {0xe9,0xcb,1,0,0}))
        return "native HUD array deallocator mismatch";
    return nullptr;
}

const char* ValidateSourceAllocator(std::span<const std::byte> image, std::uintptr_t base,
                                    std::string_view identity) {
    if (identity != engine_sha256 || !base ||
        base > std::numeric_limits<std::uint32_t>::max() - 0x2b2438 || image.size() < 0x2b2440)
        return "native HUD source allocator image mismatch";
    // Original SPR_GetList calls imported calloc; the native free thunk uses
    // the same api-ms-win-crt-heap-l1-1-0 import owner, never the plugin CRT.
    if (!bytes_equal(image, 0x195cb0, {0x69,0xcb,0x98,0,0,0,0x6a,1,0x51,0xff,0x15}) ||
        load<std::uint32_t>(image, 0x195cbb) != base + 0x2b2438 ||
        !bytes_equal(image, source_free_rva, {0xff,0x25}) ||
        load<std::uint32_t>(image, source_free_rva + 2) != base + 0x2b242c)
        return "native HUD source allocator instructions mismatch";
    return nullptr;
}

Refresh::Snapshot Refresh::read() const {
    Snapshot s;
    const auto bytes = binding_.hud;
    s.catalog = {load<client_sprite_t*>(bytes, 0x0c), load<int>(bytes, 0x14)};
    s.selected = load<int>(bytes, 0x10); s.resolution = load<int>(bytes, 0x60);
    s.number = load<int>(bytes, 0x4754); s.font_height = load<int>(bytes, 0x16c);
    for (std::size_t i = 0; i < s.arrays.size(); ++i) s.arrays[i] = load<void*>(bytes, array_offsets[i]);
    for (std::size_t i = 0; i < s.textures.size(); ++i) s.textures[i] = load<int>(bytes, scope_offsets[i]);
    return s;
}
void Refresh::write(const Snapshot& s) const {
    save(binding_.hud, 0x0c, s.catalog.entries); save(binding_.hud, 0x14, s.catalog.count);
    save(binding_.hud, 0x10, s.selected); save(binding_.hud, 0x60, s.resolution);
    save(binding_.hud, 0x4754, s.number); save(binding_.hud, 0x16c, s.font_height);
    for (std::size_t i = 0; i < s.arrays.size(); ++i) save(binding_.hud, array_offsets[i], s.arrays[i]);
    for (std::size_t i = 0; i < s.textures.size(); ++i) save(binding_.hud, scope_offsets[i], s.textures[i]);
}
const char* Refresh::gate(const Admission& a, bool restoring) const {
    if (!a.connection || !a.initial_vidinit || a.state != 3 || a.uploading ||
        !a.consumers_paused || a.download_started || a.signon_started)
        return "native HUD refresh requires paused pre-download signon";
    if (phase_ != Phase::idle && (a.connection != admission_.connection ||
        a.initial_vidinit != admission_.initial_vidinit)) return "native HUD connection changed";
    const bool ready = a.view_proof == ResourceViewProof::server_manifest
        ? a.manifest_complete && a.hashes_verified
        : a.view_proof == ResourceViewProof::restored_base && a.base_view_verified;
    if (restoring ? !a.old_view_restored : !ready)
        return restoring ? "old resource view has not been restored" : "native HUD resource view is not verified";
    return nullptr;
}
const char* Refresh::validate_catalog(Catalog catalog, int resolution) const {
    if (!catalog.entries || catalog.count <= 0 || catalog.count > 8192 ||
        (resolution != 320 && resolution != 640)) return "invalid native HUD directory";
    constexpr std::array names{"number_0", "bucket1", "flash_empty", "flash_full",
        "flash_beam", "dmg_bio", "cross", "radar", "radaropaque"};
    std::array<int, names.size()> indices; indices.fill(-1);
    int selected = 0;
    for (int i = 0; i < catalog.count; ++i) {
        const auto& item = catalog.entries[i];
        if (item.iRes != resolution) continue;
        if (!std::memchr(item.szName, 0, sizeof(item.szName)) ||
            !std::memchr(item.szSprite, 0, sizeof(item.szSprite))) return "unterminated native HUD entry";
        for (std::size_t n = 0; n < names.size(); ++n)
            if (!std::strncmp(item.szName, names[n], 24) && indices[n] < 0) indices[n] = selected;
        ++selected;
    }
    if (std::ranges::any_of(indices, [](int i) { return i < 0; }) || indices[5] + 1 >= selected)
        return "required native HUD rectangle entry is missing";
    return nullptr;
}
const char* Refresh::validate_arrays(const Snapshot& s) const {
    if (const auto* error = validate_catalog(s.catalog, s.resolution)) return error;
    int expected = 0;
    for (int i = 0; i < s.catalog.count; ++i) expected += s.catalog.entries[i].iRes == s.resolution;
    if (s.selected != expected || s.number < 0 || s.number >= s.selected)
        return "native HUD directory and arrays disagree";
    for (std::size_t i = 0; i < s.arrays.size(); ++i) {
        if (!s.arrays[i] || s.arrays[i] == s.catalog.entries) return "invalid native HUD array owner";
        for (std::size_t j = 0; j < i; ++j)
            if (s.arrays[i] == s.arrays[j]) return "aliased native HUD arrays";
    }
    return nullptr;
}
bool Refresh::call_vidinit() {
    try { return binding_.vid_init(binding_.context) == 1; }
    catch (...) { return false; }
}
void Refresh::release_arrays(const Snapshot& retired, const Snapshot& current) const {
    for (std::size_t i = 0; i < retired.arrays.size(); ++i) {
        auto* pointer = retired.arrays[i];
        if (!pointer || pointer == retired.catalog.entries || pointer == current.catalog.entries ||
            std::find(current.arrays.begin(), current.arrays.end(), pointer) != current.arrays.end() ||
            std::find(retired.arrays.begin(), retired.arrays.begin() + i, pointer) != retired.arrays.begin() + i)
            continue;
        binding_.delete_array(pointer);
    }
}
void Refresh::release_textures(const Snapshot& first, const Snapshot& second,
                              const Snapshot& current) const {
    std::array<int, 10> retired{};
    std::copy(first.textures.begin(), first.textures.end(), retired.begin());
    std::copy(second.textures.begin(), second.textures.end(), retired.begin() + 5);
    for (std::size_t i = 0; i < retired.size(); ++i) {
        const auto id = retired[i];
        if (id <= 0 || std::find(current.textures.begin(), current.textures.end(), id) != current.textures.end() ||
            std::find(retired.begin(), retired.begin() + i, id) != retired.begin() + i) continue;
        binding_.delete_texture(binding_.context, id);
    }
}
void Refresh::reset() {
    phase_ = Phase::idle; binding_ = {}; owner_ = {}; admission_ = {};
    old_ = {}; replacement_ = {}; restore_displaced_ = {}; candidate_ = {};
    target_resolution_ = 0;
}

Result Refresh::Prepare(const Admission& admission, const Binding& binding, DirectoryOwner owner) {
    if (busy_ || pending()) return {"native HUD refresh is already pending"};
    if (const auto* error = gate(admission)) return {error};
    const auto result = stage(binding, owner, 0);
    if (result) admission_ = admission;
    return result;
}
Result Refresh::stage(const Binding& binding, DirectoryOwner owner, int resolution) {
    if (binding.hud.size() < hud_size || !binding.delete_array || !binding.vid_init ||
        !binding.delete_texture || !binding.current_resolution || !owner.prepare ||
        !owner.activate || !owner.restore || !owner.finish)
        return {"native HUD resource owner is unavailable"};
    Busy guard(busy_);
    binding_ = binding; owner_ = owner; old_ = read();
    target_resolution_ = binding_.current_resolution(binding_.context);
    if ((target_resolution_ != 320 && target_resolution_ != 640) ||
        (resolution ? resolution != target_resolution_ : old_.resolution != target_resolution_)) {
        reset(); return {"native HUD screen resolution does not match the observed boundary"};
    }
    const char* error = nullptr;
    try { error = owner_.prepare(owner_.context, old_.catalog, candidate_); }
    catch (...) { error = "cannot prepare native HUD directory"; }
    if (error) { reset(); return {error}; } // prepare failure retains no pending owner.
    // The directory owner checks pointer/count identity without dereferencing
    // current.entries. Only its successful claim permits reading that catalog.
    if ((error = validate_arrays(old_))) {
        owner_.finish(owner_.context, false); reset(); return {error};
    }
    if (candidate_.entries == old_.catalog.entries ||
        (error = validate_catalog(candidate_, target_resolution_))) {
        owner_.finish(owner_.context, false); reset();
        return {error ? error : "native HUD directory generation was not replaced"};
    }
    phase_ = Phase::prepared;
    return {};
}
Result Refresh::Rebuild(const Admission& admission) {
    if (busy_ || phase_ != Phase::prepared) return {"native HUD refresh is not prepared"};
    if (const auto* error = gate(admission)) return {error};
    return rebuild();
}
Result Refresh::rebuild() {
    if (binding_.current_resolution(binding_.context) != target_resolution_)
        return {"native HUD screen resolution changed after preflight"};
    const auto current = read();
    if (current.catalog != old_.catalog || !same_arrays(current.arrays, old_.arrays) ||
        current.selected != old_.selected || current.resolution != old_.resolution)
        return {"native HUD changed after preflight"};
    Busy guard(busy_);
    if (!owner_.activate(owner_.context)) return {"native HUD directory activation failed"};
    // Preserve old arrays until the entire export chain succeeds; its native
    // list==null branch allocates all three replacements using the native CRT.
    auto empty = old_; empty.catalog = {}; empty.selected = 0; empty.number = -1;
    empty.font_height = 0; empty.arrays = {}; write(empty);
    phase_ = Phase::rebuilding;
    const bool returned = call_vidinit();
    replacement_ = read();
    const char* error = !returned ? "native HUD rebuild did not finish" :
        replacement_.catalog != candidate_ ? "native HUD used a different directory owner" :
        validate_arrays(replacement_);
    if (!error) for (auto* pointer : replacement_.arrays)
        if (std::find(old_.arrays.begin(), old_.arrays.end(), pointer) != old_.arrays.end())
            error = "native HUD reused a retired array";
    phase_ = error ? Phase::restore_required : Phase::rebuilt;
    return {error, true, error != nullptr};
}
Result Refresh::Commit(const Admission& admission) {
    if (busy_ || phase_ != Phase::rebuilt) return {"native HUD refresh has not rebuilt"};
    if (const auto* error = gate(admission)) return {error, true, true};
    return commit();
}
Result Refresh::commit() {
    if (binding_.current_resolution(binding_.context) != replacement_.resolution)
        return {"native HUD screen resolution changed before commit", true, true};
    const auto current = read();
    if (current.catalog != replacement_.catalog || current.arrays != replacement_.arrays)
        return {"native HUD changed before commit", true, true};
    Busy guard(busy_);
    release_arrays(old_, current); release_textures(old_, {}, current);
    owner_.finish(owner_.context, true);
    reset();
    return {nullptr, true, false};
}
Result Refresh::Rollback(const Admission& admission) {
    if (busy_ || phase_ == Phase::idle || phase_ == Phase::rebuilding || phase_ == Phase::restoring)
        return {"native HUD refresh cannot roll back in this phase"};
    if (phase_ == Phase::prepared) {
        Busy guard(busy_); owner_.finish(owner_.context, false); reset(); return {};
    }
    if (const auto* error = gate(admission, true)) return {error, true, true};
    return restore();
}
Result Refresh::restore() {
    // The original nonnull-list branch writes the old HSPRITE allocation using
    // the current screen resolution. Reject before restoring fields/calling it.
    if (binding_.current_resolution(binding_.context) != old_.resolution)
        return {"native HUD recovery requires its retained screen resolution", true, true};
    Busy guard(busy_);
    if (!owner_.restore(owner_.context)) return {"native HUD directory restoration failed", true, true};
    restore_displaced_ = read();
    write(old_);
    phase_ = Phase::restoring;
    const bool returned = call_vidinit();
    const auto restored = read();
    const char* error = !returned ? "native HUD rollback did not finish" :
        restored.catalog != old_.catalog || restored.arrays != old_.arrays ?
        "native HUD rollback changed retained array ownership" : validate_arrays(restored);
    if (error) {
        // Keep both array generations alive: component rect pointers may still
        // reference either. A parent must keep this admission suspended.
        phase_ = Phase::restore_required;
        retire_restored_textures(restored);
        return {error, true, true};
    }
    release_arrays(replacement_, restored);
    release_textures(old_, restore_displaced_, restored);
    owner_.finish(owner_.context, false);
    reset();
    return {nullptr, true, false};
}

void Refresh::retire_restored_textures(const Snapshot& current) {
    release_textures(old_, restore_displaced_, current);
    // Also used after a proven non-C++ unwind. Candidate arrays remain in
    // replacement_; never replace that generation with the restored old arrays.
    old_.textures = current.textures;
    restore_displaced_ = {};
}

Result Refresh::Initialize(const NativeInitialization& event, const Binding& binding, DirectoryOwner owner) {
    if (!event.event || event.event <= last_native_event_ || !event.engine_vidinit ||
        !event.clear_state_observed || !event.consumers_paused || !event.resource_view_ready ||
        !event.previous_callback_unwound || (event.resolution != 320 && event.resolution != 640))
        return {"native HUD initialization requires a new observed engine reset boundary", false, pending()};
    if (pending() && (binding.hud.data() != binding_.hud.data() || binding.hud.size() != binding_.hud.size() ||
        binding.delete_array != binding_.delete_array || binding.context != binding_.context ||
        binding.vid_init != binding_.vid_init || binding.delete_texture != binding_.delete_texture ||
        binding.current_resolution != binding_.current_resolution ||
        owner.context != owner_.context || owner.prepare != owner_.prepare || owner.activate != owner_.activate ||
        owner.restore != owner_.restore || owner.finish != owner_.finish))
        return {"native HUD initialization owner changed during a pending transaction", false, true};
    if (pending() && phase_ != Phase::prepared && old_.resolution != event.resolution)
        return {"interrupted native HUD recovery requires its retained resolution", false, true};
    // The parent proves the old native stack was unwound. A Host_Error longjmp
    // can leave the Busy guard set and skip the replacement snapshot entirely.
    last_native_event_ = event.event;
    busy_ = false;
    bool recovered = false;
    if (phase_ == Phase::prepared) {
        owner_.finish(owner_.context, false); reset();
    } else if (pending()) {
        if (phase_ == Phase::rebuilding) replacement_ = read();
        else if (phase_ == Phase::restoring) {
            retire_restored_textures(read());
            phase_ = Phase::restore_required;
        }
        const auto result = restore();
        if (!result) return result;
        recovered = true;
    }
    const auto prepared = stage(binding, owner, event.resolution);
    if (!prepared) return {prepared.error, recovered, false};
    const auto rebuilt = rebuild();
    if (!rebuilt) return rebuilt;
    return commit();
}

}
