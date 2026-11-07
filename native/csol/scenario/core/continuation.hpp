// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace csol::scenario::consumables
{
// MP10829020 owns one budget for the whole challenge. A successful paid
// continuation consumes it; stage advancement neither refills nor reselects it.
class Continuation final
{
public:
    struct Step
    {
        std::optional<int> offer;
        bool hint{}, expired{}, resume{};
    };
    static bool ValidLimit(float value)
    {
        return std::isfinite(value) && std::trunc(double(value)) >= -1 &&
            double(value) <= std::numeric_limits<int>::max();
    }
    bool InitializeBudget(float column4, bool newChallenge = false)
    {
        if (!newChallenge && initial_ != -2) return true;
        if (!ValidLimit(column4) || generation_ == std::numeric_limits<std::uint64_t>::max()) return false;
        // Invalidate a confirmed old receipt even if the next offer has not
        // started yet. Explicit replay is the only in-map budget reset.
        ++generation_;
        initial_ = remaining_ = static_cast<int>(column4);
        retry_ = resume_ = -1;
        next_ = 0;
        return true;
    }
    bool Begin(float now, int subtype, bool qualifiedInventory)
    {
        if (Active() || !ValidTime(now, 7) || subtype == 5 || !qualifiedInventory ||
            (remaining_ != -1 && remaining_ <= 0) ||
            generation_ == std::numeric_limits<std::uint64_t>::max()) return false;
        ++generation_;
        retry_ = -2;
        next_ = now + 7.0f;
        return true;
    }
    Step Tick(float now, bool externalPending)
    {
        Step result;
        if (!ValidTime(now, 1)) return result;
        if (retry_ != -1)
        {
            if (externalPending) next_ = now + 1.0f;
            if (now >= next_)
            {
                if (retry_ == -2) retry_ = 10;
                result.offer = retry_;
                result.hint = (retry_ & 1) == 0;
                --retry_;
                next_ = now + 1.0f;
                result.expired = retry_ == -1;
            }
        }
        if (resume_ != -1 && now >= next_)
        {
            --resume_;
            next_ = now + 1.0f;
            result.resume = resume_ == -1;
        }
        return result;
    }
    bool Accept(std::uint64_t phase, float now)
    {
        if (!phase || phase != OfferedPhase() || !ValidTime(now, 3)) return false;
        retry_ = -1;
        resume_ = 3;
        next_ = now;
        if (remaining_ > 0) --remaining_;
        return true;
    }
    std::uint64_t OfferedPhase() const { return retry_ >= 0 ? generation_ : 0; }
    int Remaining() const { return remaining_; }
    int Initial() const { return initial_; }
    int Countdown() const { return retry_; }
    bool Active() const { return retry_ != -1 || resume_ != -1; }
private:
    static bool ValidTime(float value, float delay)
    { return std::isfinite(value) && value >= 0 && std::isfinite(value + delay); }
    std::uint64_t generation_{};
    int retry_{-1}, resume_{-1}, remaining_{-2}, initial_{-2};
    float next_{};
};
}
