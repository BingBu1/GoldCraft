// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "resources.hpp"
#include "monster_types.hpp"
#include "trigger_rules.hpp"

#include <optional>

namespace csol::scenario
{
class DifficultyResources;
// Signatures match GoldSrc pfnLoadFileForMe / pfnFreeFile. The reported length
// excludes the engine's extra NUL; release every non-null returned allocation.
struct ResourceFileApi
{
    unsigned char* (*load)(const char* path, int* length){};
    void (*release)(void* bytes){};
};

struct ResourceError
{
    std::string path;
    ConfigError detail;
};

struct MapTables
{
    std::string mapName;
    int scenarioType{};
    WaveTable waves;
    RoundTable rounds;
    MonsterTypeTable monsterTypes;
    // Only manager subtypes 1/3/4/10 load this file in the original engine.
    // Presence means loaded successfully, not that all its commands are supported.
    std::optional<TriggerRuleTable> triggerRules;
};

// Own one map session. BeginMap always drops the old session, even when the
// same map is restarted or the new files fail. Reload preserves the working
// tables on failure. All required tables, including optional TRG, commit together.
// This loads data, without activating gameplay.
class MapResources
{
public:
    // scenarioType is the manager's resolved subtype, not the public game-mode
    // number. Its selection belongs to GameRules; do not infer it from filenames
    // or silently apply a trigger table to every scenario map.
    bool BeginMap(std::string_view mapName, ResourceFileApi files, ResourceError& error,
        int scenarioType = 0);
    // Resolve the subtype from resource/maplist.csv on each new map session.
    // No archive scan or stale previous-map fallback. Reload keeps that session's
    // resolved subtype; explicit numeric-ID selection can use BeginMap above.
    bool BeginMapFromCatalog(std::string_view mapName, ResourceFileApi files, ResourceError& error);
    bool Reload(ResourceFileApi files, ResourceError& error);
    // Once for each successfully loaded set of type records, before a round
    // dispatches its first monster. Reload restores unscaled records.
    bool ApplyDifficulty(const DifficultyResources& difficulty, ConfigError& error);
    void Clear();
    std::string_view MapName() const { return mapName_; }
    const MapTables* Tables() const { return tables_ ? &*tables_ : nullptr; }

private:
    std::string mapName_;
    std::optional<int> scenarioType_;
    std::optional<MapTables> tables_;
    bool difficultyApplied_{};
};
}
