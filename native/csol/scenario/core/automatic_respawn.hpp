// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace csol::scenario::respawn
{
inline bool TimedSubtype(int subtype)
{
    switch (subtype)
    {
    case 1: case 2: case 3: case 4: case 6: case 10: return true;
    default: return false;
    }
}

// MP108F9C10: base total level, without character/equipment bonus levels.
inline bool Delay(int level, float difficulty, float& seconds)
{
    if (level < 1 || !std::isfinite(difficulty) || difficulty < 0) return false;
    const int units = std::min(12, (level - 1) / 10 + 3);
    seconds = static_cast<float>(units * 5) * difficulty;
    return std::isfinite(seconds) && seconds >= 0;
}

// One death/late-join clock. The SDK owner separately validates its edict,
// userid, map, round and spawn count. Begin is an explicit lifecycle event;
// observing a dead player must never invent a missing start time.
class Clock final
{
public:
    enum class Action { None, Countdown, Spawn };
    struct Step { Action action{}; int seconds{}; std::uint64_t generation{}; };

    bool Begin(float when)
    {
        Cancel();
        if (!std::isfinite(when) || when < 0 ||
            generation_ == std::numeric_limits<std::uint64_t>::max()) return false;
        ++generation_;
        started_ = when;
        armed_ = true;
        return true;
    }
    void Cancel() { armed_ = false; claimed_ = false; due_ = false; nextNotice_ = 0; }
    bool Armed() const { return armed_; }
    std::uint64_t Generation() const { return generation_; }
    Step Poll(float now, float delay)
    {
        due_ = false;
        if (!armed_ || claimed_ || !std::isfinite(now) || now < started_ ||
            !std::isfinite(delay) || delay < 0) return {};
        const float deadline = started_ + delay;
        if (!std::isfinite(deadline)) return {};
        if (now >= deadline)
        {
            due_ = true;
            return {Action::Spawn, 0, generation_};
        }
        if (now < nextNotice_) return {};
        nextNotice_ = now + 1.0f;
        const float remaining = deadline - now;
        const int seconds = remaining >= static_cast<float>(std::numeric_limits<int>::max()) ?
            std::numeric_limits<int>::max() : static_cast<int>(remaining);
        return {Action::Countdown, seconds, generation_};
    }
    bool Claim(std::uint64_t generation)
    {
        if (!armed_ || claimed_ || !due_ || generation != generation_) return false;
        claimed_ = true;
        return true;
    }
private:
    float started_{}, nextNotice_{};
    std::uint64_t generation_{};
    bool armed_{}, claimed_{}, due_{};
};
}
