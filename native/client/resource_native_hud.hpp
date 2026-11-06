#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

struct client_sprite_s;

namespace goldcraft::resource_native_hud {

inline constexpr std::string_view client_sha256 =
    "b434b1c09b10b011be6c42e4154955da0c3ca46e95746988ebea1aa316a2a9f2";
inline constexpr std::string_view engine_sha256 =
    "9ba9a2db5e07598fd59afa35507a98c86162e4e15b3835177b78c11842cd2295";
inline constexpr std::uint32_t hud_rva = 0x117b48;
inline constexpr std::uint32_t delete_array_rva = 0xb4919;
inline constexpr std::uint32_t source_free_rva = 0x1da3e0;
inline constexpr std::size_t hud_size = 0x4780;

struct Catalog {
    client_sprite_s* entries{};
    int count{};
    bool operator==(const Catalog&) const = default;
};

// All callbacks are synchronous on the client thread. The real directory
// owner keeps both generations alive until finish; never infer its allocator
// from the pointer. activate/restore return false WITHOUT changing ownership.
// prepare first verifies current pointer/count against its own storage without
// dereferencing the supplied pointer; success authorizes catalog validation.
struct DirectoryOwner {
    void* context{};
    const char* (*prepare)(void*, Catalog current, Catalog& replacement){};
    bool (*activate)(void*) noexcept{};
    bool (*restore)(void*) noexcept{};
    void (*finish)(void*, bool commit) noexcept{};
};

enum class ResourceViewProof { server_manifest, restored_base };
struct Admission {
    std::uint64_t connection{};
    std::uint64_t initial_vidinit{}; // Observed native call for this connection.
    int state{}, uploading{};
    bool manifest_complete{}, hashes_verified{}, consumers_paused{};
    bool download_started{}, signon_started{};
    bool old_view_restored{}; // Only used by rollback after the parent restores VFS.
    ResourceViewProof view_proof{ResourceViewProof::server_manifest};
    // Base restoration after overlay withdrawal has no server manifest. Parent
    // verifies the base VFS and dependent cache owners before setting this.
    bool base_view_verified{};
};

struct Binding {
    std::span<std::byte> hud;
    // Caller first uses ValidateImage and the matching runtime symbol catalog.
    // delete_array must call client+delete_array_rva, not the plugin's CRT.
    void (*delete_array)(void*) noexcept{};
    void* context{};
    // Original native client export at +0x47790, including its own components.
    // Do not recurse through CSOL refresh wrappers or call HUD_Init.
    int (*vid_init)(void*){};
    // Native scope creates ISurface IDs, not raw GL names. Route to that same
    // ISurface owner. Callbacks must not modify the HUD or its current textures.
    void (*delete_texture)(void*, int) noexcept{};
    // Sample the actual current screen width as 320/640 immediately on every
    // call. Reading the retained gHUD resolution is not a screen observation.
    int (*current_resolution)(void*) noexcept{};
};

// This is an observed native export invocation after a real CL_ClearState
// epoch, not permission to synthesize a call from Frame or HUD_Reset. The
// parent's native hook retains every outer plugin wrapper and delegates only
// its original trampoline. Ordinary/base views require no server manifest.
struct NativeInitialization {
    std::uint64_t event{};
    bool engine_vidinit{}, clear_state_observed{}, consumers_paused{};
    bool resource_view_ready{}, previous_callback_unwound{};
    int resolution{}; // Actual current screen width selects 320 or 640.
};

// Validate a relocated, mapped native client image before binding writable HUD
// fields. SHA values come from the verified original modules, before hooks.
// No hook or symbol registration is performed by this component.
const char* ValidateImage(std::span<const std::byte> image,
                          std::uintptr_t image_base,
                          std::string_view client_identity,
                          std::string_view engine_identity);
const char* ValidateSourceAllocator(std::span<const std::byte> engine_image,
                                    std::uintptr_t image_base,
                                    std::string_view engine_identity);

enum class Phase { idle, prepared, rebuilding, rebuilt, restore_required, restoring };
struct Result {
    const char* error{};
    bool changed{}, requires_restore{};
    explicit operator bool() const { return error == nullptr; }
};

class Refresh {
public:
    Refresh() = default;
    Refresh(const Refresh&) = delete;
    Refresh& operator=(const Refresh&) = delete;

    Result Prepare(const Admission&, const Binding&, DirectoryOwner);
    Result Rebuild(const Admission&);
    Result Commit(const Admission&);
    // Prepared cancellation needs no VFS change. After Rebuild, parent restores
    // old VFS/cache owners first, stays in the same paused signon, then calls
    // Rollback. It rebinds every native rect consumer before freeing new arrays.
    // A failed rollback keeps consumers paused and requires cancelling admission;
    // never resume drawing or start downloads while requires_restore is true.
    Result Rollback(const Admission&);
    // Invoke only when the parent has marked a resource-view reset or a failed
    // transaction at a new natural native VidInit. A pending callback must have
    // truly unwound (including Host_Error longjmp) before the hook calls this.
    // Normally issues one original call; interrupted transactions first rebind
    // their retained generation, then perform this initialization. Any failed
    // preflight/recovery stops, never silently issues a second native call.
    Result Initialize(const NativeInitialization&, const Binding&, DirectoryOwner);
    Phase phase() const { return phase_; }
    bool pending() const { return phase_ != Phase::idle; }

private:
    struct Snapshot {
        Catalog catalog;
        int selected{}, resolution{}, number{}, font_height{};
        std::array<void*, 3> arrays{};
        std::array<int, 5> textures{};
    };
    Snapshot read() const;
    void write(const Snapshot&) const;
    const char* gate(const Admission&, bool restoring = false) const;
    const char* validate_catalog(Catalog, int resolution) const;
    const char* validate_arrays(const Snapshot&) const;
    bool call_vidinit();
    Result stage(const Binding&, DirectoryOwner, int resolution);
    Result rebuild();
    Result commit();
    Result restore();
    void retire_restored_textures(const Snapshot& current);
    void release_arrays(const Snapshot&, const Snapshot&) const;
    void release_textures(const Snapshot&, const Snapshot&, const Snapshot&) const;
    void reset();

    Phase phase_{Phase::idle};
    bool busy_{};
    Binding binding_;
    DirectoryOwner owner_;
    Admission admission_;
    Snapshot old_, replacement_, restore_displaced_;
    Catalog candidate_;
    int target_resolution_{};
    std::uint64_t last_native_event_{};
};

}
