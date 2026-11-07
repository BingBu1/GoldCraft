// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_entity_access.h"
#include "cbase.h"
#include "ehandle.h"
#include <memory>

// The SDK helpers resolve private data internally after an engine callback.
// Scenario callers need the revocation check between lookup and that read.
// Classname collection uses the engine's ascending-edict lookup, preserving
// the SDK classname hash traversal order without borrowing its internal nodes.
struct CSOLScenarioEntityLookup
{
    CBaseEntity* entity{};
    bool valid{}; // valid+null is exhaustion; invalid is cancellation/failure.
};

inline CSOLScenarioEntityLookup CSOL_LookupScenarioEntity(CBaseEntity* start, const char* classname,
    const std::shared_ptr<const CSOLScenarioEntityAccess>& access)
{
    if (!access || !access->Allowed() || !g_engfuncs.pfnFindEntityByString) return {};
    auto* entity = FIND_ENTITY_BY_STRING(start ? start->edict() : nullptr, "classname", classname);
    if (!access->Allowed()) return {};
    // Engine lookup returns null (or the world sentinel) on exhaustion.
    // A non-null edict which was freed by a callback is not that sentinel.
    if (!entity) return {nullptr, true};
    if (entity->free || !g_engfuncs.pfnEntOffsetOfPEntity) return {};
    const EHANDLE found(entity);
    const auto offset = OFFSET(entity);
    if (!access->Allowed() || found.Get() != entity || entity->free) return {};
    if (!offset) return {nullptr, true};
    auto* instance = CBaseEntity::Instance(entity);
    if (!instance || (instance->pev->flags & FL_KILLME)) return {};
    return {instance, true};
}

inline CBaseEntity* CSOL_FindScenarioEntity(CBaseEntity* start, const char* classname,
    const std::shared_ptr<const CSOLScenarioEntityAccess>& access)
{
    if (!access || !access->Allowed()) return nullptr;
    auto* entity = FIND_ENTITY_BY_STRING(start ? start->edict() : nullptr, "classname", classname);
    if (!access->Allowed()) return nullptr;
    const EHANDLE found(entity);
    const bool missing = FNullEnt(entity);
    if (!access->Allowed() || missing || found.Get() != entity || entity->free) return nullptr;
    return CBaseEntity::Instance(entity);
}

inline CBaseEntity* CSOL_ScenarioPlayerEntity(int index,
    const std::shared_ptr<const CSOLScenarioEntityAccess>& access)
{
    if (!access || !access->Allowed() || !gpGlobals || index < 1 || index > gpGlobals->maxClients) return nullptr;
    auto* entity = INDEXENT(index);
    if (!access->Allowed() || !entity || entity->free) return nullptr;
    return CBaseEntity::Instance(entity);
}
