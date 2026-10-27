// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "config.hpp"

namespace csol::scenario
{
struct MapCatalogRow
{
    std::size_t recordIndex{};
    int id{};
    std::string nameToken;
    std::string fileName;
    int scenarioType{};
};

struct MapCatalog
{
    CsvDocument source;
    std::vector<MapCatalogRow> rows;
    // Original numeric-ID selection stops at the first matching data row.
    std::map<int, std::size_t> byId;
    const MapCatalogRow* Find(int id) const;
    // The host knows gpGlobals->mapname, not CSO's selected numeric map ID.
    // Accept only when every reachable row for that filename agrees on SubType.
    // This is a host adaptation, not CSO's numeric-ID selection algorithm.
    bool ResolveScenarioType(std::string_view fileName, int& output, ConfigError& error) const;
};

// Plain resource/maplist.csv, preserving CP949 bytes and the original header.
// Fixed columns 0/1/2/58 match the original CSV consumers. Strict schema and
// malformed-number rejection are local validation, not original CSV behavior.
bool ReadMapCatalog(std::string_view bytes, MapCatalog& output, ConfigError& error);
}
