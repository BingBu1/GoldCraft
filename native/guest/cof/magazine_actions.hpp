// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_damage.hpp"
#include <array>
#include <cstdint>

namespace guest::cof
{
namespace server { enum class LongGun : unsigned char; }
// Explicit original state, not offsets into a host player or weapon.
// All times are absolute authoritative seconds. Do not pass CS prediction
// countdowns here or reset this state when merely switching weapons.
struct MagazineActionState
{
    int clip{}, reserve{}, action{}, special{}, shotPhase{}, burstShots{};
    bool aiming{}, pendingReload{}, loading{};
    float primary{}, secondary{}, extra{}, idle{}, ownerAttack{};
};

enum class MagazineEventKind : std::uint8_t
{
    ViewAnimation, PlayerReloadLoaded, PlayerReloadEmpty,
    SchedulePlayerMelee, PlayerMelee
};
struct MagazineEvent
{
    MagazineEventKind kind;
    int argument{}; // View sequence or original weapon ID.
    float delay{};  // SchedulePlayerMelee only.
};

struct MagazineActionHost
{
    void* context{};
    // Owner/life/map/weapon identity, on the authoritative thread. Required
    // around every external callback, which may synchronously remove items.
    bool (*current)(void*) noexcept {};
    bool (*event)(void*, const MagazineEvent&) noexcept {};
};

// Prepared from the actual view model, using original1000F390's float
// (numframes-1)/fps, not the host frames/fps duration. A zero duration is valid
// for a one-frame sequence; presence is represented separately.
struct MagazineSequenceTimes
{
    // Glock's original jump exit uses sequence42. This is host storage, not
    // a request to play every sequence on every weapon (P345 ends at33).
    std::array<float, 43> duration{};
    std::uint64_t present{};
};

enum class MagazineActionResult : std::uint8_t
{
    Applied, Deferred, Ineligible, InvalidInput, UnsupportedWeapon,
    MissingSequence, MissingHost, Cancelled
};

// Original ordinary magazine reload START. It discards remaining cartridges,
// selects the original loaded/empty sequence and establishes loading. The
// separate CompleteMagazineReload helper owns the eventual ammo transfer.
// Revolver is deliberately unsupported here: it has cylinder/insert logic.
MagazineActionResult StartMagazineReload(Pistol pistol, MagazineActionState& state,
    float now, const MagazineSequenceTimes& times, const MagazineActionHost& host) noexcept;

// Original automatic TMP/MP5 reload start, using their own sequence/action
// order. This does not implement automatic fire, sound latches or pump/bolt
// insertion. Long-gun identities use the existing typed host catalog.
MagazineActionResult StartAutomaticMagazineReload(server::LongGun weapon, MagazineActionState& state,
    float now, const MagazineSequenceTimes& times, const MagazineActionHost& host) noexcept;

// Original pistol-whip scheduling; not CoF's separate long-gun pushback path.
MagazineActionResult StartPistolMelee(Pistol pistol, MagazineActionState& state,
    float now, const MagazineActionHost& host) noexcept;

// Original Glock/P345 idle; VP70's original idle function is empty.
MagazineActionResult MagazineIdle(Pistol pistol, MagazineActionState& state,
    float now, const MagazineSequenceTimes& times, const MagazineActionHost& host) noexcept;
}
