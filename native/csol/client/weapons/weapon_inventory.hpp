// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <iosfwd>
#include <cstdint>
#include "../../weapons/weapon_slots.hpp"
#include <usercmd.h>
namespace csol::client
{
bool WeaponHasAmmo(int id);
bool WeaponTextMenuVisible();
void WeaponSelectionDiagnostics(std::ostream& out);
void ClearNativeWeaponSelection();
void WeaponSelectionVisibility(int hidden,int intermission);
void ObserveSelectionAmmo(int type,int count);
void ObserveSelectionClip(int id,int count);
int WeaponReserveAmmo(int type);
void InitializeWeaponInventory();
void ResetWeaponInventory(bool forgetDefinitions);
void ObserveLegacyWeaponDefinition(int size,void* data);
void WeaponInventoryCreateMove(float frame,usercmd_t* cmd,int active);
void DrawWeaponSelection(int hidden,int intermission);
void DrawWeaponGuard(int hidden,int intermission);
void WeaponInventoryVideoInit();
const WeaponDefinition* FindWeaponDefinition(int id);
// Original default title follows a paired item only when that item is the
// first owned weapon in the same HUD slot. Presentation textures stay on id.
const WeaponDefinition* FindWeaponTitleDefinition(int id);
const WeaponCurrent& CurrentExtendedWeapon();
// Prediction may still replay the previous weapon after a newer current-
// weapon message. Keep its state simulation, but do not present it on the
// newly equipped model. Servers without this protocol retain native behavior.
bool WeaponPresentationAllowed(int weaponId);
// Stock CS caches its last animation separately from the engine. A custom
// weapon can change the engine animation without updating that stock cache.
std::uint64_t PendingWeaponAnimationSync(int weaponId);
bool ConsumeWeaponAnimationSync(std::uint64_t token,int weaponId);
bool ExtendedWeaponActive();
void ReceiveExtendedWeaponState(const WeaponCurrent& current);
void ReceiveExtendedWeaponDefinition(const WeaponDefinition& definition);
}
