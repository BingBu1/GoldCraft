// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
class CBasePlayerWeapon;
// Exact supported knife identity for selection metadata. Unlike the custom
// melee variant helpers, this also accepts the native default CKnife.
int CSOL_MeleeSelectionItem(const CBasePlayerWeapon& weapon);
// Preserve the native Deploy return while a purchase observes its exact new
// CKnife. SelectItem/SwitchWeapon do not propagate that return to the caller.
int CSOL_NativeKnifeDeployment(CBasePlayerWeapon& weapon, int result);
