// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_trigger.h"

#include <cstdlib>

LINK_ENTITY_TO_CLASS(zbstrigger, CCSOLScenarioTrigger, CCSEntity)

void CCSOLScenarioTrigger::Spawn()
{
    pev->solid = SOLID_TRIGGER;
    pev->movetype = MOVETYPE_NONE;
    SET_MODEL(edict(), STRING(pev->model));
    pev->effects |= EF_NODRAW;
}

void CCSOLScenarioTrigger::KeyValue(KeyValueData* data)
{
    if (FStrEq(data->szKeyName, "id"))
    {
        m_iId = std::atoi(data->szValue);
        data->fHandled = TRUE;
        return;
    }
    CBaseEntity::KeyValue(data);
}

void CCSOLScenarioTrigger::Collect(csol::scenario::TriggerVolumes& volumes)
{
    // Original MP reads mins/maxs, not absmin/absmax. Do not translate by
    // origin or rotate brush bounds here; the manager compares these values.
    const auto& mins = pev->mins;
    const auto& maxs = pev->maxs;
    volumes.Register({{mins.x, mins.y, mins.z}, {maxs.x, maxs.y, maxs.z},
        m_iId, static_cast<std::size_t>(entindex())});
}
