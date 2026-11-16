// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "phase.hpp"
#include "settings.hpp"
#include <bit>
#include <cmath>

namespace csol::scenario::custom
{
inline constexpr char NightLightingMessage[] = "CsoNLight";
inline constexpr std::size_t NightLightingMessageSize = 24;
struct NightLightingConfig
{
    bool enabled{true};
    float radius{128};
    std::uint32_t color{0xffffff}; // RRGGBB, before brightness.
    std::uint8_t brightness{48};
    bool operator==(const NightLightingConfig&) const = default;
};
struct NightLightingSnapshot
{
    NightLightingConfig config;
    Phase phase{Phase::Ended};
    std::uint64_t owner{};
    std::uint32_t revision{};
};
inline bool Valid(const NightLightingConfig& value) noexcept
{
    return std::isfinite(value.radius) && value.radius >= 16 && value.radius <= 512 && value.color <= 0xffffff;
}
inline std::optional<NightLightingConfig> ReadNightLighting(const Snapshot& settings) noexcept
{
    const auto radius = settings.Number(Setting::NightPlayerLightRadius);
    const auto brightness = settings.Integer(Setting::NightPlayerLightBrightness);
    const auto color = settings.Integer(Setting::NightPlayerLightColor);
    if (!radius || !brightness || !color || *brightness < 0 || *brightness > 255 || *color < 0) return {};
    NightLightingConfig value{settings.Enabled(Feature::NightPlayerLights), float(*radius),
        std::uint32_t(*color), std::uint8_t(*brightness)};
    return Valid(value) ? std::optional{value} : std::nullopt;
}
inline std::optional<std::array<std::byte, NightLightingMessageSize>> EncodeNightLighting(
    const NightLightingSnapshot& value) noexcept
{
    if (!Valid(value.config) || value.phase > Phase::Ended || !value.owner || !value.revision) return {};
    std::array<std::byte, NightLightingMessageSize> bytes{};
    bytes[0] = std::byte{1}; bytes[1] = std::byte(value.config.enabled);
    bytes[2] = std::byte(value.phase); bytes[3] = std::byte(value.config.brightness);
    const auto word = [&](unsigned offset, std::uint32_t number)
    { for (unsigned i = 0; i < 4; ++i) bytes[offset + i] = std::byte((number >> (8 * i)) & 255); };
    word(4, std::bit_cast<std::uint32_t>(value.config.radius)); word(8, value.config.color);
    word(12, value.revision);
    for (unsigned i = 0; i < 8; ++i) bytes[16 + i] = std::byte((value.owner >> (8 * i)) & 255);
    return bytes;
}
inline std::optional<NightLightingSnapshot> DecodeNightLighting(std::span<const std::byte> bytes) noexcept
{
    if (bytes.size() != NightLightingMessageSize || bytes[0] != std::byte{1} ||
        std::to_integer<unsigned>(bytes[1]) > 1 || std::to_integer<unsigned>(bytes[2]) > unsigned(Phase::Ended)) return {};
    const auto word = [&](unsigned offset)
    {
        std::uint32_t result{};
        for (unsigned i = 0; i < 4; ++i) result |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (8 * i);
        return result;
    };
    NightLightingSnapshot value;
    value.config = {bytes[1] != std::byte{}, std::bit_cast<float>(word(4)), word(8),
        std::to_integer<std::uint8_t>(bytes[3])};
    value.phase = Phase(std::to_integer<unsigned>(bytes[2])); value.revision = word(12);
    for (unsigned i = 0; i < 8; ++i) value.owner |= std::to_integer<std::uint64_t>(bytes[16 + i]) << (8 * i);
    return Valid(value.config) && value.owner && value.revision ? std::optional{value} : std::nullopt;
}
class NightLightingPresentation final
{
public:
    bool Receive(std::span<const std::byte> bytes) noexcept
    {
        const auto value = DecodeNightLighting(bytes);
        if (!value || (known_ && (value->owner < value_.owner ||
            !std::uint32_t(value->revision - value_.revision) ||
            std::uint32_t(value->revision - value_.revision) >= 0x80000000u ||
            (value->owner == value_.owner && value->phase < value_.phase)))) return false;
        value_ = *value; known_ = true; return true;
    }
    const NightLightingSnapshot* View() const noexcept { return known_ ? &value_ : nullptr; }
    bool Active() const noexcept
    { return known_ && value_.phase == Phase::Night && value_.config.enabled && value_.config.brightness && value_.config.color; }
    void Reset() noexcept { value_ = {}; known_ = false; }
private:
    NightLightingSnapshot value_;
    bool known_{};
};
}
