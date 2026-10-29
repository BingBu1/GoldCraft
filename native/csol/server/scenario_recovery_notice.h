// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_player_runtime.h"

// Kind7 and null-source module26 recovery both notify the recovering player.
// Combat owns the real MODE resource and message ID.
// The rule uses the live ability28/29/3 state for a special-character interval.
// A missing trusted directory remains unknown and fails explicitly.
class CCSOLScenarioRecoveryNotice final : public CSOLScenarioRecoveryNotice
{
public:
    explicit CCSOLScenarioRecoveryNotice(std::weak_ptr<CCSOLScenarioRuntime> runtime)
        : m_runtime(std::move(runtime)) {}
    bool NotifySelfRecovery(CSOLPlayerRecoveryCall& call, float amount,
        CSOLMonsterDeathContext& context, csol::scenario::ConfigError& error,
        bool (*ownerValid)(const void*), const void* ownerContext) override;
private:
    std::weak_ptr<CCSOLScenarioRuntime> m_runtime;
};
