// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace csol::client
{
void InitializeCharacterSkillDisplay();
void ResetCharacterSkillDisplay();
void CharacterSkillDisplayFrame();
void CharacterSkillDisplayVideoInit();
void DrawCharacterSkillDisplay(float time, int intermission);
void ShutdownCharacterSkillDisplay();
}
