// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "effects_dispatch.hpp"

namespace guest::cof
{
struct ScreenShakeRequest
{
    Vector origin;
    float amplitude, frequency, duration, radius;
};
struct ScreenShakeResult
{
    WeaponEffectStatus status;
    unsigned packets{};
};

// Original hl.dll0036b91c/10119DE0. Uses the actual native six-byte
// ScreenShake consumer. Registration/client admission belongs to the map owner.
ScreenShakeResult ShakePlayers(const ScreenShakeRequest&, int preparedMessage,
    const PlaySfxCall&) noexcept;
}
