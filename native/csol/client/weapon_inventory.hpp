// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <iosfwd>
#include "../weapon_slots.hpp"
#include <usercmd.h>
namespace csol::client
{
bool WeaponHasAmmo(int id);
void WeaponSelectionDiagnostics(std::ostream& out);
void ClearNativeWeaponSelection();
void WeaponSelectionVisibility(int hidden,int intermission);
void ObserveSelectionAmmo(int type,int count);
void ObserveSelectionClip(int id,int count);
void InitializeWeaponInventory();
void ResetWeaponInventory(bool forgetDefinitions);
void ObserveLegacyWeaponDefinition(int size,void* data);
void WeaponInventoryCreateMove(float frame,usercmd_t* cmd,int active);
void DrawWeaponSelection(int hidden,int intermission);
void DrawWeaponGuard(int hidden,int intermission);
void WeaponInventoryVideoInit();
const WeaponDefinition* FindWeaponDefinition(int id);
const WeaponCurrent& CurrentExtendedWeapon();
bool ExtendedWeaponActive();
void ReceiveExtendedWeaponState(const WeaponCurrent& current);
void ReceiveExtendedWeaponDefinition(const WeaponDefinition& definition);
}
