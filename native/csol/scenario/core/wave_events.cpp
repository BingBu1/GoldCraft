// SPDX-License-Identifier: GPL-3.0-or-later
#include "wave_events.hpp"

#include <charconv>

namespace csol::scenario
{
namespace
{
bool EventNumber(std::string_view value, std::uint16_t& output)
{
    const auto first = value.find_first_not_of(" \t\r\n\v\f");
    if (first == value.npos)
    {
        output = 0;
        return true;
    }
    value.remove_prefix(first);
    if (value.front() == '+')
    {
        value.remove_prefix(1);
        if (value.empty() || value.front() < '0' || value.front() > '9')
        {
            output = 0;
            return true;
        }
    }
    // Original CSV short getter takes atoi's integer prefix, then keeps its
    // low 16 bits. No numeric prefix means zero. Reject int32 overflow rather
    // than relying on a CRT behavior not established by the original contract.
    std::int32_t number{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
    if (parsed.ec == std::errc::invalid_argument)
    {
        output = 0;
        return true;
    }
    if (parsed.ec != std::errc{}) return false;
    output = static_cast<std::uint16_t>(number);
    return true;
}
}

WaveEventResult DispatchWaveEvent(const WaveRow& row, WaveEventActions& actions,
    ConfigError& error)
{
    error = {};
    std::uint16_t message{};
    if (!row.IsEvent() || row.types.empty() || !EventNumber(row.eventMessage, message))
    {
        error.message = "invalid scenario event row or message number";
        return WaveEventResult::Invalid;
    }
    const auto type = row.types.front();
    if (type != 9999 && type != 30000 && !actions.HasSolidMonster(type))
        return WaveEventResult::Suppressed;
    const std::array<std::uint8_t, 4> payload{0x1b, 7,
        static_cast<std::uint8_t>(message), static_cast<std::uint8_t>(message >> 8)};
    actions.BroadcastEvent(payload);
    return WaveEventResult::Sent;
}
}
