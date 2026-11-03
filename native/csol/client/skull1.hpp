// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "weapons.hpp"
#include "../weapon_slots.hpp"

namespace csol::client {
void InitializeSkull1();
void ResetSkull1();
void ReceiveSkull1State(const WeaponCurrent& current);
bool PredictSkull1(local_state_t* from,local_state_t* to,usercmd_t* command,
    int runFunctions,double time,unsigned seed);
int Skull1PredictedAmmo();
int Skull1PredictedReserve();
bool Skull1CrosshairShot();
void Skull1Diagnostics(std::ostream& output);
}
