// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "vector.h"
#include <cstdint>

class CBaseEntity;

namespace guest::cof
{
enum class PistolPlayerHits : unsigned char { OriginalCooperativeSkipPlayers };
enum class PistolSource : unsigned char { NormalMuzzle, UnsupportedOriginalOverride };
enum class BallisticsStatus : unsigned char
{
    Ready, Fired, InvalidInput, UnsupportedSource, MissingEffects, Cancelled
};

struct PistolBallistics
{
    Vector source, direction, spread, right, up;
    std::uint32_t seed;
    int damage;
    bool aiming;
    PistolPlayerHits playerHits;
    PistolSource sourceMode;
};

struct BallisticsEffects
{
    // Cold preparation registers the matched customdecal consumer and precaches
    // the bubble sprite. IDs are runtime values, never original process offsets.
    int customDecalMessage;
    int bubbleModel;
    // Set by the scenario 870 owner only. Pistol rays never consume this predicate.
    bool (*pierceZombie)(CBaseEntity&) noexcept {};
};

struct BallisticsCall
{
    // Required owner/life/map/weapon generation guard, owned by the caller.
    // Called on the authoritative thread around SDK/engine callbacks.
    bool (*current)(void*) noexcept;
    void* context;
};

struct BallisticsResult
{
    BallisticsStatus status;
    Vector spread;
};

// Check BEFORE deducting ammo, recoil, animation, or other shot side effects.
// Four pistols: one shot, original type3, distance8192, explicit integer damage.
// Zero damage preserves original no-TraceAttack behavior, including world FX.
BallisticsStatus CheckPistolBallistics(const PistolBallistics& shot,
    const BallisticsEffects& effects, const BallisticsCall& call) noexcept;

// Original hl.dll0036b91c /10088CA0, adapted to the actual ReGameDLL damage API.
// No CS FireBullets/FireBullets3 substitution; no host health/armor writes.
BallisticsResult FirePistolBallistics(CBaseEntity& shooter, const PistolBallistics& shot,
    const BallisticsEffects& effects, const BallisticsCall& call) noexcept;

// Original 870: eight seeded pellets over 2048 units, with one shared damage
// transaction. The input payload and preflight are shared with the single shot.
BallisticsResult FireShotgunBallistics(CBaseEntity& shooter, const PistolBallistics& shot,
    const BallisticsEffects& effects, const BallisticsCall& call) noexcept;

// Human-requested host deviation: keep CoF's seeded ray, then use the actual
// CS .357SIG material/shield/hookchain path with no additional spread or range
// falloff. This is opt-in for Revolver; the original adapter above is unchanged.
BallisticsResult FireCSPistolBallistics(CBaseEntity& shooter, const PistolBallistics& shot,
    const BallisticsEffects& effects, const BallisticsCall& call, int penetration) noexcept;

enum class CSRifleMaterial : unsigned char { Rifle556, Rifle762 };
// Native CS material shots do not consume the original custom-decal or bubble
// resource IDs. Keep source/ray/damage/ownership checks independent of those FX.
BallisticsStatus CheckCSRifleBallistics(const PistolBallistics& shot,
    const BallisticsCall& call, int penetration, CSRifleMaterial material) noexcept;
// Human-authored rifle rule, retaining the original CoF ray and the actual
// CS material/shield/damage consumers. No additional spread or range falloff.
BallisticsResult FireCSRifleBallistics(CBaseEntity& shooter, const PistolBallistics& shot,
    const BallisticsEffects& effects, const BallisticsCall& call, int penetration,
    CSRifleMaterial material) noexcept;
}
