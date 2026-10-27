// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "spawn_points.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace csol::scenario
{
// Original wave maps and active lists use the same 16-bit key bits. Keep the
// full parsed integer on each source row for provenance and diagnostics.
constexpr std::uint16_t WaveKey(int value) { return static_cast<std::uint16_t>(value); }

struct ConfigError
{
    std::size_t byteOffset{};
    std::string message;
};

struct CsvRecord
{
    std::size_t byteOffset{};
    std::vector<std::string> fields;
};

// Field bytes retain the supplied encoding (original tables use CP949). Keep
// metadata, empty fields and repeated records; do not flatten into a dictionary.
struct CsvDocument
{
    std::string sourceBytes;
    std::vector<CsvRecord> records;
};

bool ReadCsv(std::string_view bytes, CsvDocument& output, ConfigError& error);

struct WaveRow
{
    std::size_t recordIndex{};
    int waveId{};
    int spawnPoint{};
    std::int32_t startTime{};
    std::int32_t endTime{};
    std::int16_t interval{};
    std::vector<int> types;
    std::vector<std::int8_t> weights;
    std::int8_t weightTotal{};
    std::string eventMessage;
    bool IsEvent() const { return spawnPoint == 9999; }
};

struct WaveTable
{
    CsvDocument source;
    std::vector<WaveRow> rows;
    // Keys are WaveKey(waveId). Indices preserve duplicates and source order.
    std::map<int, std::map<int, std::vector<std::size_t>>> groups;
};

struct RoundRow
{
    std::size_t recordIndex{};
    int round{};
    std::vector<int> waves;
};

struct RoundTable
{
    CsvDocument source;
    std::size_t headerRecord{};
    std::vector<RoundRow> rows;
};

// These import decoded CSV. Archive selection/decryption and game-state
// activation belong to the loader, not to this data layer. On failure the last
// valid output is unchanged; unsupported or malformed fields are reported.
bool ReadWaves(std::string_view bytes, WaveTable& output, ConfigError& error);
bool ReadRounds(std::string_view bytes, RoundTable& output, ConfigError& error);

enum class MissingReferenceKind { Wave, MonsterPoint };
struct MissingReference
{
    MissingReferenceKind kind;
    std::size_t roundRecord{};
    std::size_t waveRecord{};
    int round{};
    int wave{};
    int spawnPoint{};
};

// Report each use with its source record. Never synthesize missing waves or
// points, and never treat event point 9999 as a monster spawn.
std::vector<MissingReference> FindMissingReferences(
    const RoundTable& rounds, const WaveTable& waves, const SpawnPoints& points,
    int monsterTeam);
}
