// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "config.hpp"
#include "map_resources.hpp"

#include <optional>

namespace csol::scenario
{
// Preserve physical rows. The original indexes by the room's saved unsigned
// short, not by column 0's ID, and reads each requested parameter as a float.
struct DifficultyTable
{
    CsvDocument source;
    bool Parameter(std::size_t row, std::size_t column, float& output, ConfigError& error) const;
    std::string_view NameToken(std::size_t row) const;
};

bool ReadDifficultyTable(std::string_view bytes, DifficultyTable& output, ConfigError& error);
bool ReadDifficultyResource(std::span<const std::byte> encoded,
    DifficultyTable& output, ConfigError& error);

enum class DifficultyState { Empty, Loading, Ready, Failed };

// The selected room value is captured at BeginMap. Reload changes only the
// table, as in the original independent reload function. GameRules/admin code
// owns allowed values, map limits and when a new selection takes effect.
class DifficultyResources
{
public:
    DifficultyResources() = default;
    DifficultyResources(const DifficultyResources&) = delete;
    DifficultyResources& operator=(const DifficultyResources&) = delete;
    bool BeginMap(std::string_view mapName, std::uint16_t difficulty,
        ResourceFileApi files, ResourceError& error);
    bool Reload(std::string_view mapName, ResourceFileApi files, ResourceError& error);
    bool Clear();
    bool Parameter(int gameMode, std::size_t column, float& output, ConfigError& error) const;
    DifficultyState State() const { return state_; }
    std::uint16_t SelectedRow() const { return difficulty_; }
    const DifficultyTable* Table() const { return table_ ? &*table_ : nullptr; }
    std::string_view SourcePath() const { return path_; }

private:
    bool Load(std::string_view mapName, ResourceFileApi files, ResourceError& error);
    std::optional<DifficultyTable> table_;
    std::string path_;
    std::uint16_t difficulty_{};
    DifficultyState state_{DifficultyState::Empty};
};
}
