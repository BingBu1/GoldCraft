// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Numeric stages of the CSO CZombiBossImprov (MP 39f962f3..., vtable
// 117FE46C). Pure float32 code: the live behavior supplies entity, team,
// lease and callback gates. Evidence: docs/2026-11-05_实现-通用BOSS-CZombiImprov-report.md.

#include <array>
#include <bit>
#include <cstdint>

#pragma clang fp contract(off)

namespace csol::scenario::zombi_boss
{
// Explicit record ids of the Boss behavior factory 113E5400. Every other
// id (with type.skill >= 1000) reaches the default 113E5496 branch.
inline constexpr std::array<std::int32_t, 42> FactoryExplicitRecords{
    30200, 30300, 30500, 30600, 30700, 30701, 30702, 30703, 30800, 30801, 30802,
    30900, 31000, 31001, 31002, 31200, 31201, 31300, 31400, 31401, 31500, 31600,
    31601, 31700, 31800, 31801, 31802, 31803, 31804, 31805, 31900, 31901, 32000,
    32001, 32100, 32200, 32201, 32300, 32301, 32400, 32500, 32900};

constexpr bool FactoryDefaultRecord(std::int32_t id)
{
    for (const auto record : FactoryExplicitRecords)
        if (record == id) return false;
    return true;
}

// 114BD500: one-step fast inverse square root normalization of a 3D vector.
inline std::array<float, 3> FastNormalize(const std::array<float, 3>& v)
{
    float square = v[0] * v[0];
    square = square + v[1] * v[1];
    square = square + v[2] * v[2];
    const auto bits = std::bit_cast<std::int32_t>(square);
    const float estimate = std::bit_cast<float>(std::int32_t(0x5f3759d5 - (bits >> 1)));
    float scale = square * 0.5f;
    scale = scale * estimate;
    scale = scale * estimate;
    scale = (1.5f - scale) * estimate;
    return {v[0] * scale, v[1] * scale, v[2] * scale};
}

// 113E4CD8..4D5F (common begin, row type 2): store normalized XY times
// row+44 rushPower, and a fixed Z of 200.0 (0x43480000).
inline std::array<float, 3> RushVelocity(const std::array<float, 3>& targetMinusHost, float rushPower)
{
    const auto direction = FastNormalize(targetMinusHost);
    return {direction[0] * rushPower, direction[1] * rushPower, 200.0f};
}

// 113E7948..7976: the Boss feet (v84 + 8) above other.maxs.z + other.origin.z
// + 10.0 (ordered greater; unordered selects the side-contact branch).
inline bool StandsOn(float feetZ, float otherOriginZ, float otherMaxsZ)
{
    const float top = (otherMaxsZ + otherOriginZ) + 10.0f;
    return feetZ > top;
}

// 113E77C0(other, &facing, 1.0, kickSide, kickUp) before 114ECF00.
inline std::array<float, 3> ContactKick(float facingX, float facingY, float kickSide, float kickUp)
{
    const float horizontal = 1.0f * kickSide;
    return {horizontal * facingX, horizontal * facingY, 1.0f * kickUp};
}
}
