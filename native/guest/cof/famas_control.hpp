// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "magazine_actions.hpp"

namespace guest::cof
{
enum class FamasControlEventKind : unsigned char { SchedulePushback, ViewAnimation };
struct FamasControlEvent
{
    FamasControlEventKind kind;
    int argument{}; // Original weapon ID23 or view sequence.
    float delay{};
};
struct FamasControlHost
{
    void* context{};
    bool (*current)(void*) noexcept {};
    float (*time)(void*) noexcept {};
    bool (*event)(void*, const FamasControlEvent&) noexcept {};
};
enum class FamasControlResult : unsigned char
{
    Applied, AppliedWithoutView, Deferred, Ineligible, MissingHost,
    MissingSequence, InvalidInput, ConsumerRejected, Cancelled
};

// MP1003C370: the existing player-owned pushback and shared CSOL stamina.
FamasControlResult StartFamasPushback(MagazineActionState&, float stamina,
    const FamasControlHost&) noexcept;
// MP1003C2C0/1011C560: ID23 deliberately has no reserve-ammo prerequisite.
// Host adaptation preserves skill-expanded magazines and reports optional
// view-delivery failure without leaving the mechanical reload incomplete.
FamasControlResult StartFamasMagazineReload(MagazineActionState&,
    const MagazineSequenceTimes&, const FamasControlHost&) noexcept;
}
