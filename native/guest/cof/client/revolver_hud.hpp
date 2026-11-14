// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace guest::cof::client
{
// Original Draw10018CC0 consumes clip0..5 directly. Native scenario grants can
// exceed the five physical chambers; retain full numeric ammo and show all five.
constexpr int RevolverAmmoFrame(int clip) noexcept
{ return clip < 0 ? 0 : clip > 5 ? 5 : clip; }
}
