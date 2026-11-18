// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../../scenario/custom/environment_config.hpp"
#include "../../scenario/custom/environment_snapshot.hpp"
#include "../../scenario/custom/night_lighting.hpp"
#include "ehandle.h"
#include <array>
#include <memory>

class CBasePlayer;
class CCSOLScenarioRuntime;
namespace goldcraft { class IMapLightStyleService; }

class CCSOLChallengeEnvironment final
{
public:
    bool Precache(const csol::scenario::custom::Snapshot& settings);
    bool Update(const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
        std::uint64_t challenge, csol::scenario::custom::Phase phase, float now);
    bool Replay(const std::shared_ptr<CCSOLScenarioRuntime>& runtime, CBasePlayer& player);
    bool NightFlashlightsAvailable() const noexcept;
    bool HoldsNightFlashlight(CBasePlayer& player) const;
    void SelectNightFlashlight(CBasePlayer& player);
    void CheckNightFlashlight(CBasePlayer& player);
    void ReleaseNightFlashlight(CBasePlayer& player, bool holster = false);
    void Revoke() noexcept;
    void Restore();
private:
    void ClearNightFlashlights(bool holster);
    bool Publish(const std::shared_ptr<CCSOLScenarioRuntime>& runtime, CBasePlayer* player);
    bool PublishNight(const std::shared_ptr<CCSOLScenarioRuntime>& runtime, CBasePlayer* player);
    csol::scenario::custom::EnvironmentConfig config_;
    csol::scenario::custom::EnvironmentFade fade_;
    csol::scenario::custom::EnvironmentSnapshot snapshot_;
    csol::scenario::custom::NightLightingSnapshot night_;
    goldcraft::IMapLightStyleService* lights_{};
    std::weak_ptr<CCSOLScenarioRuntime> runtime_;
    std::array<EHANDLE, 32> nightFlashlights_{};
    string_t flashlightView_{}, flashlightPlayer_{};
    std::uint64_t challenge_{}, lightOwner_{};
    std::uint32_t revision_{};
    int message_{}, nightMessage_{};
    bool nightKnown_{};
    bool prepared_{}, revoked_{}, busy_{}, known_{};
};
