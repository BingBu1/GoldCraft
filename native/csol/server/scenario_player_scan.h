// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../scenario/trigger_program.hpp"

// One scanner per scenario session. The owning GameRules evaluates its actual
// scenario/round/termination gates before enabling a scan. This class supplies
// neither those gates nor the separately maintained alive-CT count.
class CCSOLTriggerPlayerScan
{
public:
    void BeginMap();
    csol::scenario::TriggerResult Update(bool enabled, csol::scenario::TriggerProgram& program,
        float elapsed, int aliveCtCount, csol::scenario::TriggerActions& actions);
    int NextPlayerSlot() const { return m_nextPlayer + 1; }
    bool IsScanning() const { return m_scanning; }

private:
    int m_nextPlayer{};
    bool m_scanning{};
};
