// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace csol::scenario::custom
{
inline constexpr char JumpDeniedKey[] = "cso_jdeny";
inline bool JumpDenied(const char* value) noexcept
{ return value && value[0] == '1' && value[1] == '\0'; }

// Host adaptation for native CS clientb434b1c0 Jump1005FB10. Its water and
// water-jump branches run before the oldbuttons latch; swimming stays native.
// The caller supplies this command's server-authored physics-info denial bit.
template<class Movement> inline bool ApplyJumpPredictionGate(Movement& movement, bool denied) noexcept
{
    constexpr int jump = 1 << 1;
    if (!denied || movement.dead || movement.spectator || movement.movetype != 3 ||
        !(movement.cmd.buttons & jump)) return false;
    movement.oldbuttons |= jump;
    return true;
}
}
