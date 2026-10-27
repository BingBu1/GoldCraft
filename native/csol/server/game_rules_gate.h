// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

class CGameRules;
class CHalfLifeMultiplay;
class CBasePlayer;
class CCSOLScenarioMap;
struct edict_s;

// InstallGameRules starts one map's decision after game.cfg. This consumes the
// AMXX precache gate's locked mode; it never confirms plugin isolation itself.
CGameRules* CSOL_CreateGameRules();
void CSOL_PrecacheGameRules();
bool CSOL_ActivateGameRules();
bool CSOL_UsesScenarioRules();
bool CSOL_ScenarioSelecting();
void CSOL_ApplyScenarioMovement(CBasePlayer& player, float weaponSpeed);
std::uint32_t CSOL_ScenarioPassMask(int playerIndex);

// Called inside ReGameDLL's existing ReAPI original-function wrappers. A
// true result consumes only the scenario branch; other modes remain native.
bool CSOL_ScenarioThink(CHalfLifeMultiplay& rules);
// Read the current map's stored rules multiplier, never recalculate on hit.
bool CSOL_ReadScenarioDamageScale(const CCSOLScenarioMap& map, float& value);
bool CSOL_ScenarioRestartRound(CHalfLifeMultiplay& rules);
bool CSOL_ScenarioCheckWinConditions(CHalfLifeMultiplay& rules);
bool CSOL_ScenarioPrepareSpawn(CHalfLifeMultiplay& rules, CBasePlayer& player);
bool CSOL_ScenarioCanRespawn(CHalfLifeMultiplay& rules, CBasePlayer& player, bool& allowed);
bool CSOL_ScenarioTeamFull(CHalfLifeMultiplay& rules, int team, bool& full);
bool CSOL_ScenarioHoldSpawn(CBasePlayer& player);
void CSOL_ScenarioCharacterSelected(CBasePlayer& player);
bool CSOL_ScenarioSelectSpawn(CBasePlayer& player, edict_s*& spot);
void CSOL_ScenarioPlayerDisconnected(CBasePlayer& player);
bool CSOL_ShowScenarioTeamMenu(CBasePlayer& player);
bool CSOL_ScenarioAllowAppearance(CBasePlayer& player);

// A rejection survives service EndMap and client disconnect callbacks. Only
// creating rules for a new map clears it; changing a CVAR cannot unlock a
// partially loaded map. This is not a global engine pause facility.
bool CSOL_RulesRejected();
const char* CSOL_RulesRejectionReason();
void CSOL_RejectGameRules(const char* reason);
void CSOL_FreezeRejectedEntity(edict_s* entity);
void CSOL_HoldRejectedPlayer(CBasePlayer& player);
bool CSOL_PrepareRejectedPlayer(edict_s* entity);
