// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

class CBasePlayerWeapon;
// All 29 stock CS weapons have the original empty monster-kill callbacks.
// Require the exact native type AND native identity; custom subclasses/IDs
// need their own adapter rather than inheriting this permission.
bool CSOL_NativeWeaponMonsterCallbacks(const CBasePlayerWeapon& weapon);
