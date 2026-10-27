// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_trigger_manager.h"

#include <vector>

void CCSOLTriggerManager::BeginMap()
{
    m_volumes.Clear();
    m_status = CSOLTriggerCollection::NotCollected;
}

CSOLTriggerCollection CCSOLTriggerManager::CollectMap()
{
    if (m_status != CSOLTriggerCollection::NotCollected) return m_status;
    std::vector<EHANDLE> markers;
    CBaseEntity* entity = nullptr;
    while ((entity = UTIL_FindEntityByClassname(entity, "zbstrigger")))
    {
        if (!UTIL_IsValidEntity(entity->edict())) continue;
        auto* trigger = dynamic_cast<CCSOLScenarioTrigger*>(entity);
        if (!trigger) continue;
        trigger->Collect(m_volumes);
        markers.emplace_back(entity);
    }
    // Preserve snapshots before any plugin-visible removal callback.
    for (const auto& handle : markers)
    {
        entity = handle;
        if (entity && UTIL_IsValidEntity(entity->edict())) UTIL_Remove(entity);
    }
    // MAP_TRG is an all-empty fallback. Never merge it into valid BSP geometry:
    // zs_deadend's fallback ID 50 points at an older, different brush volume.
    return m_status = m_volumes.Volumes().empty() ?
        CSOLTriggerCollection::NeedsMapTriggerFile : CSOLTriggerCollection::Ready;
}
