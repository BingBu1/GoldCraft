// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "config.hpp"

#include <array>
#include <span>

namespace csol::scenario
{
class WaveEventActions
{
public:
    virtual ~WaveEventActions() = default;
    // Query the scenario's registered monsters. Original gating tests type and
    // pev->solid != 0; it does not add health/deadflag/team/IsAlive checks.
    virtual bool HasSolidMonster(int type) = 0;
    // MPToCL, variable-length user message, MSG_ALL. Consume/copy these four
    // bytes synchronously; their storage belongs to the dispatch call.
    virtual void BroadcastEvent(std::span<const std::uint8_t, 4> payload) = 0;
};

enum class WaveEventResult { Invalid, Suppressed, Sent };

// Called by the wave-row handler after its clock has advanced. Suppression
// does not roll the clock back or retry an interval-zero event. Only types[0]
// controls gating; weights and additional types are not consulted.
WaveEventResult DispatchWaveEvent(const WaveRow& row, WaveEventActions& actions,
    ConfigError& error);
}
