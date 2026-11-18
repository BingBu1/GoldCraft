// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "magazine_actions.hpp"

namespace guest::cof
{
struct FamasFrameOwner
{
    float velocityX{}, velocityY{}, pendingGateTime{};
    std::uint32_t flags{};
    bool condition3AA{}, pendingGate{};
};
enum class FamasFrameEvent : unsigned char { ViewAnimation, ExitAim, Reload };
struct FamasFrameHost
{
    void* context{};
    bool (*current)(void*) noexcept {};
    float (*time)(void*) noexcept {};
    bool (*owner)(void*, FamasFrameOwner&) noexcept {};
    // False for a mechanical ExitAim/Reload means it was not executed. A
    // control's AppliedWithoutView result still counts as mechanical success.
    bool (*event)(void*, FamasFrameEvent, int sequence) noexcept {};
    bool (*duration)(void*, float&) noexcept {};
};
enum class FamasFrameResult : unsigned char
{
    Applied, AppliedWithoutPresentation, InputUnavailable, MissingHost,
    InvalidInput, Cancelled, UnsupportedState
};

// Complete ordinary MP1003B420; common1011D560 is the caller's next stage.
// Partial presentation does not revoke inventory or skip that common stage.
// Special1/2 and the separate hold-input producer are not inferred here.
FamasFrameResult AdvanceFamasDerivedFrame(MagazineActionState&, float& actionDeadline,
    const FamasFrameHost&) noexcept;
}
