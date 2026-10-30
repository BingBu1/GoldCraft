// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../scenario/config.hpp"

// State owned by one installed character ability. These are logical fields,
// not the original private ABI. Repeated combat preparation must not recreate
// them. The enclosing runtime owns player, equipment and round generations.
struct CSOLScenarioSkillEffectTimers
{
    float duration{};
    float pendingAt{-1};
    float activeUntil{-1};
};

// Actual host operations for original 106E4B30. No successful default methods:
// a concrete skill supplies every reached effect, including verified empty
// virtuals. Current/ReadTime are pure reads; they must not mutate state.
// This interface does not claim that the character animation or cloak host
// exists merely because its timing algorithm is implemented.
class CSOLScenarioSkillEffectHost
{
public:
    virtual ~CSOLScenarioSkillEffectHost() = default;
    virtual bool Current() const = 0;
    virtual bool ReadTime(float& now, csol::scenario::ConfigError& error) const = 0;
    virtual bool HasActionConstraint33(bool& present, csol::scenario::ConfigError& error) = 0;
    virtual bool CharacterActionPlaying(bool& playing, csol::scenario::ConfigError& error) = 0;
    virtual bool RemoveActionConstraint33(csol::scenario::ConfigError& error) = 0;
    virtual bool StartEffects(csol::scenario::ConfigError& error) = 0; // v44.
    virtual bool ActiveEffects(csol::scenario::ConfigError& error) = 0; // v48.
    virtual bool EndEffects(csol::scenario::ConfigError& error) = 0; // v4C.
    virtual bool UpdateEffects(csol::scenario::ConfigError& error) = 0; // v50.
};

// Called by the actual player update after activation has set pendingAt. The
// original character-action flag is required; IsAlive alone is not that flag.
bool CSOL_UpdateScenarioSkillEffects(CSOLScenarioSkillEffectTimers& timers,
    CSOLScenarioSkillEffectHost& host, csol::scenario::ConfigError& error);

// Original player+3430/+3434/+3438/+343C. One target has one such state; another
// radius-heal installation overwrites it, rather than stacking a new timer.
struct CSOLScenarioSkillHealing
{
    float nextAt{}, interval{}, expires{}, ratio{};
};

// The actual player status update must supply its normal life/mode gates and
// original 0.5-second outer schedule (114FA430 -> 114FCDD0). This is not a
// freely running per-frame recovery callback.
// Direct health reads/writes preserve 114FD648: this producer does not call the
// separate TakeHealth/ApplyRecovery path or emit those recovery callbacks.
class CSOLScenarioSkillHealingHost
{
public:
    virtual ~CSOLScenarioSkillHealingHost() = default;
    virtual bool Current() const = 0;
    virtual bool ReadTime(float& now, csol::scenario::ConfigError& error) const = 0;
    virtual bool ReadHealth(float& health, float& maximum,
        csol::scenario::ConfigError& error) const = 0;
    virtual bool WriteHealth(float health, csol::scenario::ConfigError& error) = 0;
    virtual bool NotifyRecoveryPulse(csol::scenario::ConfigError& error) = 0; // effect9.
};

// Only the four-field installation from 106E07F0. The caller must perform its
// real alive/radius test and the original duration/status-label notifications.
bool CSOL_InstallScenarioSkillHealing(CSOLScenarioSkillHealing& healing,
    float duration, float interval, float ratio,
    CSOLScenarioSkillHealingHost& host, csol::scenario::ConfigError& error);

// At most one pulse per scheduled status update; advances from the previous
// deadline, not now. It never catches up all missed seconds in one call.
bool CSOL_UpdateScenarioSkillHealing(CSOLScenarioSkillHealing& healing,
    CSOLScenarioSkillHealingHost& host, bool& applied,
    csol::scenario::ConfigError& error);
