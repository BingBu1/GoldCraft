// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

class CBaseEntity;
class CBasePlayer;
class Vector;
struct TraceResult;

// CSO BodyTarget accepts the source and attack range. The adapter supplies
// the matched target implementation instead of silently replacing it with
// Center(), which changes head hits and large-monster reach.
using CSOLMeleeBodyTarget = Vector (*)(CBaseEntity&, const Vector&, float);

bool CSOL_TraceMeleeTarget(CBaseEntity& target, CBasePlayer& attacker,
    const Vector& origin, const Vector& forward, const Vector& horizontalForward,
    float distance, float halfAngleCosine, CSOLMeleeBodyTarget bodyTarget,
    TraceResult& result);

bool CSOL_MeleeBackstabFacing(const Vector& targetOrigin, const Vector& attackerOrigin,
    const Vector& targetForward);
