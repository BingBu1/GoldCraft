// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

class CBasePlayer;
class CCSOLScenarioRuntime;

// Chapter carry-over across zs_nextmap (MP 39f962f3...). Evidence:
// docs/2026-11-16_取证-灾难先兆三图接入-report.md section 4.7.
// The table is a process-lifetime static, as the original globals
// 11EE7640..11EE7668 survive the map change inside the loaded game DLL.

// Rules vt44C = 10824EE0, called by the zs_nextmap handler 108285E0 before
// "map <name> 1": clear, then snapshot every player with a character.
void CSOL_ChapterCarryCapture(CCSOLScenarioRuntime& runtime);
// 11433D70 (rules creation): a table that was already applied is cleared.
void CSOL_ChapterCarryRulesCreated();
// Rules vt450 = 108253B0 from the round start 10826860: marks applied once.
void CSOL_ChapterCarryRoundStarted();
// 108246C0 (+33B1): this player has a pending snapshot.
bool CSOL_ChapterCarryPending(CBasePlayer& player);
// Rules vt2A8 = 10826140 at the end of Spawn. gameStarted is rules +27C.
void CSOL_ChapterCarryRestore(CCSOLScenarioRuntime& runtime, CBasePlayer& player, bool gameStarted);
