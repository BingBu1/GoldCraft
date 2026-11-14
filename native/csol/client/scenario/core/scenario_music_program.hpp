// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../../../scenario/core/scenario_music.hpp"
namespace csol::client
{
bool InitializeScenarioMusicProgram();
bool StartScenarioMusicProgram(const music::Packet& packet);
void CancelScenarioMusicProgram();
void ScenarioMusicProgramFrame();
void ShutdownScenarioMusicProgram();
}
