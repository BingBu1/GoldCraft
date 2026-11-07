// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace csol
{
// Explicit wire format: version byte, entity short, precached sprite short.
constexpr char KnifeTrailMessage[] = "CsolTrail";
constexpr int KnifeEffectVersion = 1;
constexpr int KnifeTrailMessageBytes = 5;
constexpr float KnifeTrailLife = 0.1f;
constexpr float KnifeTrailWidth = 1.0f;
constexpr float KnifeTrailBrightness = 200.0f / 255.0f;
}
