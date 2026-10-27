// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "config.hpp"

#include <span>

namespace csol::scenario
{
struct ResourceFileApi;
struct ResourceError;

struct DropItemRule
{
    std::size_t recordIndex{};
    std::int16_t dropId{};
    std::int32_t difficulty{};
    std::int32_t dropEffect{};
    float probability{};
    std::int32_t ownerType{};
    std::int32_t count{};
    std::int32_t removeSeconds{};
    std::int32_t rewardId{};
};

struct DropItemTable
{
    CsvDocument source;
    std::vector<DropItemRule> rows;
    std::int32_t difficulty{};
    // Repeated IDs and rewards are significant. Keep each effective rule in
    // its original relative order instead of flattening it into a scalar map.
    std::map<std::int16_t, std::vector<DropItemRule>> byId;
    std::span<const DropItemRule> Find(std::int16_t dropId) const;
};

// Import the original table for an explicit difficulty. This does not select
// the server difficulty, roll a probability, spawn an item or grant a reward.
// Parsing failure preserves the preceding output and the supplied raw bytes.
// Empty input is the original zero-row table. Missing fields default to zero;
// Decimal underflow preserves signed zero, including the double conversion.
// Overflow and non-finite floats are rejected as host parsing safeguards.
bool ReadDropItemRules(std::string_view bytes, std::int32_t difficulty,
    DropItemTable& output, ConfigError& error);
bool ReadDropItemResource(std::span<const std::byte> encoded, std::int32_t difficulty,
    DropItemTable& output, ConfigError& error);

// Reload the shared maps/ZBS_DropItem.cso resource. Every attempt first clears
// the old table, as in the original manager. Missing or zero-byte files yield
// an empty table; a present malformed resource fails without old-rule fallback.
// The caller supplies the actual difficulty; this layer cannot infer it from
// the map, round number, player count or a translated menu label.
class DropItemResources
{
public:
    bool Load(std::int32_t difficulty, ResourceFileApi files, ResourceError& error);
    bool Clear();
    const DropItemTable& Rules() const { return rules_; }
    bool Present() const { return present_; }
private:
    DropItemTable rules_;
    bool present_{};
    bool loading_{};
};
}
