// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "scenario_entity_access.h"
#include <memory>
class CBasePlayer;
struct edict_s;
void CSOL_PrecacheCharacters();
bool CSOL_CharacterCommand(edict_s* entity,const char* command);
void CSOL_UpdateCharacters(CBasePlayer* player);
void CSOL_CharacterRoleChanged(CBasePlayer* player);
void CSOL_CharacterDisconnected(CBasePlayer* player);
const char* CSOL_CharacterModel(CBasePlayer* player);
// May be queried before the player becomes alive during scenario initialization.
// False means no selected/mapped character; it does not invent a class ID.
bool CSOL_CharacterScenarioClass(CBasePlayer* player, int& classId,
    std::shared_ptr<const CSOLScenarioEntityAccess> access = {});
// Local bot policy: choose the first compatible verified catalog entry when
// a bot has no character selection. Never substitutes an unknown class ID.
bool CSOL_EnsureScenarioBotCharacter(CBasePlayer* player,
    std::shared_ptr<const CSOLScenarioEntityAccess> access = {});
void CSOL_ApplyCharacterModel(CBasePlayer* player);
