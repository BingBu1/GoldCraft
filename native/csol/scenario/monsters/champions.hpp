// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../core/map_resources.hpp"

namespace csol::scenario
{
struct ChampionRow
{
    std::size_t recordIndex{};
    std::int16_t difficulty{};
    float start{}, interval{}, end{};
    std::int16_t maximum{};
    MonsterType type;
};

// MP1138B7E0: rows are filtered by exact room difficulty, then indexed by
// unsigned16 zombitype. The last duplicate replaces the whole schedule/type.
struct ChampionTable
{
    CsvDocument source;
    std::vector<ChampionRow> rows;
    std::map<std::uint16_t, std::size_t> Select(std::uint16_t difficulty) const;
};

bool ReadChampions(std::string_view bytes, ChampionTable& output, ConfigError& error);
bool ReadChampionResource(std::span<const std::byte> encoded,
    ChampionTable& output, ConfigError& error);

// Read once while model precaching is legal. All difficulty appearances are
// retained so a later same-map replay never tries to precache a new model.
// Missing/zero map-specific files fall back to Default; corrupt data fails.
bool LoadChampions(std::string_view mapName, ResourceFileApi files,
    ChampionTable& output, std::string& sourcePath, ResourceError& error);
}
