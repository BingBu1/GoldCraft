// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace guest::cof
{
// Host balance profiles, deliberately separate from original CoF simulation.
// Factory identities are verified against hl.dll 0036b91c (see domain report).
enum class Pistol : unsigned char { Glock, P345, Revolver, VP70, Count };
inline constexpr std::array<std::string_view, static_cast<std::size_t>(Pistol::Count)> PistolKeys{
    "glock", "p345", "revolver", "vp70"
};

struct ScenarioDamage
{
    float shot{}, melee{};
    bool operator==(const ScenarioDamage&) const = default;
};

// Parse/reload on the authoritative game thread during cold preparation.
// A failed transaction preserves the entire previous valid configuration.
// Reads are bounded O(1), with no allocations, I/O or mutable caches.
class ScenarioDamageConfig
{
public:
    bool Parse(std::string_view json, std::string& error);
    const ScenarioDamage* Find(Pistol weapon) const noexcept;
    bool Loaded() const noexcept { return loaded_; }

private:
    std::array<ScenarioDamage, PistolKeys.size()> values_{};
    bool loaded_{};
};
}
