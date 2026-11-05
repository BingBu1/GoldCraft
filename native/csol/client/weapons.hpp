// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "runtime.hpp"
#include <usercmd.h>
#include <iosfwd>

namespace csol::client
{
void InitializeWeapons();
void ShutdownWeapons();
void ResetWeapons(bool forgetRegistration);
void WeaponVideoInit();
void WeaponDiagnostics(std::ostream& out);
int WeaponRedraw(float time, int intermission);
int ActiveWeaponHudId();
int WeaponFieldOfView();
void WeaponPostRunCmd(local_state_t* from, local_state_t* to, usercmd_t* cmd,
    int runFunctions, double time, unsigned int seed);
}
