// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "weapon_damage.h"

void CSOL_InitBeamDamage();
bool CSOL_ReloadBeamDamage();
bool CSOL_BeamDamageReady();
float CSOL_BeamDamage(CSOLBeamVariant variant, CSOLDamageMode mode, CSOLBeamDamage attack);
