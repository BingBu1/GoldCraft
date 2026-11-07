// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "boss_skills.hpp"
#include <array>
#include <optional>

namespace csol::scenario::oberon
{
using Position = std::array<float, 3>;
struct Hit
{
    float damage{};
    Position impulse{};
};

// These are the original mode15 type3..7 numeric stages. The live owner
// supplies team/alive/lease gates and keeps each attack's callback order.
float InverseLength(float square);
std::array<float, 2> Forward(float yaw);
std::optional<Hit> Sector(const BossSkillRow&, const Position& boss,
    float minimumZ, float feetZ, float yaw, const Position& target);
std::optional<Hit> Cylinder(const BossSkillRow&, const Position& boss,
    float minimumZ, float maximumZ, float feetZ, bool suppressBelowFeetKill,
    const Position& target);
std::optional<Hit> Bomb(float damage, float radius, float kickSide, float kickUp,
    const Position& origin, const Position& target);

struct BombLaunch
{
    Position origin{}, velocity{};
    float flightTime{};
};
BombLaunch Launch(const Position& boss, float yaw, int offset,
    const std::array<float, 2>& target);
}
