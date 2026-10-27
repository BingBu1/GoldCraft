// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "death_notices.hpp"
#include <algorithm>
#include <cstring>

namespace csol::client
{
namespace
{
decltype(engine.pfnSPR_GetList) originalGetList{};
// CHud retains the returned sprite-list pointer across video/session resets.
// Build once and keep its address stable for the lifetime of the client DLL.
std::vector<client_sprite_t> hudSprites;

client_sprite_t* GetSpriteList(char* path, int* count)
{
    auto* original = originalGetList(path, count);
    if (!path || std::strcmp(path, "sprites/hud.txt") || !count || !original || *count <= 0)
        return original;
    if (hudSprites.empty())
    {
        hudSprites.assign(original, original + *count);
        struct Entry {const char *name,*sprite;int y;};
        const Entry entries[]{{"d_tknifeex2","csol/640hud12",32},{"d_beamgun","csol/640hud266",0},{"d_beamgunle","csol/640hud266",16},{"d_cannon","csol/640hud6",160}};
        for (const auto& entry : entries) for (const int resolution : {320, 640})
        {
            const bool present = std::any_of(hudSprites.begin(), hudSprites.end(), [=](const auto& icon)
            { return icon.iRes == resolution && std::strcmp(icon.szName, entry.name) == 0; });
            if (present) continue;
            client_sprite_t icon{};
            std::strcpy(icon.szName, entry.name);
            std::strcpy(icon.szSprite, entry.sprite);
            icon.iRes = resolution;
            // Original CSO hud.txt: x=172, y=32, width=48, height=16.
            icon.rc = {172, 220, entry.y, entry.y+16};
            hudSprites.push_back(icon);
        }
    }
    *count = static_cast<int>(hudSprites.size());
    return hudSprites.data();
}
}

void InitializeDeathNotices()
{
    if (originalGetList) return;
    Hook(reinterpret_cast<void*>(engine.pfnSPR_GetList), reinterpret_cast<void*>(GetSpriteList),
        reinterpret_cast<void**>(&originalGetList));
}
}
