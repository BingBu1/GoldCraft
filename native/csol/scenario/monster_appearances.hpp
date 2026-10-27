// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace csol::scenario
{
struct MonsterAppearance
{
    std::uint16_t index{};
    std::string_view name;
    std::string_view model;
    std::array<float, 3> mins{};
    std::array<float, 3> maxs{};
    bool healthSummary{};
    // Original appearance record +0x2c and its death-sound entry. The death
    // timer is independent of animation length; zero seconds is valid.
    float deathSeconds{};
    std::string_view deathSound;
    // Original appearance +0x20/+0x24/+0x28. These are independent of
    // the collision hull (notably Deimos and Oberon).
    float feetZ{};
    float centroidZ{};
    float eyesZ{};
    // Original appearance +0x34. Null in 140 of the 143 ordered rows.
    std::string_view ambientSound{};
};

inline constexpr std::size_t MonsterAppearanceCount = 143;
std::span<const MonsterAppearance, MonsterAppearanceCount> MonsterAppearances();
// Original mode15 catalog: exact, case-sensitive name; first match wins.
// A numeric string is a name, not an index. Unknown names have no fallback.
const MonsterAppearance* FindMonsterAppearance(std::string_view name);
}
