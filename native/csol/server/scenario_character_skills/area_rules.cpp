// SPDX-License-Identifier: GPL-3.0-or-later
#include "area_rules.hpp"
#include <algorithm>
#include <bit>
#include <cstdint>

#pragma clang fp contract(off)
namespace csol::scenario::skills
{
bool SkillAreaTargetAllowed(bool player, int classification,
    bool includeProps, bool includeClassification43) noexcept
{
    // Original1152FDF0 has separate living-target and optional prop tables.
    // Classification43 is optional only inside the prop branch.
    if (player) return true;
    switch (classification)
    {
    case 17: case 20: case 27: case 30: case 33: case 34:
    case 45: case 50: case 55: return true;
    case 16: case 19: case 23: return includeProps;
    case 43: return includeProps && includeClassification43;
    default: return false;
    }
}

LotusValues LotusDifficultyValues(int difficulty) noexcept
{
    // 106EF6D0 clamps the row before subtracting one, with no attack-level
    // multiplier. The two shield parameters currently have equal defaults.
    const int row = std::clamp(difficulty, 1, 7) - 1;
    return {100.0f + static_cast<float>(row) * 200.0f,
        1000.0f + static_cast<float>(row) * 700.0f,
        1000 + row * 200, 1000 + row * 200};
}

bool LotusMonsterKnockback(int skill, int eventClass, int originalTypeId) noexcept
{
    // 1152FD70 -> 113800F0/11380150, followed by106E8908's original row ID.
    return skill < 1000 && eventClass <= 149 && originalTypeId != 0x8020;
}

float SkillAreaDamage(float squared, float radius, float center, float edge) noexcept
{
    if (!(radius * radius > squared)) return 0;
    // 114BBE90 is the original one-step inverse-square-root approximation,
    // not sqrtf. Retain its float32 intermediates and magic constant.
    const auto bits = std::bit_cast<std::int32_t>(squared);
    const float estimate = std::bit_cast<float>(0x5f3759d5 - (bits >> 1));
    float half = squared * 0.5f;
    half *= estimate;
    half *= estimate;
    const float inverse = (1.5f - half) * estimate;
    const float distance = 1.0f / inverse;
    const float fraction = (radius - distance) / radius;
    return std::max(0.0f, fraction) * (center - edge) + edge;
}
}
