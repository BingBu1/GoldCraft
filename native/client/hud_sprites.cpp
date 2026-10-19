#include "hud_sprites.hpp"
#include <algorithm>
#include <cstring>
#include <vector>

namespace goldcraft::hud_sprites {
namespace {
Loader original = nullptr;
std::vector<client_sprite_t> entries;
}
void bind(Loader loader) { original = loader; entries.clear(); }
client_sprite_t* list(char* path, int* count) {
    if (!original) { if (count) *count = 0; return nullptr; }
    if (!path || !count || _stricmp(path, "sprites/hud.txt")) return original(path, count);
    // The native HUD retains this array through video resets. Build it once
    // per client initialization and keep its address stable for that lifetime.
    if (entries.empty()) {
        auto* source = original(path, count);
        if (!source || *count <= 0 || *count > 8192) return source;
        entries.reserve(static_cast<std::size_t>(*count) + 2);
        entries.assign(source, source + *count);
        for (int resolution : {320, 640}) {
            if (std::ranges::any_of(entries, [=](const client_sprite_t& entry) {
                return entry.iRes == resolution && !std::strcmp(entry.szName, "d_infection");
            })) continue;
            client_sprite_t entry{};
            std::strcpy(entry.szName, "d_infection");
            std::strcpy(entry.szSprite, "zp_infection");
            entry.iRes = resolution; entry.rc = {0, 20, 0, 20};
            entries.push_back(entry);
        }
    }
    *count = static_cast<int>(entries.size());
    return entries.data();
}
}
