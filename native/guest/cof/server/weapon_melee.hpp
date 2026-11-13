// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "vector.h"
#include <array>

class CBasePlayer;
namespace guest::cof
{
enum class MeleeWorld : unsigned char { Multiplayer, UnsupportedSingleplayer };
enum class MeleeStatus : unsigned char
{
    Ready, Hit, Miss, NoActiveWeapon, InvalidInput, UnsupportedWorld, MissingEffects, Cancelled
};
struct PistolMeleeRequest
{
    int damage;
    // Explicit snapshot of original 10226489; no inferred drug/status semantics.
    bool quietAudio;
    MeleeWorld world;
};
using PistolMeleeSamples = std::array<const char*, 8>;
inline constexpr PistolMeleeSamples OriginalPistolMeleeSamples{
    "player/police/Swing/Swing1.wav", "player/police/Swing/Swing2.wav",
    "player/police/Swing/Swing3.wav", "player/police/Swing/Swing4.wav",
    "player/police/Swing/Swing5.wav", "player/police/Swing/Swing6.wav",
    "weapons/mobile/punch_swing.wav", "weapons/mobile/punch_smack.wav"
};
inline bool ValidPistolMeleeSamples(const PistolMeleeSamples& samples) noexcept
{
    for (const char* sample : samples)
        if (!sample || !*sample) return false;
    return true;
}
struct PistolMeleeEffects
{
    int shakeMessage;
    bool soundsReady;
    // Required original viewpunch.sc backend; never mapped to stock punchangle.
    void (*recoil)(void*, CBasePlayer&, const Vector&) noexcept;
    void* context;
    // Cold resource owner supplies all eight prepared paths, in original order.
    // Their NUL-terminated immutable storage must outlive this binding. A missing
    // table cannot silently fall back to unprepared original resource names.
    PistolMeleeSamples samples{};
};
struct MeleeCall
{
    // Parent verifies selected pistol, owner/life/map/weapon generation.
    bool (*current)(void*) noexcept;
    void* context;
};
// Cold resource preparation, before the weapon becomes usable.
bool PrecachePistolMeleeSounds(const PistolMeleeSamples&) noexcept;
MeleeStatus CheckPistolWeaponMelee(CBasePlayer&, const PistolMeleeRequest&,
    const PistolMeleeEffects&, const MeleeCall&) noexcept;
// Executes the already scheduled original multiplayer WeaponMelee callback.
// Parent owns the original .3-second scheduling and cancellation lifecycle.
MeleeStatus PistolWeaponMelee(CBasePlayer&, const PistolMeleeRequest&,
    const PistolMeleeEffects&, const MeleeCall&) noexcept;
}
