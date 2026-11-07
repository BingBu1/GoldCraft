// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "game_rules_gate.h"
#include <cstdint>

namespace csol::scenario
{
// Local scenario rules requested by the host. Ordinary CS/ZP still use
// their configured mp_startmoney and mp_maxmoney values.
inline constexpr int InitialMoney = 7500;
inline constexpr int MaximumMoney = 32000;
}

inline int CSOL_AccountStartMoney(int nativeMoney)
{
    return CSOL_UsesScenarioRules() ? csol::scenario::InitialMoney : nativeMoney;
}

inline int CSOL_AccountMaxMoney(int nativeMoney)
{
    return CSOL_UsesScenarioRules() ? csol::scenario::MaximumMoney : nativeMoney;
}
