// SPDX-License-Identifier: GPL-3.0-or-later
#include "oberon_combat.hpp"
#include <algorithm>
#include <bit>
#include <cmath>

#pragma clang fp contract(off)

namespace csol::scenario::oberon
{
namespace
{
bool Finite(const Position& value)
{ return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]); }

float Cosine(float degrees)
{
    // MP1133C5F0 narrows the double angle before calling the CRT cosine.
    static const auto values = []
    {
        std::array<float, 256> result{};
        for (unsigned i = 0; i != result.size(); ++i)
        {
            const float angle = static_cast<float>((double(i) * 6.283185307179586) / 255.0);
            result[i] = static_cast<float>(std::cos(double(angle)));
        }
        return result;
    }();
    // Preserve repeated-float normalization for normal engine angles. The
    // original additionally maps non-progressing (huge/NaN) values to zero.
    if (!std::isfinite(degrees) || degrees - 360.0f == degrees) degrees = 0;
    while (degrees < 0) degrees += 360.0f;
    while (degrees >= 360.0f) degrees -= 360.0f;
    const int index = -static_cast<int>((degrees * -255.0f) / 360.0f);
    return values[static_cast<std::size_t>(index)];
}

Hit Impact(float damage, float falloff, float x, float y, float side, float up)
{
    const float horizontal = falloff * side;
    return {damage, {x * horizontal, y * horizontal, falloff * up}};
}
}

float InverseLength(float square)
{
    // Original114BBE90, including the 0x5f3759d5 constant and SSE order.
    const auto bits = std::bit_cast<std::int32_t>(square);
    const float estimate = std::bit_cast<float>(std::int32_t(0x5f3759d5 - (bits >> 1)));
    const float half = square * 0.5f;
    const float product = (half * estimate) * estimate;
    return (1.5f - product) * estimate;
}

std::array<float, 2> Forward(float yaw) { return {Cosine(yaw), Cosine(yaw - 90.0f)}; }

std::optional<Hit> Sector(const BossSkillRow& row, const Position& boss,
    float minimumZ, float feetZ, float yaw, const Position& target)
{
    if (!Finite(boss) || !Finite(target) || !std::isfinite(yaw) || !(row.damageDistance > 0)) return {};
    const float x = target[0] - boss[0], y = target[1] - boss[1];
    const float square = y * y + x * x;
    if (!(square > 0 && square <= row.damageDistance * row.damageDistance) ||
        target[2] < minimumZ - 118.11000061035156f - 36.0f ||
        target[2] > minimumZ + row.damageHeight + 36.0f) return {};
    const float inverse = InverseLength(square);
    const float nx = x * inverse, ny = y * inverse;
    const float falloff = 1.0f - (1.0f / inverse) / row.damageDistance;
    float damage = 99999.0f;
    if (!(target[2] < feetZ))
    {
        const auto forward = Forward(yaw);
        if (!(forward[0] * nx + forward[1] * ny > row.damageCosine)) return {};
        damage = falloff * row.damage;
    }
    if (damage == 0 || !std::isfinite(damage)) return {};
    return Impact(damage, falloff, nx, ny, row.kickSide, row.kickUp);
}

std::optional<Hit> Cylinder(const BossSkillRow& row, const Position& boss,
    float minimumZ, float maximumZ, float feetZ, bool suppressBelowFeetKill,
    const Position& target)
{
    if (!Finite(boss) || !Finite(target) || !(row.damageDistance > 0)) return {};
    const float x = target[0] - boss[0], y = target[1] - boss[1];
    const float square = y * y + x * x;
    if (!(square < row.damageDistance * row.damageDistance) ||
        target[2] < minimumZ || target[2] > maximumZ) return {};
    const float distance = static_cast<float>(std::sqrt(double(square)));
    const float falloff = 1.0f - distance / row.damageDistance;
    const float damage = !suppressBelowFeetKill && target[2] < feetZ ? 99999.0f : falloff * row.damage;
    // Native stability difference: coincident XY has no horizontal direction.
    // Keep its damage and vertical impulse without writing NaN to movement.
    const float nx = distance > 0 ? x / distance : 0;
    const float ny = distance > 0 ? y / distance : 0;
    if (!std::isfinite(damage)) return {};
    return Impact(damage, falloff, nx, ny, row.kickSide, row.kickUp);
}

std::optional<Hit> Bomb(float damage, float radius, float side, float up,
    const Position& origin, const Position& target)
{
    if (!Finite(origin) || !Finite(target) || !(damage > 0) || !(radius > 0)) return {};
    const float x = target[0] - origin[0], y = target[1] - origin[1], z = target[2] - origin[2];
    const float square = (y * y + x * x) + z * z;
    if (!(square < radius * radius)) return {};
    const float inverse = InverseLength(square);
    const float distance = 1.0f / inverse;
    const float attenuated = std::max(0.0f, (radius - distance) / radius) * damage;
    if (!(attenuated > 0)) return {};
    // Original11399430 uses full base damage; attenuation affects knockback.
    return Impact(damage, attenuated / damage, x * inverse, y * inverse, side, up);
}

BombLaunch Launch(const Position& boss, float yaw, int offset, const std::array<float, 2>& target)
{
    const auto forward = Forward(yaw);
    BombLaunch result;
    result.origin = {boss[0] + forward[0] * 90.0f, boss[1] + forward[1] * 90.0f,
        boss[2] + float(offset) - 20.0f};
    result.flightTime = static_cast<float>(std::sqrt(8.907642364501953));
    result.velocity = {(target[0] - result.origin[0]) / result.flightTime,
        (target[1] - result.origin[1]) / result.flightTime, 1968.5f / result.flightTime};
    return result;
}
}
