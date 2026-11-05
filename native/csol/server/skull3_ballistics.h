// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

class CBasePlayer;
class Vector;

// Original MP bullet28 (SKULL-3): distinct from both stock .45 and 556AZ.
// The weapon owns ammunition and supplies its own trace count/range/damage.
Vector CSOL_Fire45ACPAZ(CBasePlayer& player, const Vector& source, const Vector& forward,
    float spread, float distance, int penetration, int damage, float rangeModifier, unsigned seed);
