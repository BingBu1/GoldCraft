// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "weapons.hpp"
#include "../weapon_slots.hpp"

namespace csol::client {
void InitializeMeleeWeapons();
void ResetMeleeWeapons();
void ReceiveMeleeState(const WeaponCurrent& current);
bool PredictMeleeWeapon(local_state_t* from, local_state_t* to, usercmd_t* command,
    int runFunctions, double time, unsigned seed);
void MeleeDiagnostics(std::ostream& out);

// CSO knife variants invert the handedness used by studio bone setup. This
// temporary value belongs only to the custom viewmodel draw, never prediction.
class MeleeHandScope
{
public:
    MeleeHandScope(cl_entity_t* entity, int flags);
    ~MeleeHandScope();
    MeleeHandScope(const MeleeHandScope&) = delete;
    MeleeHandScope& operator=(const MeleeHandScope&) = delete;
private:
    cvar_t* handedness{};
    float savedHandedness{};
};
}
