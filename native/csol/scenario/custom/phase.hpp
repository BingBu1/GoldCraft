// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <optional>

namespace csol::scenario::custom
{
struct RoundKey
{
    std::uint64_t session{}, round{};
    bool Valid() const noexcept { return session && round; }
    bool operator==(const RoundKey&) const = default;
};
enum class Phase : std::uint8_t { Noon, Dusk, Night, Ended };
struct PhaseConfig { float noonSeconds{180.0f}, duskSeconds{120.0f}; };
struct PhaseView { Phase phase; float remaining; };

// User-defined round clock; it does not reproduce CSOL's original day length.
class PhaseClock
{
public:
    bool Begin(RoundKey key, float now, PhaseConfig config = {}) noexcept;
    std::optional<PhaseView> Sample(RoundKey key, float now) const noexcept;
    bool End(RoundKey key) noexcept;
    void Reset() noexcept;
private:
    RoundKey key_{};
    float start_{}, dusk_{}, night_{};
    bool ended_{};
};
}
