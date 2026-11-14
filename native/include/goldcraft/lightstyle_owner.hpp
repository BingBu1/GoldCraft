// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>

namespace goldcraft
{
inline constexpr unsigned LightStylePatternBytes = 64;

// Host adaptation: retain external style0 writes while one map-scoped effect
// owns its presentation. No client/private-engine layout is involved.
class LightStyleOwner
{
public:
    void EnterMap(std::uint32_t map) noexcept
    {
        if (mapKnown_ && map_ == map) return;
        ForgetMap(); map_ = map; mapKnown_ = true;
    }
    void ForgetMap() noexcept
    {
        mapKnown_ = known_ = applied_ = false;
        owner_ = 0; baseline_.fill(0); level_.fill(0);
    }
    const char* Observe(const char* pattern) noexcept
    {
        if (!mapKnown_ || !pattern) return pattern;
        unsigned length = 0;
        while (length + 1 < baseline_.size() && pattern[length]) ++length;
        std::memmove(baseline_.data(), pattern, length);
        baseline_[length] = 0;
        known_ = true;
        return applied_ ? level_.data() : pattern;
    }
    std::uint64_t Acquire() noexcept
    {
        if (!mapKnown_ || !known_ || owner_ || serial_ == std::numeric_limits<std::uint64_t>::max()) return 0;
        owner_ = ++serial_; return owner_;
    }
    bool Set(std::uint64_t owner, char level) noexcept
    {
        if (!Owns(owner) || level < 'a' || level > 'z') return false;
        level_[0] = level; level_[1] = 0; applied_ = true; return true;
    }
    bool Release(std::uint64_t owner) noexcept
    {
        if (!Owns(owner)) return false;
        owner_ = 0; applied_ = false; level_.fill(0); return true;
    }
    bool Owns(std::uint64_t owner) const noexcept
    { return mapKnown_ && owner && owner_ == owner; }
    bool Known() const noexcept { return known_; }
    bool Applied() const noexcept { return applied_; }
    std::uint32_t Map() const noexcept { return map_; }
    std::uint64_t Owner() const noexcept { return owner_; }
    const char* Baseline() const noexcept { return baseline_.data(); }
    const char* Level() const noexcept { return level_.data(); }
private:
    std::array<char, LightStylePatternBytes> baseline_{};
    std::array<char, 2> level_{};
    std::uint64_t serial_{}, owner_{};
    std::uint32_t map_{};
    bool mapKnown_{}, known_{}, applied_{};
};
}
