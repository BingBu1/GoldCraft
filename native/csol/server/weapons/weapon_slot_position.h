// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
class CBasePlayerItem;

// Original automatic SwitchWeapon writes before Deploy; explicit SelectItem
// does not. These positions are independent of the host HUD ordering.
void CSOL_RememberWeaponSlotPosition(CBasePlayerItem& item);
