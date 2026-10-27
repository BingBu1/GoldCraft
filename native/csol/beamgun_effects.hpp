// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include "weapon_slots.hpp"

namespace csol
{
constexpr char BeamEffectMessage[] = "CSOBeamFX";
constexpr int BeamEffectBytes = 40;
struct BeamEffect
{
    int weapon{}, owner{}, mode{}, phase{}, count{};
    std::array<int, 4> targets{};
    std::array<float, 3> origin{}, end{};
};
inline bool ValidBeamEffect(const BeamEffect& value)
{
    if ((value.weapon != BeamgunId && value.weapon != BeamgunLEId) || value.owner < 1 || value.owner > 32 ||
        value.mode < 0 || value.mode > 6 || value.phase < 0 || value.phase > 4 || value.count < 0 || value.count > 4)
        return false;
    if(value.mode==2 && value.phase>2) return false; // authoritative Normal/Zombie/Scenario
    for (int i = 0; i < 4; ++i)
        if (value.targets[i] < 0 || value.targets[i] > 32 || (i < value.count && !value.targets[i])) return false;
    for (const auto& point : {value.origin, value.end})
        for (const float coordinate : point)
            if (!std::isfinite(coordinate) || std::abs(coordinate) > 131072) return false;
    return true;
}
inline std::array<std::byte, BeamEffectBytes> EncodeBeamEffect(const BeamEffect& value)
{
    std::array<std::byte, BeamEffectBytes> bytes{};
    if (!ValidBeamEffect(value)) return bytes;
    int offset = 0;
    const auto write = [&](std::uint32_t number, int size) {
        for (int i = 0; i < size; ++i) bytes[offset++] = std::byte((number >> (8 * i)) & 255);
    };
    write(1, 1); write(value.weapon, 2); write(value.owner, 2);
    write(value.mode, 1); write(value.phase, 1); write(value.count, 1);
    for (const auto target : value.targets) write(target, 2);
    for (const auto& point : {value.origin, value.end})
        for (float coordinate : point) write(std::bit_cast<std::uint32_t>(coordinate), 4);
    return bytes;
}
inline bool DecodeBeamEffect(std::span<const std::byte> bytes, BeamEffect& output)
{
    if (bytes.size() != BeamEffectBytes || bytes[0] != std::byte{1}) return false;
    int offset = 1;
    const auto read = [&](int size) {
        std::uint32_t number = 0;
        for (int i = 0; i < size; ++i) number |= std::uint32_t(std::to_integer<unsigned char>(bytes[offset++])) << (8 * i);
        return number;
    };
    BeamEffect value;
    value.weapon = read(2); value.owner = read(2); value.mode = read(1); value.phase = read(1); value.count = read(1);
    for (auto& target : value.targets) target = read(2);
    for (auto* point : {&value.origin, &value.end})
        for (auto& coordinate : *point) coordinate = std::bit_cast<float>(read(4));
    if (!ValidBeamEffect(value)) return false;
    output = value;
    return true;
}
}
