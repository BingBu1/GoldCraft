// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "goldcraft/resource_sync.hpp"
#include <cstddef>
#include <cstdint>
#include <string>

// Include after the engine SDK (client_t). All calls are main-thread only.
constexpr bool GC_ResourceSyncEnabled() {
#if defined(GOLDCRAFT_RESOURCE_SYNC_V4) && GOLDCRAFT_RESOURCE_SYNC_V4
    return true;
#else
    return false;
#endif
}
bool GC_ResourceSyncBuild(std::string& error);
void GC_ResourceSyncReset();
bool GC_ResourceSyncReady();
std::uint64_t GC_ResourceSyncGeneration();
const goldcraft::resource_sync::Entry* GC_ResourceSyncEntry(std::size_t ordinal);
bool GC_ResourceSyncClient(const client_t* client);
