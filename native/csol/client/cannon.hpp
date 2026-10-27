// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "weapons.hpp"
#include "../weapon_slots.hpp"

namespace csol::client {
void InitializeCannon();
void ResetCannon();
void ResetCannonEffects();
void DrawCannonSmoke();
bool CannonStudioEvent(const mstudioevent_t* event, const cl_entity_t* entity);
void ReceiveCannonState(const WeaponCurrent& current);
bool PredictCannon(local_state_t* from, local_state_t* to, usercmd_t* command,
    int runFunctions, double time, unsigned seed);
int CannonPredictedAmmo();
bool CannonCrosshairShot();
void CannonDiagnostics(std::ostream& output);
}
