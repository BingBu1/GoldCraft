// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../runtime.hpp"
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
        struct Entry {const char *name,*sprite;int y;int x{172};int width{48};};
        const Entry entries[]{
            {"d_tknifeex2","csol/640hud12",32},{"d_beamgun","csol/640hud266_v2",0},
            {"d_beamgunle","csol/640hud266_v2",16},{"d_cannon","csol/640hud6",160},
            {"d_k3","csol/640hud11",64},{"d_skull1","csol/640hud4",48,220,32},
            {"d_skull5","csol/640hud4",144},
            {"d_skull2","csol/640hud38",0},
            {"d_skull3","csol/640hud6",128},{"d_skull3d","csol/640hud6",144},
            {"d_m249ex","csol/640hud3_v2",240,122},{"d_balrog11","csol/640hud11",144},
            {"d_dragonsword","csol/640hud10",160},{"d_skullaxe","csol/640hud4",96}};
        for (const auto& entry : entries) for (const int resolution : {320, 640})
        {
            const bool present = std::any_of(hudSprites.begin(), hudSprites.end(), [=](const auto& icon)
            { return icon.iRes == resolution && std::strcmp(icon.szName, entry.name) == 0; });
            if (present) continue;
            client_sprite_t icon{};
            std::strcpy(icon.szName, entry.name);
            std::strcpy(icon.szSprite, entry.sprite);
            icon.iRes = resolution;
            // Original hud.txt has per-weapon widths (SKULL-1 is 32 by 16).
            icon.rc = {entry.x, entry.x+entry.width, entry.y, entry.y+16};
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
