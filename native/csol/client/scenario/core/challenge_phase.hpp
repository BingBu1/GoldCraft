// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <iosfwd>
namespace csol::client
{
void InitializeChallengePhase();
void ChallengePhaseFrame();
void ChallengePhaseVideoInit();
void DrawChallengePhase(int intermission);
void ResetChallengePhase();
void InvalidateChallengePhaseResources();
void ShutdownChallengePhase();
void ChallengePhaseDiagnostics(std::ostream&);
}
