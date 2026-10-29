// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cbase.h"
#include "triggers.h"
#include "scenario_breakable_guard.h"
#include "scenario_entity_access.h"
#include "../scenario/trigger_volumes.hpp"
#include <cstdint>
#include <memory>

// A brush marker consumed by the scenario manager, not a Touch callback.
class CCSOLScenarioTrigger : public CBaseEntity
{
public:
    void Spawn() override;
    void KeyValue(KeyValueData* data) override;
    void Collect(csol::scenario::TriggerVolumes& volumes);
    int m_iId{};
};

// Progress completion invokes OnControls; this brush is never touch-driven.
// Its return requests protection, independently of whether master/nextthink
// allowed the inherited target chain to run.
class CCSOLScenarioClearPoint final : public CBaseTrigger
{
public:
    void Spawn() override;
    void KeyValue(KeyValueData* data) override;
    BOOL OnControls(entvars_t* activator) override;
    std::uint64_t ControlRevision() const { return m_revision; }
    void OnDestroy() override;
private:
    CSOLBreakableScope::Guard Guard(std::shared_ptr<const CSOLScenarioEntityAccess> access) const;
    std::uint64_t m_revision{};
    bool m_activated{}, m_allowProtection{true};
};
