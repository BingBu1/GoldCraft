// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace guest::cof
{
struct OwnerButtonEdges
{
    std::uint32_t pressed;
    std::uint32_t released;
};

// All inputs and state are supplied by the authoritative owner adapter. No
// construction, spawn, restore, death or session-reset policy is implied here.
struct OwnerJumpInputs
{
    std::uint8_t phone;
    std::uint32_t flags;
    std::int32_t waterLevel;
    std::int32_t waterType;
    std::uint32_t pressedButtons;
    bool hasGroundEntity;
};

struct OwnerJumpState
{
    std::uint8_t justJumped;
    float deadline;
};

// Original hl.dll0036b91c /100EFCBA..100EFCE2. Store current buttons as
// previous only at the original PostThink tail, after weapon processing.
OwnerButtonEdges ComputeOwnerButtonEdges(std::uint32_t previous,
    std::uint32_t current) noexcept;

// Original100E5030..100E509B: gates only, not Jump animation or movement.
bool CanBeginOwnerJump(const OwnerJumpInputs& inputs) noexcept;

// Original100E520F..100E522F. Call only after the actual Jump accepts;
// calling on an input edge alone does not establish an accepted jump.
void RecordAcceptedOwnerJump(OwnerJumpState& state, float now) noexcept;

// Original100EB22D..100EB25E. Runs after movement, before ItemPostFrame.
// Early-PreThink spread consumes the state that existed before this stage.
void AdvanceOwnerJumpAfterMovement(OwnerJumpState& state,
    std::uint32_t flags, float now) noexcept;
}
