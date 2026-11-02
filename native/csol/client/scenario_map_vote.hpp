// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <iosfwd>
struct usercmd_s;
namespace csol::client {
void InitializeScenarioMapVote();
void ResetScenarioMapVote();
void ShutdownScenarioMapVote();
void ScenarioMapVoteFrame();
void ScenarioMapVoteCreateMove(usercmd_s* command,int active);
bool ScenarioMapVoteVisible();
void ScenarioMapVoteDiagnostics(std::ostream& output);
}
