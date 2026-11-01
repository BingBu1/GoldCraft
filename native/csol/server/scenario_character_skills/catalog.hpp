// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <span>

namespace csol::scenario::skills
{
// These describe actual original callback families, not implementation status.
// Empty is reserved for the verified shared ret16 body. A caller must dispatch
// every other value to the corresponding effect implementation.
enum class DamageCallback : std::uint8_t
{
    Empty,
    Type16,
    StopCloaking11,
    Type28,
    StopCloaking12,
    Type31,
    StopCloaking13,
    Accumulate46,
    Recover48,
    Recover49,
    Recover50,
};

struct SkillTraits
{
    int subtype;
    bool hasCurrentDefinition;
    bool automaticTrigger;
    bool requiresCharacterAction;
    bool requiresScenarioTarget;
    bool damageProgress;
    DamageCallback damageCallback;
    bool emptyNotification28;
    bool emptyNotification2C;
    bool emptyLifecycle30;
    bool emptyLifecycle34;
    bool emptyLifecycle38;
    bool emptyInitialize3C;
    bool emptyActive48;
    bool emptyEnd4C;
    bool emptyUpdate50;
};

// Includes the original factory's subtype35, even though the current common
// ClassMod table does not offer it. Definitions/loadout authority remain with
// the existing ClassMod loader; this table must never grant an ability.
std::span<const SkillTraits> AllSkills() noexcept;
const SkillTraits* FindSkill(int subtype) noexcept;
}
