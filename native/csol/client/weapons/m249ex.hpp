// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "weapons.hpp"
#include "../../weapons/weapon_slots.hpp"

namespace csol::client {
void InitializeM249EX();
void ResetM249EX();
void ReceiveM249EXState(const WeaponCurrent& current);
bool PredictM249EX(local_state_t* from, local_state_t* to, usercmd_t* command,
    int runFunctions, double time, unsigned seed);
int M249EXPredictedAmmo();
int M249EXPredictedReserve();
bool M249EXCrosshairShot();
void M249EXDiagnostics(std::ostream& output);
}
