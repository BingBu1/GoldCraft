// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "environment.hpp"
#include <span>

namespace csol::scenario::custom
{
inline constexpr char EnvironmentMessage[] = "CsoEnv";
inline constexpr std::size_t EnvironmentMessageSize = 32;
struct EnvironmentSnapshot
{
    EnvironmentView view;
    FogProfile profile;
    std::uint64_t owner{};
    std::uint32_t revision{};
};
inline bool Valid(const EnvironmentSnapshot& value) noexcept
{
    return value.owner && value.revision && Valid(value.profile) &&
        (value.view.active ? value.view.level >= 'a' && value.view.level <= 'z' :
            !value.view.level && !value.view.fog) &&
        (!value.view.fog || (value.view.active && value.profile.density > 0));
}
inline std::optional<std::array<std::byte, EnvironmentMessageSize>> EncodeEnvironment(const EnvironmentSnapshot& value) noexcept
{
    if (!Valid(value)) return {};
    std::array<std::byte, EnvironmentMessageSize> bytes{};
    bytes[0] = std::byte{1}; bytes[1] = std::byte(value.view.active);
    bytes[2] = std::byte(value.view.fog); bytes[3] = std::byte(value.profile.sky);
    bytes[4] = std::byte(value.view.level);
    const auto word = [&](unsigned offset, std::uint32_t number)
    { for (unsigned i = 0; i != 4; ++i) bytes[offset + i] = std::byte((number >> (8 * i)) & 255); };
    word(5, value.revision);
    for (unsigned i = 0; i != 8; ++i) bytes[9 + i] = std::byte((value.owner >> (8 * i)) & 255);
    word(17, std::bit_cast<std::uint32_t>(value.profile.density));
    for (unsigned i = 0; i != 3; ++i) bytes[21 + i] = std::byte(value.profile.color[i]);
    word(24, std::bit_cast<std::uint32_t>(value.profile.start));
    word(28, std::bit_cast<std::uint32_t>(value.profile.end));
    return bytes;
}
inline std::optional<EnvironmentSnapshot> DecodeEnvironment(std::span<const std::byte> bytes) noexcept
{
    if (bytes.size() != EnvironmentMessageSize || bytes[0] != std::byte{1}) return {};
    for (unsigned i = 1; i != 4; ++i) if (std::to_integer<unsigned>(bytes[i]) > 1) return {};
    const auto word = [&](unsigned offset)
    {
        std::uint32_t value{};
        for (unsigned i = 0; i != 4; ++i) value |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (8 * i);
        return value;
    };
    EnvironmentSnapshot value;
    value.view = {bytes[1] != std::byte{}, bytes[2] != std::byte{}, char(std::to_integer<unsigned>(bytes[4]))};
    value.profile.sky = bytes[3] != std::byte{}; value.revision = word(5);
    for (unsigned i = 0; i != 8; ++i) value.owner |= std::to_integer<std::uint64_t>(bytes[9 + i]) << (8 * i);
    value.profile.density = std::bit_cast<float>(word(17));
    for (unsigned i = 0; i != 3; ++i) value.profile.color[i] = std::to_integer<std::uint8_t>(bytes[21 + i]);
    value.profile.start = std::bit_cast<float>(word(24)); value.profile.end = std::bit_cast<float>(word(28));
    return Valid(value) ? std::optional{value} : std::nullopt;
}
class EnvironmentPresentation final
{
public:
    bool Receive(std::span<const std::byte> bytes) noexcept
    {
        const auto value = DecodeEnvironment(bytes);
        if (!value || (known_ && (value->owner < value_.owner ||
            !std::uint32_t(value->revision - value_.revision) ||
            std::uint32_t(value->revision - value_.revision) >= 0x80000000u ||
            (value->owner == value_.owner && retired_ && value->view.active)))) return false;
        retired_ = known_ && value->owner == value_.owner &&
            (retired_ || (value_.view.active && !value->view.active));
        value_ = *value; known_ = true; return true;
    }
    const EnvironmentSnapshot* View() const noexcept { return known_ ? &value_ : nullptr; }
    void Reset() noexcept { value_ = {}; known_ = retired_ = false; }
private:
    EnvironmentSnapshot value_;
    bool known_{}, retired_{};
};
}
