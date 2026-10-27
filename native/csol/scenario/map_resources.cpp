// SPDX-License-Identifier: GPL-3.0-or-later
#include "map_resources.hpp"
#include "map_catalog.hpp"
#include "difficulty_monsters.hpp"

#include <algorithm>
#include <memory>
#include <utility>

namespace csol::scenario
{
namespace
{
bool MapNameValid(std::string_view name)
{
    return !name.empty() && name.size() <= 63 &&
        std::all_of(name.begin(), name.end(), [](unsigned char c)
        {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '_' || c == '-';
        });
}

template<class Table, class Reader>
bool ReadFile(const std::string& path, ResourceFileApi files,
    Table& table, Reader reader, ResourceError& error)
{
    int length = 0;
    std::unique_ptr<unsigned char, decltype(files.release)> bytes(
        files.load(path.c_str(), &length), files.release);
    ConfigError detail;
    if (!bytes) detail.message = "scenario configuration file is missing";
    else if (length <= 0 || length > 8 * 1024 * 1024 || length % 8)
        detail.message = "invalid encrypted scenario configuration size";
    else if (reader(std::as_bytes(std::span(bytes.get(), static_cast<std::size_t>(length))),
        table, detail)) return true;
    error = {path, std::move(detail)};
    return false;
}
}

bool MapResources::BeginMapFromCatalog(std::string_view mapName, ResourceFileApi files, ResourceError& error)
{
    std::string nextMap(mapName);
    Clear();
    error = {};
    if (!MapNameValid(nextMap))
    {
        error.detail.message = "scenario map name must contain 1..63 ASCII letters, digits, underscores or hyphens";
        return false;
    }
    mapName_ = std::move(nextMap);
    constexpr const char* path = "resource/maplist.csv";
    if (!files.load || !files.release)
    {
        error = {path, {0, "scenario file load and release callbacks are required"}};
        return false;
    }
    MapCatalog catalog;
    int scenarioType{};
    {
        int length = 0;
        std::unique_ptr<unsigned char, decltype(files.release)> bytes(files.load(path, &length), files.release);
        if (!bytes)
        {
            error = {path, {0, "CSOL map catalog is missing"}};
            return false;
        }
        if (length <= 0 || length > 8 * 1024 * 1024)
        {
            error = {path, {0, "invalid plain map catalog size"}};
            return false;
        }
        ConfigError detail;
        if (!ReadMapCatalog(std::string_view(reinterpret_cast<const char*>(bytes.get()),
            static_cast<std::size_t>(length)), catalog, detail) ||
            !catalog.ResolveScenarioType(mapName_, scenarioType, detail))
        {
            error = {path, std::move(detail)};
            return false;
        }
    }
    // Free the catalog buffer before requesting the map's table files.
    scenarioType_ = scenarioType;
    return Reload(files, error);
}

bool MapResources::BeginMap(std::string_view mapName, ResourceFileApi files, ResourceError& error,
    int scenarioType)
{
    // The caller may pass MapName() when restarting the same map.
    std::string nextMap(mapName);
    Clear();
    error = {};
    if (!MapNameValid(nextMap))
    {
        error.detail.message = "scenario map name must contain 1..63 ASCII letters, digits, underscores or hyphens";
        return false;
    }
    mapName_ = std::move(nextMap);
    scenarioType_ = scenarioType;
    return Reload(files, error);
}

bool MapResources::Reload(ResourceFileApi files, ResourceError& error)
{
    error = {};
    if (mapName_.empty())
    {
        error.detail.message = "no scenario map session";
        return false;
    }
    if (!scenarioType_)
    {
        error = {"resource/maplist.csv", {0, "scenario subtype is unresolved; begin the catalog session again"}};
        return false;
    }
    if (!files.load || !files.release)
    {
        error.detail.message = "scenario file load and release callbacks are required";
        return false;
    }
    MapTables candidate;
    candidate.mapName = mapName_;
    candidate.scenarioType = *scenarioType_;
    if (!ReadFile("maps/zbs_wave_" + mapName_ + ".cso", files,
        candidate.waves, ReadWaveResource, error) ||
        !ReadFile("maps/zbs_" + mapName_ + ".cso", files,
        candidate.rounds, ReadRoundResource, error) ||
        !ReadFile("maps/ZBS_ZT_" + mapName_ + ".cso", files,
        candidate.monsterTypes, ReadMonsterTypeResource, error)) return false;
    if (scenarioType_ == 1 || scenarioType_ == 3 || scenarioType_ == 4 || scenarioType_ == 10)
    {
        candidate.triggerRules.emplace();
        if (!ReadFile("maps/ZBS_TRG_" + mapName_ + ".cso", files,
            *candidate.triggerRules, ReadTriggerRuleResource, error)) return false;
    }
    tables_ = std::move(candidate);
    difficultyApplied_ = false;
    return true;
}

bool MapResources::ApplyDifficulty(const DifficultyResources& difficulty, ConfigError& error)
{
    error = {};
    if (!tables_)
    {
        error.message = "monster difficulty requires loaded map types";
        return false;
    }
    if (difficultyApplied_) return true;
    if (!ApplyMonsterDifficulty(tables_->monsterTypes, difficulty, error)) return false;
    difficultyApplied_ = true;
    return true;
}

void MapResources::Clear()
{
    tables_.reset();
    mapName_.clear();
    scenarioType_.reset();
    difficultyApplied_ = false;
}
}
