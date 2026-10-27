// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cbase.h"
#include "../scenario/trigger_volumes.hpp"

// A brush marker consumed by the scenario manager, not a Touch callback.
class CCSOLScenarioTrigger : public CBaseEntity
{
public:
    void Spawn() override;
    void KeyValue(KeyValueData* data) override;
    void Collect(csol::scenario::TriggerVolumes& volumes);
    int m_iId{};
};
