// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace csol::scenario::skills
{
bool SkillAreaTargetAllowed(bool player, int classification,
    bool includeProps = true, bool includeClassification43 = true) noexcept;
float SkillAreaDamage(float distanceSquared, float radius, float center, float edge) noexcept;
struct LotusValues
{
    float ordinaryDamage{}, fixedDamage{};
    int selfShield{}, allyShield{};
};
LotusValues LotusDifficultyValues(int difficulty) noexcept;
bool LotusMonsterKnockback(int skill, int eventClass, int originalTypeId) noexcept;
}
