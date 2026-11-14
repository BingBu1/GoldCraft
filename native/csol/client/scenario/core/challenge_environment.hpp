// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <iosfwd>
struct movevars_s;
struct engine_studio_api_s;

namespace csol::client
{
void InitializeChallengeEnvironment();
void ChallengeEnvironmentView(int waterlevel, movevars_s* movevars = nullptr);
void InstallChallengeEnvironmentStudio(engine_studio_api_s* functions);
void DrawChallengeEnvironment();
void ResetChallengeEnvironment();
void ShutdownChallengeEnvironment();
void ChallengeEnvironmentDiagnostics(std::ostream& output);
}
