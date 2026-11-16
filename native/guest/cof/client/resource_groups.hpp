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
struct FirearmResourceGroup
{
    Firearm identity;
    std::span<const resources::LocalSoundResource> localSounds;
};
inline constexpr std::array FirearmResourceGroups{
    FirearmResourceGroup{Firearm::P345,resources::P345AudioResources},
    FirearmResourceGroup{Firearm::Glock,resources::GlockAudioResources},
    FirearmResourceGroup{Firearm::Revolver,resources::RevolverAudioResources},
    FirearmResourceGroup{Firearm::VP70,resources::VP70AudioResources},
    FirearmResourceGroup{Firearm::TMP,resources::TMPAudioResources},
    FirearmResourceGroup{Firearm::MP5,resources::MP5AudioResources},
    FirearmResourceGroup{Firearm::M16,resources::M16AudioResources},
    FirearmResourceGroup{Firearm::G43,resources::G43AudioResources}
};
inline constexpr std::size_t FirearmLocalSampleCount=[] {
    std::size_t count{};
    for(const auto& group:FirearmResourceGroups)count+=group.localSounds.size();
    return count;
}();
inline constexpr std::size_t PistolLocalSampleCount=[] {
    std::size_t count{};
    for(const auto& group:PistolResourceGroups)count+=group.localSounds.size();
    return count;
}();
}
