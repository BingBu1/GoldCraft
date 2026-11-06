#include "hud_sprites.hpp"
#include <algorithm>
#include <cstring>
#include <new>
#include <vector>

namespace goldcraft::hud_sprites {
namespace {
Loader original = nullptr;
std::vector<client_sprite_t> entries;
std::vector<client_sprite_t> staged;
SourceRelease source_release = nullptr;
std::vector<client_sprite_t*> pending_sources;
bool prepared{}, activated{};

void append_infection(std::vector<client_sprite_t>& target) {
    for (int resolution : {320, 640}) {
        if (std::ranges::any_of(target, [=](const client_sprite_t& entry) {
            return entry.iRes == resolution && !std::strncmp(entry.szName, "d_infection", sizeof(entry.szName));
        })) continue;
        client_sprite_t entry{};
        std::strcpy(entry.szName, "d_infection");
        std::strcpy(entry.szSprite, "zp_infection");
        entry.iRes = resolution; entry.rc = {0, 20, 0, 20};
        target.push_back(entry);
    }
}
const char* prepare(void*, resource_native_hud::Catalog current,
                    resource_native_hud::Catalog& replacement) {
    if (prepared) return "HUD directory update is already pending";
    if (!original || !source_release) return "native HUD directory allocator is unavailable";
    if (entries.empty() || current.entries != entries.data() || current.count != static_cast<int>(entries.size()))
        return "native HUD directory is not owned by hud_sprites";
    int count = 0;
    char path[] = "sprites/hud.txt";
    auto* source = original(path, &count);
    if (!source) return "native HUD directory could not be loaded";
    // The original loader returns a fresh calloc allocation; its free callback
    // was resolved from that same engine. Only our copied vector reaches gHUD.
    if (count <= 0 || count > 8190) {
        source_release(source);
        return "native HUD directory count is invalid";
    }
    try {
        std::vector<client_sprite_t> candidate;
        candidate.reserve(static_cast<std::size_t>(count) + 2);
        candidate.assign(source, source + count);
        append_infection(candidate);
        staged.swap(candidate);
    } catch (...) {
        source_release(source);
        return "cannot allocate native HUD directory generation";
    }
    source_release(source);
    prepared = true; activated = false;
    replacement = {staged.data(), static_cast<int>(staged.size())};
    return nullptr;
}
bool activate(void*) noexcept {
    if (!prepared || activated) return false;
    entries.swap(staged); activated = true; return true;
}
bool restore(void*) noexcept {
    if (!prepared) return false;
    if (activated) { entries.swap(staged); activated = false; }
    return true;
}
void finish(void*, bool commit) noexcept {
    if (!prepared) return;
    if (!commit && activated) { entries.swap(staged); activated = false; }
    // On commit staged owns the old generation; on abort it owns the candidate.
    std::vector<client_sprite_t>().swap(staged);
    prepared = activated = false;
}
}
bool bind(Loader loader) {
    // Client initialization only: the old client/gHUD has already been retired.
    if (prepared) return false;
    original = loader; entries.clear(); staged.clear();
    prepared = activated = false;
    return true;
}
bool bind_source_release(SourceRelease release) {
    if (!release || prepared || (source_release && source_release != release)) return false;
    source_release = release;
    for (auto* source : pending_sources) source_release(source);
    pending_sources.clear();
    return true;
}
resource_native_hud::DirectoryOwner resource_owner() {
    return {nullptr, prepare, activate, restore, finish};
}
client_sprite_t* list(char* path, int* count) {
    if (!original) { if (count) *count = 0; return nullptr; }
    if (!path || !count || _stricmp(path, "sprites/hud.txt")) return original(path, count);
    // The native HUD retains this array through video resets. Build it once
    // per client initialization and keep its address stable for that lifetime.
    if (entries.empty()) {
        auto* source = original(path, count);
        if (!source || *count <= 0 || *count > 8190) return source;
        try {
            entries.reserve(static_cast<std::size_t>(*count) + 2);
            entries.assign(source, source + *count);
            append_infection(entries);
            if (!source_release) pending_sources.push_back(source);
        } catch (...) {
            entries.clear();
            // Preserve original loader semantics for an unowned directory;
            // resource_owner rejects this pointer instead of guessing a free.
            return source;
        }
        if (source_release) source_release(source);
    }
    *count = static_cast<int>(entries.size());
    return entries.data();
}
}
