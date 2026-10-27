// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "config.hpp"

#include <array>
#include <span>

namespace csol::scenario
{
struct ScenarioMessage
{
    std::size_t recordIndex{};
    std::uint16_t id{};
    // Keep original resource bytes, including localization keys and whitespace.
    // Localization, character conversion and rendering belong to the client.
    std::string text;
    std::string image;
    std::string sound;
    std::array<std::uint8_t, 3> color{255, 180, 30};
    float fadeIn{0.5f};
    float show{3.0f};
    float fadeOut{0.5f};
    std::int32_t type{};
    std::int32_t textType{-1};
    float flickerTime{-1.0f};
    float xCenterRatio{-1.0f};
    float yCenterRatio{-1.0f};
};

struct MessageTable
{
    CsvDocument source;
    std::vector<ScenarioMessage> rows;
    // The original client overwrites an existing node, including low-16-bit
    // collisions. Retain all source rows, but resolve a key to its last row.
    std::map<std::uint16_t, std::size_t> byId;
    const ScenarioMessage* Find(std::uint16_t id) const;
};

// Fixed 15-column layout, with the first record skipped as in the original.
// Empty/absent optional fields retain defaults; malformed input is transactional.
bool ReadMessageTable(std::string_view bytes, MessageTable& output, ConfigError& error);
bool ReadMessageTableResource(std::span<const std::byte> encoded,
    MessageTable& output, ConfigError& error);
}
