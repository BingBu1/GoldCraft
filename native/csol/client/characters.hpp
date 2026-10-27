// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../character_catalog.hpp"
#include <iosfwd>
struct usercmd_s;
namespace csol::client {
void InitializeCharacters();
void ShutdownCharacters();
void ResetCharacters();
void CharacterFrame();
void CharacterCreateMove(float frame,usercmd_s* cmd,int active);
const Character* ViewCharacter();
void CharacterDiagnostics(std::ostream& output);
}
