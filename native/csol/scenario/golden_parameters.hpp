// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "config.hpp"

#include <bit>
#include <cmath>
#include <map>
#include <span>

namespace csol::scenario
{
// SharedDataMgr v698 keys 1/2/4 come from Packet_ZBSGoldenZB, independently
// of both the Golden monster JSON and the main difficulty table.
struct GoldenExtraParameters
{
    float probability{}, persons{}, secondPlanProbability{};
};

class GoldenParameters
{
public:
    // HW37A24521 constructs an empty map; HW37A32080 returns zero for missing
    // keys. The host owns this state. Player commands must not supply packets.
    float Parameter(std::uint8_t key) const
    {
        const auto found = values_.find(key);
        return found == values_.end() ? 0.0f : found->second;
    }
    GoldenExtraParameters Snapshot() const
    { return {Parameter(1), Parameter(2), Parameter(4)}; }

    bool ApplyPacket(std::span<const std::byte> bytes, ConfigError& error)
    {
        error = {};
        if (bytes.empty())
        {
            error.message = "Golden parameter packet is missing its subtype";
            return false;
        }
        // HW37539657 ignores every other subtype without clearing old values.
        if (std::to_integer<unsigned>(bytes[0]) != 0) return true;
        if (bytes.size() < 2 || bytes.size() != 2 + 5 * std::to_integer<unsigned>(bytes[1]))
        {
            error.message = "Golden parameter packet has an invalid entry count";
            return false;
        }
        std::map<std::uint8_t, float> next;
        for (std::size_t at = 2; at < bytes.size(); at += 5)
        {
            std::uint32_t bits{};
            for (unsigned byte = 0; byte != 4; ++byte)
                bits |= std::to_integer<std::uint32_t>(bytes[at + 1 + byte]) << (byte * 8);
            const float value = std::bit_cast<float>(bits);
            if (!std::isfinite(value))
            {
                error.byteOffset = at + 1;
                error.message = "Golden parameter must be finite";
                return false;
            }
            // Original insertion keeps the first repeated key. Validation is
            // transactional here so malformed input cannot leave a partial map.
            next.emplace(std::to_integer<std::uint8_t>(bytes[at]), value);
        }
        values_.swap(next);
        return true;
    }

private:
    std::map<std::uint8_t, float> values_;
};
}
