// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

// Register from GameDLLInit before game_init.cfg and the first map load.
void CSOL_InitGameModes();
// -1 means no valid prepared mode; callers must not silently choose a mode.
int CSOL_LockedGameMode();
bool CSOL_GameModeIsolationReady();
// Captured by a new scenario session; changing the CVAR does not retune an
// already loaded map, difficulty table or drop table partway through play.
bool CSOL_RequestedScenarioDifficulty(std::uint16_t& output);
