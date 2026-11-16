// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "magazine_actions.hpp"

namespace guest::cof
{
enum class G43ControlEventKind : unsigned char
{
    SchedulePushback, ViewAnimation, PlayerWeaponMelee,
    PlayerReloadLoaded, PlayerReloadEmpty
};
struct G43ControlEvent
{
    G43ControlEventKind kind;
    int argument{}; // Original animation sequence or weapon ID26.
    float delay{};  // Player-owned pushback scheduling only.
};
struct G43ControlHost
{
    void* context{};
    bool (*current)(void*) noexcept {};
    float (*time)(void*) noexcept {};
    bool (*event)(void*, const G43ControlEvent&) noexcept {};
};
enum class G43ControlResult : unsigned char
{
    Applied, Deferred, Ineligible, MissingHost, MissingSequence, InvalidInput,
    ConsumerRejected, Cancelled
};

// Matched MP10044E60: schedule the player's pushback, not a weapon Think or
// a monster task. The host supplies its existing shared CSOL stamina balance.
G43ControlResult StartG43Pushback(MagazineActionState&, float stamina,
    const G43ControlHost&) noexcept;
// Matched MP10044D60 and actual1011C560/10120210. Keep host skill-expanded
// magazines until spent; an ordinary physical reload fills ten rounds.
G43ControlResult StartG43MagazineReload(MagazineActionState&,
    const MagazineSequenceTimes&, const G43ControlHost&) noexcept;
}
