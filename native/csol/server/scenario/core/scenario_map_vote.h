// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../../../scenario/core/map_vote.hpp"
struct edict_s;
bool CSOL_PrecacheScenarioMapVote();
// Call after message registration and before a completed challenge's finish
// policy is applied. An empty playable slate returns false; retain FreePlay.
bool CSOL_StartScenarioMapVote(unsigned seconds = csol::scenario::map_vote::DefaultSeconds);
// Called by the owning rules' Think, including its completion phase. A result
// is returned once. Apply the normal validated ChangeMap policy to that result.
std::optional<csol::scenario::map_vote::Choice> CSOL_UpdateScenarioMapVote();
bool CSOL_ScenarioMapVoteCommand(edict_s* player, const char* command);
bool CSOL_ScenarioMapVoteActive();
// Admin finish-policy changes/replay cancel a pending vote. Frame sends the
// cancelled snapshot. Map activation/deactivation resets the module instead.
void CSOL_CancelScenarioMapVote();
void CSOL_ResetScenarioMapVote();
