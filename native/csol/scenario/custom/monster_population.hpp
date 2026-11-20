// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

namespace csol::scenario::custom
{
struct HordeControls
{
    double intervalSeconds{}, probability{}, durationSeconds{};
    int extraZombies{};
};

struct MonsterPopulationControls
{
    int base{}, perPlayer{};
    std::optional<HordeControls> horde;
};
}
