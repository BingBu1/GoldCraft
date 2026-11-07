// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace csol::client
{
void InitializeScenarioEffects();
void ScenarioEffectsFrame();
void ResetScenarioEffects();
void DrawScenarioEffects(float time, int intermission);
void ShutdownScenarioEffects();
}
