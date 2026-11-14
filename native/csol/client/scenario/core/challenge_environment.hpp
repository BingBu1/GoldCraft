// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <iosfwd>

namespace csol::client
{
void InitializeChallengeEnvironment();
void ChallengeEnvironmentView(int waterlevel);
void DrawChallengeEnvironment();
void ResetChallengeEnvironment();
void ShutdownChallengeEnvironment();
void ChallengeEnvironmentDiagnostics(std::ostream& output);
}
