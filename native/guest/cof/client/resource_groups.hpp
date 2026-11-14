// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../catalog.hpp"
#include "../resource_catalog.hpp"
#include <span>

namespace guest::cof::client
{
struct PistolResourceGroup
{
    Pistol identity;
    std::span<const resources::LocalSoundResource> localSounds;
};
// Cold resource candidates, never whole-weapon admission. Keep the accepted
// P345/Glock audio preparation order; append only verified resource consumers.
inline constexpr std::array PistolResourceGroups{
    PistolResourceGroup{Pistol::P345,resources::P345AudioResources},
    PistolResourceGroup{Pistol::Glock,resources::GlockAudioResources},
    PistolResourceGroup{Pistol::Revolver,resources::RevolverAudioResources},
    PistolResourceGroup{Pistol::VP70,resources::VP70AudioResources}
};
inline constexpr std::size_t PistolLocalSampleCount=[] {
    std::size_t count{};
    for(const auto& group:PistolResourceGroups)count+=group.localSounds.size();
    return count;
}();
}
