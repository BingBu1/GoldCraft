// SPDX-License-Identifier: GPL-3.0-or-later
#include "phase.hpp"
#include <cmath>

namespace csol::scenario::custom
{
bool PhaseClock::Begin(RoundKey key, float now, PhaseConfig config) noexcept
{
    if (!key.Valid() || key == key_ || !std::isfinite(now) || !std::isfinite(config.noonSeconds) ||
        !std::isfinite(config.duskSeconds) || config.noonSeconds <= 0 || config.duskSeconds <= 0) return false;
    const float dusk = now + config.noonSeconds;
    const float night = dusk + config.duskSeconds;
    if (!std::isfinite(night) || dusk <= now || night <= dusk) return false;
    key_ = key; start_ = now; dusk_ = dusk; night_ = night; ended_ = false;
    return true;
}
std::optional<PhaseView> PhaseClock::Sample(RoundKey key, float now) const noexcept
{
    if (!key_.Valid() || key != key_ || !std::isfinite(now) || now < start_) return {};
    if (ended_) return PhaseView{Phase::Ended, 0};
    if (now < dusk_) return PhaseView{Phase::Noon, dusk_ - now};
    if (now < night_) return PhaseView{Phase::Dusk, night_ - now};
    return PhaseView{Phase::Night, 0};
}
bool PhaseClock::End(RoundKey key) noexcept
{
    if (!key_.Valid() || key != key_) return false;
    ended_ = true; return true;
}
void PhaseClock::Reset() noexcept { key_ = {}; ended_ = false; }
}
