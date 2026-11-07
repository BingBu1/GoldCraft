// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "weapons.hpp"
#include "../../weapons/weapon_slots.hpp"

namespace csol::client {
void InitializeSkull3();
void ResetSkull3();
void ReceiveSkull3State(const WeaponCurrent& current);
bool PredictSkull3(local_state_t* from,local_state_t* to,usercmd_t* command,
    int runFunctions,double time,unsigned seed);
int Skull3PredictedAmmo();
int Skull3PredictedReserve();
bool Skull3CrosshairShot();
void Skull3Diagnostics(std::ostream& output);
}
