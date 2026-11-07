// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "weapons.hpp"
#include "../../weapons/weapon_slots.hpp"

namespace csol::client {
void InitializeSkull5();
void ResetSkull5();
void ReceiveSkull5State(const WeaponCurrent& current);
bool PredictSkull5(local_state_t* from,local_state_t* to,usercmd_t* command,
    int runFunctions,double time,unsigned seed);
int Skull5PredictedAmmo();
int Skull5PredictedReserve();
bool Skull5CrosshairShot();
void Skull5Diagnostics(std::ostream& output);
}
