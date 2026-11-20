// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace guest::cof::light
{
// The host shop assigns prices and separate IDs to these two utility items.
// Original CoF IDs 10 and 13 collide with ordinary CS weapons.
struct Equipment
{
    int id;
    int hudSlot;
    int price;
    const char* classname;
    const char* title;
    const char* view;
    const char* player;
    const char* world;
};

inline constexpr Equipment Lantern{
    976, 4, 600, "weapon_cof_lantern", "客串武器·提灯",
    "models/guest/cof/lantern/v_lantern.mdl", "models/guest/cof/lantern/p_lantern.mdl",
    "models/guest/cof/lantern/w_lantern.mdl"
};
inline constexpr Equipment Flare{
    977, 3, 150, "weapon_cof_flare", "客串武器·照明弹",
    "models/guest/cof/flare/v_flare.mdl", "models/guest/cof/flare/p_flare.mdl",
    "models/guest/cof/flare/w_flare.mdl"
};
// The player's two-bit entity_state iuser4 reserves bit 0 for CS vehicles.
// Bit 1 inverts the held-model default, including for observers entering PVS.
inline constexpr int LanternStateBit = 2;
constexpr bool LanternLit(bool heldModel, int playerState) noexcept
{
    return heldModel != ((playerState & LanternStateBit) != 0);
}

inline constexpr int MaximumFlares = 5;
inline constexpr char ThrowCommand[] = "cof_throwflare";
// A distinct precache index carries the lit third-person state through GoldSrc's
// existing weaponmodel field, including PVS entry and in-eye observation.
inline constexpr char FlareLitPlayer[] = "models/guest/cof/flare/p_flare_lit.mdl";

constexpr const Equipment* Find(int id) noexcept
{
    if (id == Lantern.id) return &Lantern;
    if (id == Flare.id) return &Flare;
    return nullptr;
}
}
