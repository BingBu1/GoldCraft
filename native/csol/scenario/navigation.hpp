// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "config.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace csol::scenario
{
struct NavPoint { float x{}, y{}, z{}; };
struct NavHidingSpot
{
    std::uint32_t id{};
    NavPoint position;
    std::uint8_t flags{};
};
struct NavApproach
{
    std::uint32_t here{}, previous{}, next{};
    std::uint8_t previousHow{}, nextHow{};
};
struct NavEncounterSpot { std::uint32_t id{}; std::uint8_t position{}; };
struct NavEncounter
{
    std::uint32_t from{}, to{};
    std::uint8_t fromDirection{}, toDirection{};
    std::vector<NavEncounterSpot> spots;
};
struct NavAreaRecord
{
    std::uint32_t id{};
    std::uint8_t attributes{};
    NavPoint low, high;
    float northEastZ{}, southWestZ{};
    std::array<std::vector<std::uint32_t>, 4> connections;
    std::vector<NavHidingSpot> hidingSpots;
    std::vector<NavApproach> approaches;
    std::vector<NavEncounter> encounters;
    std::uint16_t place{};
};
struct NavigationFile
{
    std::uint32_t bspSize{};
    std::vector<std::string> places;
    std::vector<NavAreaRecord> areas;
    std::size_t trailingBytes{};
};

// The first-season source map uses v5. Older location-file conversion is a
// separate format: report it explicitly instead of reading v1..4 as v5.
// Errors retain the byte offset in this binary format.
bool ReadNavigationV5(std::span<const std::byte> bytes, NavigationFile& output,
    ConfigError& error);
}
