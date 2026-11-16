// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "magazine_actions.hpp"

namespace guest::cof
{
struct G43FrameOwner
{
    float velocityX{}, velocityY{}, pendingGateTime{};
    std::uint32_t flags{}, pressed{}, released{}, held{};
    bool condition3AA{}, pendingGate{}, toggleAim{};
};
enum class G43FrameEvent : unsigned char { ViewAnimation, ExitAim, Reload };
struct G43FrameHost
{
    void* context{};
    bool (*current)(void*) noexcept {};
    float (*time)(void*) noexcept {};
    bool (*owner)(void*, G43FrameOwner&) noexcept {};
    bool (*event)(void*, G43FrameEvent, int sequence) noexcept {};
    bool (*duration)(void*, float&) noexcept {};
};
enum class G43FrameResult : unsigned char
{
    Applied, MissingHost, InvalidInput, ConsumerRejected, Cancelled, UnsupportedState
};

// Original10043DD0 ordinary derived frame. The caller executes common1011D560
// afterwards; special1/2 self-harm behavior is deliberately not fabricated.
G43FrameResult AdvanceG43DerivedFrame(MagazineActionState&, float& actionDeadline,
    const G43FrameHost&) noexcept;
}
