// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string_view>

namespace csol::scenario
{
// Concrete map lifecycles available for explicit server map selection. The
// automatic map-vote allowlist is separate until live acceptance is complete.
constexpr bool HasMapGameplay(std::string_view name, int subtype)
{
    return (name == "zs_deadend" && subtype == 1) ||
        (name == "zs_lostcity" && subtype == 0);
}
}
