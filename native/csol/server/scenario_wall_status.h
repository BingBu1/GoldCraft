// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
class CBasePlayer;
class CBaseEntity;
// Use UpdateStatusBar's existing authoritative eye trace. True consumes the
// wall target; false leaves ordinary player/hostage status with native CS.
bool CSOL_UpdateWallStatus(CBasePlayer& player, CBaseEntity* target);
// Call at the end of CBasePlayer::InitStatusBar (respawn/HUD initialization).
void CSOL_ResetWallStatus(CBasePlayer& player);
