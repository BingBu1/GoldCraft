// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../scenario_active_skill_effects.h"
#include <cstdint>

namespace csol::scenario::skills
{
struct SkillTraits;

// Fields owned by one installed capability. Re-preparing the same character
// must retain this state. No private MP offsets are used by the host.
struct ActivationState
{
    float cooldown{};
    float readyAt{-1};
    float delay{};
    float actionConstraint{3};
    std::int32_t progress{}, requiredProgress{};
    CSOLScenarioSkillEffectTimers effects;
};

// Each bool is operation success; ordinary refusal is written to its output.
// Current() includes the skill, player, map and character-selection generation.
// Every reached original effect must have a real implementation, including a
// verified empty virtual where appropriate. There are no successful defaults.
class ActivationHost : public CSOLScenarioSkillEffectHost
{
public:
    virtual bool FrozenWrathBlocks(bool& blocked, ConfigError& error) = 0;
    virtual bool PlayerAlive(bool& alive, ConfigError& error) = 0;
    virtual bool ScenarioTargetAvailable(bool& available, ConfigError& error) = 0;
    virtual bool NotifyNotReady(ConfigError& error) = 0;
    virtual bool StartCharacterAction5(bool& started, ConfigError& error) = 0;
    // v40 sets the actual delay/constraint and prepares visuals; v44 in the
    // inherited effect host performs the delayed gameplay effect.
    virtual bool PrepareEffects(ActivationState& state, bool& prepared, ConfigError& error) = 0;
    virtual bool AddActionConstraint33(float duration, ConfigError& error) = 0;
    virtual bool AdjustCooldown(float requestedReadyAt, float& readyAt, ConfigError& error) = 0;
    virtual bool PlayerModelId(int& modelId, ConfigError& error) = 0;
    virtual bool PlayActivationVoice(ConfigError& error) = 0;
    // Original 0x24/0x26 sender owns the live UI-delay setting; it is not the
    // active duration. The adapter must preserve float32 packet fields.
    virtual bool PublishActivation(int subtype, bool progress, float value, ConfigError& error) = 0;
};

enum class ActivationResult { Started, NotReady, ActionRefused, PreparationRefused, Cancelled, Failed };

bool Ready(const SkillTraits& traits, const ActivationState& state,
    ActivationHost& host, bool& ready, ConfigError& error);
ActivationResult Activate(const SkillTraits& traits, ActivationState& state,
    ActivationHost& host, ConfigError& error);
bool Update(ActivationState& state, ActivationHost& host, ConfigError& error);

// 114CED70: MIN reduction against 1 only; negative reductions lengthen the
// cooldown. Group selection/expiry belongs to the real StatBuff owner.
float AdjustCooldown(float now, float requestedReadyAt, float reduction);
}
