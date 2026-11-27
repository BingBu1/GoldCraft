// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>

namespace csol::maneuver_gear
{
inline constexpr char Event[] = "events/maneuvergear.sc";
inline constexpr char WireSound[] = "giant_sounds/wire_shoot.wav";
inline constexpr char GasSound[] = "giant_sounds/gas.wav";
inline constexpr char WireSprite[] = "sprites/smoke.spr";

struct FuelImage
{
    const char* path;
    int width, height;
};
// Original client 02845060 / 02851160, in paint order.
inline constexpr std::array FuelImages{
    FuelImage{"resource/attackontitan/gasbg.tga", 359, 97},
    FuelImage{"resource/attackontitan/gasleft.tga", 108, 97},
    FuelImage{"resource/attackontitan/gasgauge.tga", 210, 16},
    FuelImage{"resource/attackontitan/gaspoint.tga", 5, 20},
};
}
