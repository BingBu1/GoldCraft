// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "controller.hpp"
#include "golden.hpp"
#include "trigger_program.hpp"

#include <algorithm>

namespace csol::scenario
{
// MP10978BE0 queries the trigger record with this wave's low 16-bit ID,
// independently of which rule activates the wave. The host pins its map and
// round before calling; this helper has no engine or observer callbacks.
inline bool ReadGoldenWaveStatus(std::uint16_t id, const TriggerProgram& triggers,
    const TriggerVolumes& volumes, WaveController& waves,
    GoldenWaveStatus& output, ConfigError& error)
{
    error = {};
    GoldenWaveStatus next{id, triggers.RuntimeCooldown(id), false};
    // Original geometry collection creates records whose cooldown is zero.
    if (!next.triggerCooldown && volumes.Find(id)) next.triggerCooldown = 0;
    if (next.triggerCooldown && !waves.EnsureWaveRuntime(id, error)) return false;
    if (const auto* runner = waves.FindWave(id))
        next.runtimeHasPendingRow = std::any_of(runner->Clocks().begin(), runner->Clocks().end(),
            [](const RowClock& clock) { return clock.next != -1; });
    output = next;
    return true;
}
}
