// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_trigger.h"
#include "game_rules_gate.h"

#include <cstdlib>
#include <utility>

LINK_ENTITY_TO_CLASS(zbstrigger, CCSOLScenarioTrigger, CCSEntity)
LINK_ENTITY_TO_CLASS(trigger_clearpoint, CCSOLScenarioClearPoint, CCSTrigger)

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

CSOLBreakableScope::Guard CCSOLScenarioClearPoint::Guard(
    std::shared_ptr<const CSOLScenarioEntityAccess> access) const
{
    const EHANDLE handle(const_cast<CCSOLScenarioClearPoint*>(this));
    return [access = std::move(access), handle, self = this, revision = m_revision]()
    {
        return access && access->Allowed() && handle.Get<CBaseEntity>() == self &&
            !(self->pev->flags & FL_KILLME) && self->m_revision == revision;
    };
}

void CCSOLScenarioClearPoint::Spawn()
{
    CSOLScenarioSdkScope sdk;
    if (!sdk.Scenario() || !sdk.Current()) return;
    ++m_revision;
    // An explicit re-Spawn is a new entity operation. It invalidates the old
    // target chain without inheriting that chain's now-stale revision guard.
    CSOLBreakableScope scope(Guard(sdk.EntityAccess()), true);
    InitTrigger();
    if (!CSOLBreakableScope::Current(this)) return;
    pev->solid = SOLID_NOT;
    SetTouch(nullptr);
    SetUse(nullptr);
    SetThink(nullptr);
    m_activated = false;
    m_flWait = -2.0f;
}

void CCSOLScenarioClearPoint::KeyValue(KeyValueData* data)
{
    if (FStrEq(data->szKeyName, "nogodmode"))
    {
        m_allowProtection = std::atoi(data->szValue) == 0;
        data->fHandled = TRUE;
        return;
    }
    CBaseTrigger::KeyValue(data);
}

BOOL CCSOLScenarioClearPoint::OnControls(entvars_t* activator)
{
    CSOLScenarioSdkScope sdk;
    if (!sdk.Scenario() || !sdk.Current()) return FALSE;
    CSOLBreakableScope scope(Guard(sdk.EntityAccess()));
    if (!CSOLBreakableScope::Current(this)) return FALSE;
    if (m_activated) return TRUE;
    // Match Instance's world fallback, but check access after that engine
    // lookup before reading private data. Never allocate a missing activator.
    CBaseEntity* entity = this;
    if (activator)
    {
        auto* edict = ENT(activator);
        if (!edict) edict = ENT(0);
        if (!CSOLBreakableScope::Current(this)) return FALSE;
        entity = edict ? GET_PRIVATE<CBaseEntity>(edict) : nullptr;
    }
    if (!CSOLBreakableScope::Current(this)) return FALSE;
    ActivateMultiTrigger(entity);
    if (!CSOLBreakableScope::Current(this)) return FALSE;
    m_activated = true;
    return m_allowProtection ? TRUE : FALSE;
}

void CCSOLScenarioClearPoint::OnDestroy()
{
    ++m_revision;
    CBaseTrigger::OnDestroy();
}
