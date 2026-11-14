// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cmath>
#include <optional>

namespace csol::scenario::custom
{
using MovementVector = std::array<float, 3>;
struct CofMoveCommand
{
    float forward{}, side{}, up{};
    bool operator==(const CofMoveCommand&) const = default;
};
struct CofGroundWish
{
    CofMoveCommand limited;
    MovementVector velocity{}, direction{};
    float speed{};
};

inline bool FiniteMovement(MovementVector value) noexcept
{ return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]); }

// hl.dll100033D0: scalar float squares/adds, double sqrt, float length, then
// float reciprocal/multiplication. Do not replace with approximate rsqrt/FMA.
inline float NormalizeCofVector(MovementVector& value) noexcept
{
#pragma clang fp contract(off)
    const float sum = (value[0] * value[0] + value[1] * value[1]) + value[2] * value[2];
    const float length = static_cast<float>(std::sqrt(static_cast<double>(sum)));
    if (length != 0)
    {
        const float inverse = 1.0f / length;
        for (auto& axis : value) axis *= inverse;
    }
    return length;
}

// Actual CoF PM_CheckParameters10004203..A9 limits raw3D command first.
// Invalid host input returns no result; it is never normalized into authority.
inline std::optional<CofMoveCommand> LimitCofCommand(CofMoveCommand command, float maximum) noexcept
{
#pragma clang fp contract(off)
    if (!std::isfinite(maximum) || maximum <= 0 ||
        !FiniteMovement({command.forward, command.side, command.up})) return {};
    const float sum = (command.side * command.side + command.forward * command.forward) + command.up * command.up;
    if (!std::isfinite(sum)) return {};
    const float length = static_cast<float>(std::sqrt(static_cast<double>(sum)));
    if (length != 0 && length > maximum)
    {
        const float scale = maximum / length;
        command.forward *= scale; command.side *= scale; command.up *= scale;
    }
    return command;
}

// Normal-ground wish stage10009F42..9FFA. The caller supplies the recovered
// client percentage (75/45/125), not an absolute unit speed. Special surfaces,
// acceleration, command timing and real host PM ownership remain downstream.
inline std::optional<CofGroundWish> CofNormalGroundWish(CofMoveCommand command,
    MovementVector forward, MovementVector right, float maximum, float percentage) noexcept
{
#pragma clang fp contract(off)
    if (!FiniteMovement(forward) || !FiniteMovement(right) ||
        !std::isfinite(percentage) || percentage < 0) return {};
    const auto limited = LimitCofCommand(command, maximum);
    if (!limited) return {};
    forward[2] = right[2] = 0;
    if (!std::isfinite(NormalizeCofVector(forward)) || !std::isfinite(NormalizeCofVector(right))) return {};
    CofGroundWish result;
    result.limited = *limited;
    for (unsigned i = 0; i != 2; ++i)
        result.velocity[i] = right[i] * limited->side + forward[i] * limited->forward;
    if (!FiniteMovement(result.velocity)) return {};
    result.direction = result.velocity;
    const float rawSpeed = NormalizeCofVector(result.direction);
    result.speed = (percentage * rawSpeed) / 100.0f;
    if (!std::isfinite(rawSpeed) || !std::isfinite(result.speed)) return {};
    if (result.speed > maximum)
    {
        const float scale = maximum / result.speed;
        for (auto& axis : result.velocity) axis *= scale;
        result.speed = maximum;
    }
    return result;
}
}
