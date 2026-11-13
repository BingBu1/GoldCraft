// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "phase.hpp"
#include <cmath>
#include <limits>

namespace csol::scenario::custom
{
struct ChallengePhaseView
{
    PhaseView time{Phase::Noon, 0};
    bool preparing{};
    std::uint64_t challenge{};
};

// User-defined map challenge: first thaw starts time, intermediate stages
// retain it, and only a committed replay creates another challenge.
class ChallengeCycle final
{
public:
    bool Configure(PhaseConfig config) noexcept
    {
        if (stage_.Valid() || !std::isfinite(config.noonSeconds) ||
            !std::isfinite(config.duskSeconds) || config.noonSeconds <= 0 ||
            config.duskSeconds <= 0) return false;
        config_ = config; configured_ = true; return true;
    }
    bool Stage(RoundKey key, bool replay) noexcept
    {
        if (!configured_ || !key.Valid() || (stage_.Valid() &&
            (key.session != stage_.session || key.round <= stage_.round ||
                (ended_ && !replay)))) return false;
        if (!stage_.Valid() || replay)
        {
            if (challenge_ == std::numeric_limits<std::uint64_t>::max()) return false;
            ++challenge_; clock_.Reset(); begun_ = ended_ = sampled_ = false;
            view_ = {{Phase::Noon, config_.noonSeconds}, true, challenge_};
        }
        stage_ = key;
        return true;
    }
    std::optional<ChallengePhaseView> Advance(RoundKey key, float now,
        bool combat, bool finished) noexcept
    {
        if (!stage_.Valid() || key != stage_ || !std::isfinite(now) ||
            (sampled_ && now < last_)) return {};
        // Compute first, then commit; a float deadline overflow must not
        // publish a phase or permit the same challenge to restart.
        if (!ended_ && !finished && !begun_ && combat)
        {
            if (!clock_.Begin({key.session, challenge_}, now, config_)) return {};
            begun_ = true;
        }
        if (finished && !ended_)
        {
            if (begun_ && !clock_.End({key.session, challenge_})) return {};
            ended_ = true;
        }
        auto next = view_;
        if (ended_) next = {{Phase::Ended, 0}, false, challenge_};
        else if (begun_)
        {
            const auto value = clock_.Sample({key.session, challenge_}, now);
            if (!value) return {};
            next = {*value, false, challenge_};
        }
        last_ = now; sampled_ = true; view_ = next;
        return next;
    }
    std::optional<ChallengePhaseView> View(RoundKey key) const noexcept
    { return stage_.Valid() && stage_ == key ? std::optional{view_} : std::nullopt; }
private:
    PhaseClock clock_;
    PhaseConfig config_;
    RoundKey stage_;
    ChallengePhaseView view_;
    std::uint64_t challenge_{};
    float last_{};
    bool configured_{}, begun_{}, ended_{}, sampled_{};
};
}
