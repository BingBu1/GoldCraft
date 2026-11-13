// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
class CBasePlayer;
namespace goldcraft::inventory
{
// Process-local transaction state only. Never serialize this storage to a
// network packet or retain it across map/player/entity generations.
struct Snapshot
{
    std::uint64_t kind{};
    std::uint32_t bytes{};
    std::array<std::byte,512> data{};
};
class RestorableWeapon
{
public:
    virtual bool SaveInventorySnapshot(Snapshot&) const noexcept=0;
    virtual bool RestoreInventoryBinding(CBasePlayer&) noexcept=0;
    virtual bool RestoreInventorySnapshot(const Snapshot&) noexcept=0;
protected:
    ~RestorableWeapon()=default;
};
}
