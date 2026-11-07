// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace csol::scenario
{
struct RoundClockSnapshot
{
    std::uint8_t round{}, total{};
    float elapsed{};
};

// MP11382BC9/11385FAE: the game clock begins with the first BeginRound,
// including preparation, and survives ordinary stage changes. Subtype3's
// separate continuation restore is not an ordinary BeginRound operation.
// Keep this with the rules session; a new challenge starts a fresh clock.
class RoundClock
{
public:
    bool BeginRound(std::size_t zeroBasedRound, std::size_t total, float now,
        bool newChallenge = false)
    {
        if (!std::isfinite(now) || now < 0 || !total || total > 255 ||
            zeroBasedRound >= total) return false;
        // Preserve CSO's zero sentinel even if the first start is at time0.
        // Validate the entire candidate above before replacing old authority.
        if (newChallenge || !m_started || m_start == 0) m_start = now;
        m_round = static_cast<std::uint8_t>(zeroBasedRound + 1);
        m_total = static_cast<std::uint8_t>(total);
        m_started = true;
        return true;
    }

    std::optional<RoundClockSnapshot> Snapshot(float now) const
    {
        if (!m_started || !std::isfinite(now)) return std::nullopt;
        const float elapsed = std::max(0.0f, now - m_start);
        if (!std::isfinite(elapsed)) return std::nullopt;
        return RoundClockSnapshot{m_round, m_total, elapsed};
    }

    void Reset() { *this = {}; }

private:
    float m_start{};
    std::uint8_t m_round{}, m_total{};
    bool m_started{};
};
}
