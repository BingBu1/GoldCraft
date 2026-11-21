// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "body_stains.hpp"

#include <span>

namespace csol::scenario::custom
{
inline constexpr std::uint32_t MonsterStainTag = 0xc000000;
inline constexpr unsigned MonsterStainMax = BodyStainMaxHits;
inline constexpr unsigned MonsterStainIdMax = BodyStainMarkFields >> BodyStainHitBits;
inline constexpr unsigned MonsterStainSubtype = 10;
inline constexpr unsigned MonsterStainHeaderSize = 7;
inline constexpr unsigned MonsterStainMarkSize = 10;
inline constexpr unsigned MonsterStainsPerMessage = 14;
inline constexpr unsigned MonsterStainPacketMax = MonsterStainHeaderSize + MonsterStainsPerMessage * MonsterStainMarkSize;
inline constexpr float MonsterStainPositionScale = 16.f;
inline constexpr float MonsterStainNormalScale = 127.f;

struct MonsterStain
{
    std::uint8_t bone{};
    std::int16_t point[3]{};
    std::int8_t normal[3]{};
};

constexpr unsigned MonsterStainCount(std::uint32_t value) noexcept
{
    return value & MonsterStainMax;
}

constexpr unsigned MonsterStainId(std::uint32_t value) noexcept
{
    return (value & BodyStainMarkFields) >> BodyStainHitBits;
}

constexpr std::uint32_t MonsterStainField(float field) noexcept
{
    const auto value = std::bit_cast<std::uint32_t>(field);
    return (value & ~BodyStainMarkFields) == MonsterStainTag ? value & BodyStainMarkFields : 0;
}

constexpr void WriteMonsterStain(std::span<std::uint8_t, MonsterStainMarkSize> bytes, const MonsterStain& mark) noexcept
{
    bytes[0] = mark.bone;
    for (unsigned axis = 0; axis < 3; ++axis)
    {
        const auto point = std::bit_cast<std::uint16_t>(mark.point[axis]);
        bytes[1 + 2 * axis] = static_cast<std::uint8_t>(point);
        bytes[2 + 2 * axis] = static_cast<std::uint8_t>(point >> 8);
        bytes[7 + axis] = std::bit_cast<std::uint8_t>(mark.normal[axis]);
    }
}

constexpr MonsterStain ReadMonsterStain(std::span<const std::uint8_t, MonsterStainMarkSize> bytes) noexcept
{
    MonsterStain mark{bytes[0]};
    for (unsigned axis = 0; axis < 3; ++axis)
    {
        const auto point = static_cast<std::uint16_t>(bytes[1 + 2 * axis] | (bytes[2 + 2 * axis] << 8));
        mark.point[axis] = std::bit_cast<std::int16_t>(point);
        mark.normal[axis] = std::bit_cast<std::int8_t>(bytes[7 + axis]);
    }
    return mark;
}
}
