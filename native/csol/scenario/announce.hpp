// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "map_resources.hpp"

#include <array>
#include <memory>
#include <span>

namespace csol::scenario
{
struct AnnounceRibbon
{
    std::int32_t id{}, priority{}, category{};
};

struct AnnounceTable
{
    CsvDocument ribbonSource, categorySource;
    // Original lookup uses the row position (id - 1), not the row's ID field.
    std::vector<AnnounceRibbon> ribbons;
    std::map<std::int32_t, std::array<bool, 61>> categories;
    bool Allowed(int mode, std::uint32_t id) const;
};

// Both tables skip exactly their first CSV record. Empty priority/category
// fields leave uninitialized storage in CSO; reject them instead of inventing
// defaults or dropping rows and changing subsequent positional identities.
bool ReadAnnounceTable(std::string_view ribbons, std::string_view categories,
    AnnounceTable& output, ResourceError& error);

class AnnounceResources
{
public:
    bool Load(ResourceFileApi files, ResourceError& error);
    bool Clear();
    std::shared_ptr<const AnnounceTable> Snapshot() const { return table_; }
private:
    std::shared_ptr<const AnnounceTable> table_;
    bool loading_{};
};

struct AnnounceCandidates
{
    std::array<std::uint8_t, 6> ids{};
    std::size_t count{};
    std::span<const std::uint8_t> Values() const { return {ids.data(), count}; }
};

// Counters retain x86 INC wrap and signed comparisons without C++ signed
// overflow. Counting happens before the separate Bot/room/submission gates.
struct AnnounceCounts
{
    std::uint32_t a{}, b{}, c{}, d{};
    bool mixed{};
    AnnounceCandidates Record(int kind);
    // Original partial reset preserves C/D. Its caller timing is not yet
    // established; do not automatically substitute a full reset each round.
    void ResetStreak() { a = b = 0; mixed = false; }
};
}
