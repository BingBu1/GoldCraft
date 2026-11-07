// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

class CBasePlayer;
class Vector;

// Dedicated 556AZ path: the original MP bullet19 has different penetration
// parameters and continuation from stock 556MM. Ammo ownership stays on the
// weapon; this routine performs only the authoritative trace/damage work.
Vector CSOL_Fire556AZ(CBasePlayer& player, const Vector& source, const Vector& forward,
    float spread, int damage, float rangeModifier, unsigned seed);
// Bullet type supplies material penetration power; each weapon separately
// supplies the maximum trace count and distance (SKULL-5: 8, SKULL-7: 7).
Vector CSOL_Fire556AZ(CBasePlayer& player, const Vector& source, const Vector& forward,
    float spread, float distance, int penetration, int damage, float rangeModifier, unsigned seed);
