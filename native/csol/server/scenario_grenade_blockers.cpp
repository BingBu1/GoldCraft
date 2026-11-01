// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_grenade_blockers.h"
#include "scenario_entity_access.h"
#include <vector>

namespace
{
struct Entry { EHANDLE entity; bool blocked; };
std::shared_ptr<const CSOLScenarioEntityAccess> g_access;
std::vector<Entry> g_entries;
}
void CSOL_BeginBreakableGrenadeMap(std::shared_ptr<const CSOLScenarioEntityAccess> access)
{
    g_entries.clear();
    g_access = access && access->Allowed() ? std::move(access) : nullptr;
}
void CSOL_RetireBreakableGrenadeMap(const std::shared_ptr<const CSOLScenarioEntityAccess>& access)
{
    if (g_access != access) return;
    g_entries.clear();
    g_access.reset();
}
void CSOL_SetBreakableGrenadeBlock(CBaseEntity& entity, bool blocked)
{
    // KeyValue precedes Session::Activate. No player/runtime activation gate
    // belongs here. Missing fields retain the original constructor's false.
    if (!g_access || !g_access->Allowed()) return;
    for (auto& entry : g_entries)
        if (entry.entity.Get<CBaseEntity>() == &entity) { entry.blocked = blocked; return; }
    g_entries.push_back({EHANDLE(&entity), blocked});
}
bool CSOL_BreakableBlocksGrenade(const CBaseEntity& entity)
{
    if (!g_access || !g_access->Allowed()) return false;
    for (const auto& entry : g_entries)
        if (entry.entity.Get<CBaseEntity>() == &entity) return entry.blocked;
    return false;
}
