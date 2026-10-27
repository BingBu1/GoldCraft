// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "weapons.hpp"
#include "../weapon_slots.hpp"

namespace csol::client
{
void InitializeBeamgun();
void ResetBeamgun();
void BeamgunVideoInit();
void ReceiveBeamgunState(const WeaponCurrent& current);
// -1 leaves unrelated models alone. Charge textures use skin, never hands/body.
int BeamgunViewSkin(const cl_entity_t* entity);
void DrawBeamgunTracers();
unsigned BeamEffectIdentity(const cl_entity_t* entity);
void PruneBeamEffects();
bool PredictBeamgun(local_state_t* from, local_state_t* to, usercmd_t* command,
    int runFunctions, double time, unsigned seed);
void DrawBeamgunHUD(int hidden, int intermission);
bool BeamgunSecondaryAmmoVisible(int hidden, int intermission);
void BeamgunDiagnostics(std::ostream& output);
void DrawBeamScope(int weapon, int width, int height, float ratio);
void ResetBeamScope();
}
