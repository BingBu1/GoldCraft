// SPDX-License-Identifier: GPL-3.0-or-later
#include "catalog.hpp"
#include <array>

namespace csol::scenario::skills
{
namespace
{
// Verified factory and vtable families of the supplied MP. See the character
// skill inventory report for exact binary/resource identities and coverage.
constexpr std::array<SkillTraits, 55> Skills{{
    {1, true, false, true, false, false, DamageCallback::Empty, true, true, true, true, true, true, true, true, true},
    {2, true, false, true, false, false, DamageCallback::Empty, true, true, true, true, true, true, true, true, true},
    {3, true, false, true, false, false, DamageCallback::Empty, true, true, true, true, true, true, true, true, true},
    {4, true, false, true, false, false, DamageCallback::Empty, true, true, true, true, true, true, true, true, true},
    {5, true, false, true, false, false, DamageCallback::Empty, true, true, true, true, true, true, true, true, true},
    {6, true, false, true, false, false, DamageCallback::Empty, true, true, true, true, true, true, true, true, true},
    {7, true, false, true, false, false, DamageCallback::Empty, true, true, true, true, true, true, true, true, true},
    {8, true, false, true, false, false, DamageCallback::Empty, true, true, true, true, true, true, true, true, true},
    {9, true, false, true, false, false, DamageCallback::Empty, true, true, true, true, true, true, true, true, true},
    {10, true, true, false, false, false, DamageCallback::Empty, true, true, true, true, true, true, true, true, true},
    {11, true, true, false, false, false, DamageCallback::Empty, true, true, true, true, true, true, true, true, true},
    {12, true, false, true, false, false, DamageCallback::Empty, true, true, true, true, true, true, true, true, true},
    {13, true, true, false, false, false, DamageCallback::Empty, true, true, true, true, true, true, true, true, true},
    {14, true, false, true, false, false, DamageCallback::Empty, true, true, true, true, true, false, true, true, true},
    {15, true, false, true, false, false, DamageCallback::Empty, true, true, true, true, true, true, true, true, true},
    {16, true, false, true, false, false, DamageCallback::Type16, true, true, true, true, true, true, true, true, true},
    {17, true, false, true, false, false, DamageCallback::Empty, true, true, true, true, true, true, true, true, true},
    {18, true, false, true, false, false, DamageCallback::Empty, true, true, true, true, true, true, true, true, true},
    {19, true, false, true, false, false, DamageCallback::Empty, true, true, true, true, true, true, true, true, true},
    {20, true, false, true, false, false, DamageCallback::Empty, true, true, true, true, true, true, true, true, true},
    {21, true, true, false, true, false, DamageCallback::Empty, true, true, true, true, true, true, true, true, false},
    {22, true, false, true, false, false, DamageCallback::StopCloaking11, true, true, false, true, true, true, true, true, true},
    {23, true, false, true, false, false, DamageCallback::StopCloaking11, true, true, false, true, true, true, true, true, true},
    {24, true, false, true, false, false, DamageCallback::Empty, true, true, false, false, false, false, true, false, false},
    {25, true, false, true, false, false, DamageCallback::Empty, true, true, false, false, false, false, true, false, false},
    {26, true, false, true, false, false, DamageCallback::Empty, true, true, true, true, true, false, true, true, true},
    {27, true, false, true, false, false, DamageCallback::Empty, true, true, true, true, true, false, true, true, true},
    {28, true, false, true, false, false, DamageCallback::Type28, true, true, true, true, true, true, true, true, true},
    {29, true, false, true, false, false, DamageCallback::StopCloaking12, true, true, true, true, true, false, true, true, true},
    {30, true, false, true, false, false, DamageCallback::Empty, true, true, true, true, true, false, false, true, true},
    {31, true, false, true, false, false, DamageCallback::Type31, true, true, true, true, true, false, true, true, true},
    {32, true, false, true, false, false, DamageCallback::Empty, true, true, true, true, true, true, false, true, true},
    {33, true, false, true, false, false, DamageCallback::Empty, true, true, true, true, true, true, false, true, true},
    {34, true, false, true, false, false, DamageCallback::Empty, true, true, true, true, true, true, true, true, true},
    {35, false, false, true, false, false, DamageCallback::Empty, true, true, true, true, true, true, true, true, true},
    {36, true, false, true, false, false, DamageCallback::Empty, false, true, true, false, false, true, false, false, true},
    {37, true, false, true, false, false, DamageCallback::Empty, true, true, true, true, true, false, true, true, true},
    {38, true, false, true, false, false, DamageCallback::Empty, true, true, true, true, true, false, true, false, true},
    {39, true, false, true, false, false, DamageCallback::StopCloaking13, true, true, true, true, true, false, true, true, true},
    {40, true, false, true, false, false, DamageCallback::Empty, true, true, true, true, true, false, true, true, true},
    {41, true, false, true, false, false, DamageCallback::Empty, true, true, true, true, true, false, true, true, true},
    {42, true, false, true, false, false, DamageCallback::Empty, true, true, true, true, true, false, true, true, true},
    {43, true, false, true, false, false, DamageCallback::Empty, true, true, true, true, true, false, true, false, true},
    {44, true, false, true, false, false, DamageCallback::Empty, true, true, true, true, true, false, true, false, true},
    {45, true, false, true, false, false, DamageCallback::Empty, true, true, true, true, true, true, true, false, true},
    {46, true, false, true, false, false, DamageCallback::Accumulate46, true, true, true, true, true, false, true, false, true},
    {47, true, false, true, false, false, DamageCallback::Empty, true, true, true, true, true, true, true, true, true},
    {48, true, false, true, false, false, DamageCallback::Recover48, true, true, true, true, true, true, true, true, true},
    {49, true, false, true, false, false, DamageCallback::Recover49, false, true, true, true, true, false, true, true, true},
    {50, true, false, true, false, false, DamageCallback::Recover50, false, true, true, true, true, false, true, true, true},
    {51, true, false, true, false, false, DamageCallback::Empty, true, false, true, true, true, false, true, false, true},
    {52, true, false, true, false, false, DamageCallback::Empty, true, false, true, true, true, false, true, false, true},
    {53, true, false, true, false, false, DamageCallback::Empty, true, true, true, true, true, false, true, false, true},
    {54, true, false, true, false, false, DamageCallback::Empty, true, true, true, true, true, false, true, true, true},
    {55, true, false, true, false, false, DamageCallback::Empty, true, true, true, true, true, false, false, true, true},
}};
}

std::span<const SkillTraits> AllSkills() noexcept { return Skills; }

const SkillTraits* FindSkill(int subtype) noexcept
{
    return subtype >= 1 && subtype <= static_cast<int>(Skills.size())
        ? &Skills[static_cast<unsigned>(subtype - 1)] : nullptr;
}
}
