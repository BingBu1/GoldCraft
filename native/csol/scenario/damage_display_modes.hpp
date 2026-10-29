// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "map_resources.hpp"

#include <memory>
#include <span>

namespace csol::scenario
{
inline constexpr auto DamageDisplayModesPath = "resource/FloatingDamage/FloatingDamageInfo.jsoo";

// Only the server's original v720 membership data. This does not grant the
// independent room option or any source, target, context or character gate.
struct DamageDisplayModes
{
    std::vector<std::int32_t> modes;
    bool Contains(std::int32_t mode) const;
};

// Read FLOATING_DMG_STYLE.MODE from the actual commented resource. Other
// visual settings remain owned by the client. Invalid input preserves output;
// empty MODE is a valid list allowing no mode, not a request for defaults.
bool ReadDamageDisplayModes(std::string_view json, DamageDisplayModes& output, ConfigError& error);
bool ReadDamageDisplayModesResource(std::span<const std::byte> encoded,
    DamageDisplayModes& output, ConfigError& error);

class DamageDisplayModeResources
{
public:
    // Load one immutable map-session snapshot from the engine filesystem.
    // No archive scan, built-in list or previous-map fallback.
    bool Load(ResourceFileApi files, ResourceError& error);
    bool Clear();
    std::shared_ptr<const DamageDisplayModes> Snapshot() const { return table_; }
private:
    std::shared_ptr<const DamageDisplayModes> table_;
    bool loading_{};
};
}
