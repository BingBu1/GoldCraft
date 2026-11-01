// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace csol::client
{
void InitializeCharacterSkillEffects();
void ResetCharacterSkillEffects();
void ShutdownCharacterSkillEffects();
bool CharacterSkillInfiniteAmmo(int playerIndex);
bool CharacterSkillInfiniteAmmoFor(int weaponId);
bool DrawCharacterSkillInfiniteAmmo(int weaponId, int right, int y, int red, int green, int blue);
void CharacterSkillEffectsFrame();
void CharacterSkillEffectsVideoInit();
}
