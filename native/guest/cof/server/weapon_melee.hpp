// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "vector.h"

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
struct PistolMeleeEffects
{
    int shakeMessage;
    bool soundsReady;
    // Required original viewpunch.sc backend; never mapped to stock punchangle.
    void (*recoil)(void*, CBasePlayer&, const Vector&) noexcept;
    void* context;
};
struct MeleeCall
{
    // Parent verifies selected pistol, owner/life/map/weapon generation.
    bool (*current)(void*) noexcept;
    void* context;
};
// Cold resource preparation, before the weapon becomes usable.
bool PrecachePistolMeleeSounds() noexcept;
MeleeStatus CheckPistolWeaponMelee(CBasePlayer&, const PistolMeleeRequest&,
    const PistolMeleeEffects&, const MeleeCall&) noexcept;
// Executes the already scheduled original multiplayer WeaponMelee callback.
// Parent owns the original .3-second scheduling and cancellation lifecycle.
MeleeStatus PistolWeaponMelee(CBasePlayer&, const PistolMeleeRequest&,
    const PistolMeleeEffects&, const MeleeCall&) noexcept;
}
