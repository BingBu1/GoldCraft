// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "weapons.hpp"
#include "../../weapons/weapon_slots.hpp"

namespace csol::client
{
void InitializeBalrog11();
void ResetBalrog11();
void ReceiveBalrog11State(const WeaponCurrent& current);
bool PredictBalrog11(local_state_t* from, local_state_t* to, usercmd_t* command,
    int runFunctions, double time, unsigned seed);
int Balrog11PredictedAmmo();
int Balrog11PredictedReserve();
int Balrog11PredictedCharges();
bool Balrog11CrosshairShot();
void Balrog11Diagnostics(std::ostream& output);
}
