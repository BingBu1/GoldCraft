// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "shotgun_control.hpp"

namespace guest::cof
{
struct ShotgunShotState;
struct ShotgunFrameOwner
{
    float velocityX{}, velocityY{}, pendingGateTime{};
    std::uint32_t flags{}, pressed{};
    bool condition3AA{}, pendingGate{};
};
enum class ShotgunFrameEvent : unsigned char { ViewAnimation, ExitAim, Reload, CommonFrame };
struct ShotgunFrameHost
{
    void* context{};
    bool (*current)(void*) noexcept {};
    float (*time)(void*) noexcept {};
    bool (*owner)(void*, ShotgunFrameOwner&) noexcept {};
    bool (*event)(void*, ShotgunFrameEvent, int sequence) noexcept {};
    bool (*duration)(void*, float&) noexcept {};
    bool (*control)(void*, const ShotgunControlEvent&) noexcept {};
};

// Entire ordinary1006D3E0 body through its required common1011D560 consumer.
// Special1/2 lifecycle is unsupported; the host must not acknowledge a missing
// common frame, aim or virtual reload callback as success.
ShotgunControlResult AdvanceShotgunDerivedFrame(ShotgunShotState&, float& actionDeadline,
    const ShotgunFrameHost&) noexcept;
}
