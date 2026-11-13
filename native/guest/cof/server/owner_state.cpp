// SPDX-License-Identifier: GPL-3.0-or-later
#include "owner_state.hpp"

namespace guest::cof
{
OwnerButtonEdges ComputeOwnerButtonEdges(std::uint32_t previous,
    std::uint32_t current) noexcept
{
    const auto changed = previous ^ current;
    return {current & changed, ~current & changed};
}

bool CanBeginOwnerJump(const OwnerJumpInputs& inputs) noexcept
{
    return inputs.phone == 0 && (inputs.flags & 0x800u) == 0 &&
        (inputs.waterLevel < 2 || inputs.waterType == -19) &&
        (inputs.pressedButtons & 2u) != 0 && (inputs.flags & 0x200u) != 0 &&
        inputs.hasGroundEntity;
}

void RecordAcceptedOwnerJump(OwnerJumpState& state, float now) noexcept
{
    state.justJumped = 1;
    state.deadline = now + 3.0f;
}

void AdvanceOwnerJumpAfterMovement(OwnerJumpState& state,
    std::uint32_t flags, float now) noexcept
{
    if (state.justJumped != 0 && ((flags & 0x200u) != 0 || now > state.deadline))
        state.justJumped = 0;
}
}
