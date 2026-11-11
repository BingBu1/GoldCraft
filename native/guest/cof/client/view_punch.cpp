// SPDX-License-Identifier: GPL-3.0-or-later
#include "view_punch.hpp"
#include <event_args.h>
#include <ref_params.h>
#include <algorithm>
#include <cmath>
#pragma clang fp contract(off)
namespace guest::cof::client
{
namespace
{
bool Finite(const std::array<float, 3>& a) noexcept
{
    return std::isfinite(a[0]) && std::isfinite(a[1]) && std::isfinite(a[2]);
}
float Length(const std::array<float, 3>& a, bool leadingZero) noexcept
{
    float squared = a[0] * a[0];
    if (leadingZero) squared += 0.0F;
    squared += a[1] * a[1];
    squared += a[2] * a[2];
    return static_cast<float>(std::sqrt(static_cast<double>(squared)));
}
void Advance(ViewPunchState& state, float dt, int maxClients) noexcept
{
    auto& angle = state.angle;
    auto& velocity = state.velocity;
    if (maxClients <= 1)
    {
        const float length = Length(angle, false);
        if (length != 0.0F)
        {
            const float inverse = 1.0F / length;
            for (float& component : angle) component *= inverse;
        }
        const float reduced = static_cast<float>(static_cast<double>(length) -
            (static_cast<double>(length) * 0.5 + 10.0) * static_cast<double>(dt));
        const float scale = reduced > 0.0F ? reduced : 0.0F;
        for (float& component : angle) component *= scale;
        return;
    }
    if (static_cast<double>(Length(angle, true)) <= 0.001 &&
        static_cast<double>(Length(velocity, true)) <= 0.001) return;
    for (unsigned i = 0; i < 3; ++i) angle[i] += velocity[i] * dt;
    const float damping = std::max(0.0F, 1.0F - dt * 9.0F);
    for (float& component : velocity) component *= damping;
    const float spring = -std::min(2.0F, std::max(0.0F, dt * 65.0F));
    for (unsigned i = 0; i < 3; ++i) velocity[i] += angle[i] * spring;
    angle[0] = std::clamp(angle[0], -89.0F, 89.0F);
    angle[1] = std::clamp(angle[1], -179.0F, 179.0F);
    angle[2] = std::clamp(angle[2], -89.0F, 89.0F);
}
}
PunchResult ViewPunchEvent(ViewPunchState& state, const event_args_s& event,
                           bool attachedCamera, int attachMode) noexcept
{
    if (attachedCamera && attachMode <= 0) return PunchResult::CameraBlocked;
    if (!Finite(state.angle) || !Finite(state.velocity) ||
        !std::isfinite(event.origin[0]) || !std::isfinite(event.origin[1]) ||
        !std::isfinite(event.origin[2])) return PunchResult::InvalidInput;
    auto next = state.velocity;
    next[0] -= event.origin[0] * 20.0F;
    next[1] += event.origin[1] * 20.0F;
    next[2] += event.origin[2] * 20.0F;
    if (!Finite(next)) return PunchResult::InvalidInput;
    state.velocity = next;
    return PunchResult::Applied;
}
PunchResult ViewPunchAtStage(ViewPunchState& state, ref_params_s& view, int maxClients) noexcept
{
    if (!Finite(state.angle) || !Finite(state.velocity) || !std::isfinite(view.frametime) ||
        !std::isfinite(view.viewangles[0]) || !std::isfinite(view.viewangles[1]) ||
        !std::isfinite(view.viewangles[2])) return PunchResult::InvalidInput;
    std::array<float, 3> output;
    for (unsigned i = 0; i < 3; ++i) output[i] = view.viewangles[i] + state.angle[i];
    auto next = state;
    Advance(next, view.frametime, maxClients);
    if (!Finite(output) || !Finite(next.angle) || !Finite(next.velocity))
        return PunchResult::InvalidInput;
    for (unsigned i = 0; i < 3; ++i) view.viewangles[i] = output[i];
    state = next;
    return PunchResult::Applied;
}
}
