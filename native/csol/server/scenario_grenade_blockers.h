// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <memory>
class CBaseEntity;
class CSOLScenarioEntityAccess;

// Begin after world precache creates the map token, before map entity keys.
// Retire before revoking that token. An old retirement cannot clear a new map.
void CSOL_BeginBreakableGrenadeMap(std::shared_ptr<const CSOLScenarioEntityAccess> access);
void CSOL_RetireBreakableGrenadeMap(const std::shared_ptr<const CSOLScenarioEntityAccess>& access);
void CSOL_SetBreakableGrenadeBlock(CBaseEntity& entity, bool blocked);
bool CSOL_BreakableBlocksGrenade(const CBaseEntity& entity);
