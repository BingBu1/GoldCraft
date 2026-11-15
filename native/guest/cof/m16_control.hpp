// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "magazine_actions.hpp"

namespace guest::cof
{
struct M16ControlState
{
    MagazineActionState action;
    bool burst{true};
};
enum class M16ControlEventKind : unsigned char
{
    Shoot, EmptyClick, Reload, ViewAnimation, PlayerReloadLoaded, PlayerReloadEmpty
};
struct M16ControlEvent
{
    M16ControlEventKind kind;
    int sequence{};
};
struct M16ControlHost
{
    void* context{};
    bool (*current)(void*) noexcept {};
    // Pure authoritative clock read. Events may advance time or retire the
    // owner, so the original post-consumer reads must not reuse entry time.
    float (*time)(void*) noexcept {};
    bool (*event)(void*, const M16ControlEvent&) noexcept {};
};
enum class M16ControlResult : unsigned char
{
    Applied, Deferred, Ineligible, MissingHost, MissingSequence, InvalidInput,
    ConsumerRejected, Cancelled
};

// Matched hl.dll10051D50. Shoot is the complete per-shot consumer, including
// its own eligibility and scheduling; this wrapper only starts the group.
M16ControlResult M16PrimaryTrigger(M16ControlState&, bool attackPressed,
    bool fireOnEmpty, const M16ControlHost&) noexcept;
// Matched10052D20: choose animation from the old mode, then toggle it.
M16ControlResult M16ToggleFireMode(M16ControlState&, const M16ControlHost&) noexcept;
// Matched10051ED0 and1011C560/10120210. Skill-expanded host magazines are
// retained until spent; requesting reload must not discard their extra ammo.
M16ControlResult StartM16MagazineReload(M16ControlState&, const MagazineSequenceTimes&,
    const M16ControlHost&) noexcept;
}
