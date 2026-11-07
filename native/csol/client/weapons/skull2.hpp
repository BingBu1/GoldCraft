// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "weapons.hpp"
#include "../../weapons/weapon_slots.hpp"

namespace csol::client {
void InitializeSkull2();
void ResetSkull2();
void ReceiveSkull2State(const WeaponCurrent& current);
bool PredictSkull2(local_state_t* from,local_state_t* to,usercmd_t* command,
    int runFunctions,double time,unsigned seed);
int Skull2PredictedAmmo();
int Skull2PredictedReserve();
bool Skull2CrosshairShot();
void Skull2Diagnostics(std::ostream& output);
}
