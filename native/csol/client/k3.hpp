// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "weapons.hpp"
#include "../weapon_slots.hpp"

namespace csol::client {
void InitializeK3();
void ResetK3();
void ReceiveK3State(const WeaponCurrent& current);
bool PredictK3(local_state_t* from, local_state_t* to, usercmd_t* command,
    int runFunctions, double time, unsigned seed);
int K3PredictedAmmo();
int K3PredictedReserve();
bool K3CrosshairShot();
void K3Diagnostics(std::ostream& output);
}
