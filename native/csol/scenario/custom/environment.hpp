// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "phase.hpp"
#include <array>
#include <bit>
#include <cmath>
#include <optional>

namespace csol::scenario::custom
{
struct FogProfile
{
    float density{std::bit_cast<float>(std::uint32_t{0x3a378034})};
    std::array<std::uint8_t, 3> color{1, 1, 1};
    float start{100}, end{2000};
    bool sky{true};
    bool operator==(const FogProfile&) const = default;
};
inline bool Valid(const FogProfile& value) noexcept
{
    return std::isfinite(value.density) && value.density >= 0 && value.density <= .01f &&
        std::isfinite(value.start) && std::isfinite(value.end) && value.start >= 0 &&
        value.end > value.start && value.end <= 1000000;
}
struct EnvironmentConfig
{
    // User requested a darker night after the original c preset was deployed.
    char dusk{'i'}, night{'b'};
    float stepSeconds{2.5f};
    FogProfile fog;
};
inline bool Valid(const EnvironmentConfig& value) noexcept
{
    return value.dusk >= 'a' && value.dusk <= 'z' && value.night >= 'a' &&
        value.night <= value.dusk && std::isfinite(value.stepSeconds) &&
        value.stepSeconds > 0 && value.stepSeconds <= 3600 && Valid(value.fog);
}
struct EnvironmentView
{
    bool active{}, fog{};
    char level{};
    bool operator==(const EnvironmentView&) const = default;
};

// Custom challenge schedule using recovered CSOL light levels/cadence. Noon
// preserves the map, dusk owns i, and night steps to its target with no catchup. Unlike
// the original old-value message, the terminal value reaches every client.
class EnvironmentFade final
{
public:
    bool Begin(std::uint64_t challenge, EnvironmentConfig config) noexcept
    {
        if (!challenge || !Valid(config) || (challenge_ && challenge <= challenge_)) return false;
        config_ = config; challenge_ = challenge; phase_ = Phase::Noon;
        next_ = last_ = 0; sampled_ = false; view_ = {}; return true;
    }
    std::optional<EnvironmentView> Advance(std::uint64_t challenge, Phase phase, float now) noexcept
    {
        if (!challenge_ || challenge != challenge_ || phase > Phase::Ended || phase < phase_ ||
            !std::isfinite(now) || (sampled_ && now < last_)) return {};
        auto value = view_;
        auto next = next_;
        if (phase == Phase::Noon || phase == Phase::Ended) value = {};
        else if (phase != phase_)
        {
            value = {true, false, config_.dusk};
            if (phase == Phase::Night)
            {
                next = now + config_.stepSeconds;
                if (!std::isfinite(next) || next <= now) return {};
            }
        }
        else if (phase == Phase::Night && value.level > config_.night && now > next)
        {
            next = now + config_.stepSeconds;
            if (!std::isfinite(next) || next <= now) return {};
            --value.level;
        }
        value.fog = phase == Phase::Night && value.level == config_.night && config_.fog.density > 0;
        phase_ = phase; last_ = now; sampled_ = true; next_ = next; view_ = value;
        return value;
    }
    const EnvironmentConfig& Config() const noexcept { return config_; }
private:
    EnvironmentConfig config_;
    EnvironmentView view_;
    std::uint64_t challenge_{};
    Phase phase_{Phase::Noon};
    float next_{}, last_{};
    bool sampled_{};
};
}
