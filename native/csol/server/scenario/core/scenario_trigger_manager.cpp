// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_entity_lookup.h"
#include "scenario_trigger_manager.h"

#include <vector>

void CCSOLTriggerManager::BeginMap()
{
    m_volumes.Clear();
    m_status = CSOLTriggerCollection::NotCollected;
}

CSOLTriggerCollection CCSOLTriggerManager::CollectMap()
{
    const auto access = m_access;
    if (!access || !access->Allowed()) return CSOLTriggerCollection::NotCollected;
    if (m_status != CSOLTriggerCollection::NotCollected) return m_status;
    std::vector<EHANDLE> markers;
    CBaseEntity* entity = nullptr;
    while ((entity = CSOL_FindScenarioEntity(entity, "zbstrigger", access)))
    {
        if (!access->Allowed() || m_access != access) return CSOLTriggerCollection::NotCollected;
        if (!UTIL_IsValidEntity(entity->edict())) continue;
        auto* trigger = dynamic_cast<CCSOLScenarioTrigger*>(entity);
        if (!trigger) continue;
        trigger->Collect(m_volumes);
        if (!access->Allowed() || m_access != access) return CSOLTriggerCollection::NotCollected;
        markers.emplace_back(entity);
    }
    if (!access->Allowed() || m_access != access) return CSOLTriggerCollection::NotCollected;
    // Preserve snapshots before any plugin-visible removal callback.
    for (const auto& handle : markers)
    {
        if (!access->Allowed() || m_access != access) return CSOLTriggerCollection::NotCollected;
        entity = handle;
        if (entity && UTIL_IsValidEntity(entity->edict())) UTIL_Remove(entity);
    }
    if (!access->Allowed() || m_access != access) return CSOLTriggerCollection::NotCollected;
    // MAP_TRG is an all-empty fallback. Never merge it into valid BSP geometry:
    // zs_deadend's fallback ID 50 points at an older, different brush volume.
    return m_status = m_volumes.Volumes().empty() ?
        CSOLTriggerCollection::NeedsMapTriggerFile : CSOLTriggerCollection::Ready;
}
