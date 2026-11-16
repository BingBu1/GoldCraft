// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cmath>
#include <optional>

namespace csol::scenario::custom
{
// MP39f962f3, 108595D0 displacement arithmetic. Native ResetMaxSpeed belongs
// to the host owner. The user excludes StandAlone noise/AI from this port.
struct StandaloneRunState
{
    std::array<float, 3> previous{};
    bool requested{}, running{};
};
inline std::optional<float> AdvanceStandaloneRunDisplacement(StandaloneRunState& state,
    float& amount, const std::array<float, 3>& origin, bool ducking, float costPerUnit = .1f) noexcept
{
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
    if (!std::isfinite(amount) || amount < 0 || !std::isfinite(costPerUnit) || costPerUnit < 0)
        return {};
    for (unsigned i = 0; i < origin.size(); ++i)
        if (!std::isfinite(origin[i]) || !std::isfinite(state.previous[i])) return {};
    auto next = state;
    if (!next.requested)
    {
        next.running = false;
        state = next;
        return 0;
    }
    if (next.requested != next.running) next.previous = origin;
    if (ducking)
    {
        state = next;
        return 0;
    }
    next.running = true;
    const float dy = origin[1] - next.previous[1];
    const float dx = origin[0] - next.previous[0];
    const float dySquared = dy * dy;
    const float dxSquared = dx * dx;
    const float squared = dySquared + dxSquared;
    const float distance = static_cast<float>(std::sqrt(static_cast<double>(squared)));
    const float debit = distance * costPerUnit;
    const float remaining = amount - debit;
    if (!std::isfinite(distance) || !std::isfinite(debit) || !std::isfinite(remaining)) return {};
    amount = remaining;
    if (amount <= 0)
    {
        amount = 0;
        next.running = false;
    }
    next.previous = origin;
    state = next;
    return distance;
}

// MP108588E0. The second engine query occurs only when the first cap is
// exceeded. A reader keeps the original callback ordering without allocation.
template<class ReadLimit>
inline float StandaloneMaxSpeed(float weaponSpeed, bool ducking, bool running, ReadLimit&& readLimit)
{
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
    float speed = weaponSpeed;
    if (ducking) speed *= .8f;
    else if (!running) speed *= .5f;
    if (speed > readLimit()) speed = readLimit();
    return speed;
}
}
