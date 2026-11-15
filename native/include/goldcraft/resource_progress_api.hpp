// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace goldcraft::resource_progress {
inline constexpr char interface_name[] = "GoldCraftResourceProgress001";
enum class Phase : std::uint32_t { checking, downloading, verifying, refreshing };
struct Snapshot {
    Phase phase{};
    std::uint32_t completed{}, total{}, downloaded{};
};
// Read-only main-thread snapshot. Never pumps UI, network or resource owners.
struct Api {
    std::uint32_t size, version;
    bool (*read)(Snapshot*);
};
}
