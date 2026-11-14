// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../../scenario/custom/environment_config.hpp"
#include "../../scenario/custom/environment_snapshot.hpp"
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
    void Revoke() noexcept { revoked_ = true; }
    void Restore();
private:
    bool Publish(const std::shared_ptr<CCSOLScenarioRuntime>& runtime, CBasePlayer* player);
    csol::scenario::custom::EnvironmentConfig config_;
    csol::scenario::custom::EnvironmentFade fade_;
    csol::scenario::custom::EnvironmentSnapshot snapshot_;
    goldcraft::IMapLightStyleService* lights_{};
    std::weak_ptr<CCSOLScenarioRuntime> runtime_;
    std::uint64_t challenge_{}, lightOwner_{};
    std::uint32_t revision_{};
    int message_{};
    bool prepared_{}, revoked_{}, busy_{}, known_{};
};
