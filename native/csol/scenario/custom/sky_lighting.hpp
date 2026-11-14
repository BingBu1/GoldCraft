// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cmath>
#include <cstdlib>
#include <optional>

namespace csol::scenario::custom
{
using SkyColor = std::array<float, 3>;

// CSOL HW378E14F3..157D: integer sky channels, style0*1/256, then
// truncation. This is the ordinary nonnegative sky-color domain used by maps.
inline std::optional<SkyColor> ScaleSkyLight(SkyColor input, char level) noexcept
{
    if (level < 'a' || level > 'z') return {};
    const float factor = float((int(level) - int('a')) * 22) * (1.0f / 256.0f);
    for (auto& channel : input)
    {
        if (!std::isfinite(channel) || channel < 0 || channel > 255) return {};
        channel = float(static_cast<int>(float(static_cast<int>(channel)) * factor));
    }
    return input;
}
class PreparedSkyLight final
{
public:
    void Prepare(SkyColor source, char level) noexcept
    {
        if (known_ && source_ == source && level_ == level) return;
        source_ = source; level_ = level; output_ = ScaleSkyLight(source, level); known_ = true;
    }
    const std::optional<SkyColor>& Output() const noexcept { return output_; }
    const SkyColor& Source() const noexcept { return source_; }
    void Reset() noexcept { source_ = {}; output_.reset(); level_ = 0; known_ = false; }
private:
    SkyColor source_;
    std::optional<SkyColor> output_;
    char level_{};
    bool known_{};
};

// Keep the archived string. A native edit equal to the forced value is seen
// on release; direct numeric edits are retained too. Parsing is a cold action.
class NumericCvarOwner final
{
public:
    void Force(float& value, const char* string, float desired) noexcept
    {
        if (!active_ || value != applied_)
        { saved_ = value; parsed_ = Parse(string); }
        active_ = true; applied_ = desired; value = desired;
    }
    void Release(float& value, const char* string) noexcept
    {
        if (active_ && value == applied_)
        {
            const auto current = Parse(string);
            value = current && current != parsed_ ? *current : saved_;
        }
        active_ = false;
    }
    bool Active() const noexcept { return active_; }
private:
    static std::optional<float> Parse(const char* text) noexcept
    {
        if (!text) return {};
        char* end{}; const float value = std::strtof(text, &end);
        return end != text && std::isfinite(value) ? std::optional{value} : std::nullopt;
    }
    std::optional<float> parsed_;
    float saved_{}, applied_{};
    bool active_{};
};
}
