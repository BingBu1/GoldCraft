// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
class CBasePlayer;
class CBasePlayerItem;
class CBasePlayerWeapon;
// Uses normal Spawn/Touch and inventory hooks. Replaces the knife slot only
// after the new entity has been acquired; the caller owns purchase charging.
// A rejected insertion is compensated only while entity/player identities are
// still current. Third-party transfers and retired maps are never reclaimed.
bool CSOL_DeliverMeleeWeapon(CBasePlayer& player, int weaponId);
// Only the exact replacement Touch for an existing owned knife suppresses
// native pickup/autoswitch. Ordinary pickups and first grants are unchanged.
bool CSOL_IsMeleeReplacement(const CBasePlayer& player, const CBasePlayerItem& item);
// Original selection common tail; preserves attack/prediction state.
bool CSOL_ScenarioRefreshMeleeSelection(CBasePlayer& player, CBasePlayerWeapon& weapon);
// Independent original rebuy paint-only tail; no grant or selection update.
void CSOL_ScenarioRefreshMeleePaint(CBasePlayer& player,int selectedWeapon);
