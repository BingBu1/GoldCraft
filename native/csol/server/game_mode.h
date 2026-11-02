// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <array>

// Register from GameDLLInit before game_init.cfg and the first map load.
void CSOL_InitGameModes();
// -1 means no valid prepared mode; callers must not silently choose a mode.
int CSOL_LockedGameMode();
bool CSOL_GameModeIsolationReady();
// Captured by a new map or explicit challenge replay; changing the CVAR alone
// never retunes difficulty/drop tables partway through combat.
bool CSOL_RequestedScenarioDifficulty(std::uint16_t& output);
// Internal validated caller only; may invoke engine cvar hooks. Verify the
// owning map after returning; replay applies the request at a safe round boundary.
bool CSOL_WriteScenarioDifficultyRequest(std::uint16_t difficulty);
// Local explicit mapping of the original final-result delay selector.
// Default 0 selects30s; this is not a claim about CSO's unknown room default.
bool CSOL_RequestedScenarioShortFinalDelay(bool& output);

// Local dedicated-server policy, read once when the final wait expires.
// This does not change CSO's intermediate-stage or failure-continuation rules.
enum class CSOLScenarioFinishAction { FreePlay, ChangeMap, Replay, Vote };
struct CSOLScenarioFinishPolicy
{
    CSOLScenarioFinishAction action{CSOLScenarioFinishAction::FreePlay};
    std::array<char, 64> nextMap{};
};
bool CSOL_RequestedScenarioFinishPolicy(CSOLScenarioFinishPolicy& output);
