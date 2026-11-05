// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
class CBasePlayer;
class CBasePlayerItem;
class CWeaponBox;
bool CSOL_PairedWeapon(const CBasePlayerItem* item);
bool CSOL_SameWeaponGroup(const CBasePlayerItem* first, const CBasePlayerItem* second);
class CSOLWeaponGroupDropScope
{
public:
    explicit CSOLWeaponGroupDropScope(const CBasePlayerItem* selected);
    ~CSOLWeaponGroupDropScope();
    CSOLWeaponGroupDropScope(const CSOLWeaponGroupDropScope&) = delete;
    CSOLWeaponGroupDropScope& operator=(const CSOLWeaponGroupDropScope&) = delete;
private:
    const CBasePlayerItem* previous;
};
bool CSOL_ExcludedFromGroupDrop(const CBasePlayerItem* candidate, const CBasePlayerItem* current);
// Called only for a recognized pair. Each original inventory entity is packed
// separately, with the remembered position last (PackWeapon prepends).
bool CSOL_PackWeaponGroup(CWeaponBox& box, CBasePlayer& owner, CBasePlayerItem& selected, bool packAmmo);
// Returns true only when this is a paired-primary box, including a rejected
// pickup. Ordinary weapon boxes retain their native Touch implementation.
bool CSOL_TouchWeaponGroup(CWeaponBox& box, CBasePlayer& player);
