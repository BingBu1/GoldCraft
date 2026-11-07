// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <map>

namespace csol::scenario
{
constexpr std::int16_t TriggerKey(int id)
{
    return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(id));
}

struct TriggerVolume
{
    std::array<float, 3> mins{};
    std::array<float, 3> maxs{};
    int id{};
    std::size_t entityIndex{};
    bool Contains(const std::array<float, 3>& origin) const
    {
        // MP tests the player's origin against an open box, not hull overlap.
        for (std::size_t axis = 0; axis < origin.size(); ++axis)
            if (!(mins[axis] < origin[axis] && origin[axis] < maxs[axis])) return false;
        return true;
    }
};

// BSP geometry only. The original manager subsequently binds trigger rules,
// timers and wave lists; map ordering here is not an event execution order.
class TriggerVolumes
{
public:
    void Register(const TriggerVolume& volume) { volumes_[TriggerKey(volume.id)] = volume; }
    const TriggerVolume* Find(int id) const
    {
        const auto found = volumes_.find(TriggerKey(id));
        return found == volumes_.end() ? nullptr : &found->second;
    }
    const auto& Volumes() const { return volumes_; }
    void Clear() { volumes_.clear(); }

private:
    std::map<std::int16_t, TriggerVolume> volumes_;
};
}
