// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "beamgun_state.hpp"
#include "weapon_slots.hpp"

namespace csol
{
inline bool BeamWeaponId(int id) { return id == BeamgunId || id == BeamgunLEId; }

// These fields already exist in the CS weapon_data_t delta table. Only the
// active extended weapon uses slot zero; no engine layout or stock state changes.
template<class WeaponData> BeamWeaponState ReadBeamPrediction(const WeaponData& data)
{
    BeamWeaponState state;
    state.clip = data.m_iClip;
    state.energy = data.iuser1;
    state.reloading = data.m_fInReload != 0;
    state.charge = BeamCharge(data.m_iWeaponState & 3);
    state.dive = BeamDive((data.m_iWeaponState >> 2) & 3);
    state.pending = BeamPending((data.m_iWeaponState >> 4) & 3);
    state.energyFull = state.energy >= 100;
    state.nextPrimary = data.m_flNextPrimaryAttack;
    state.nextSecondary = data.m_flNextSecondaryAttack;
    state.idleTime = data.m_flTimeWeaponIdle;
    state.chargeTime = data.fuser1;
    state.comboTime = data.fuser2 / 8;
    state.delayedTime = data.fuser3 / 8;
    return state;
}

template<class WeaponData> void WriteBeamPrediction(const BeamWeaponState& state, WeaponData& data)
{
    data.m_iClip = state.clip;
    data.iuser1 = state.energy;
    data.m_fInReload = state.reloading;
    data.m_iWeaponState = int(state.charge) | (int(state.dive) << 2) | (int(state.pending) << 4);
    data.m_flNextPrimaryAttack = state.nextPrimary;
    data.m_flNextSecondaryAttack = state.nextSecondary;
    data.m_flTimeWeaponIdle = state.idleTime;
    data.fuser1 = state.chargeTime;
    // CS quantizes these fields at 128 steps/second. Scaling by eight retains
    // the -0.001 expired sentinel (otherwise it becomes zero on every update).
    // Only beam weapons interpret these fields this way; the delta ABI is intact.
    data.fuser2 = state.comboTime * 8;
    data.fuser3 = state.delayedTime * 8;
}
}
