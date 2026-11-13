// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <span>
#include <array>

class CCSOLScenarioRuntime;
class CSOLScenarioEntityAccess;
namespace csol::scenario { struct ConfigError; }

// MP11546200 plus six verified record+18 models and id0's original four-word
// SharedData pool. Read-only existence/download root for selected Teddy maps.
inline constexpr std::array CSOLTeddyWeaponRewardModels{
    "models/supplybox4.mdl","models/w_janus1.mdl","models/w_janusmk5.mdl",
    "models/w_skull5.mdl","models/w_skull3.mdl","models/w_balrog7.mdl",
    "models/w_balrog11.mdl","models/w_mg3.mdl","models/w_as50.mdl",
    "models/w_dbarrel.mdl","models/w_uts15.mdl"};

// Cold, selected-Teddy map preparation only. Resource readiness does not mean
// the three absent Janus1/JanusMK5/Balrog7 weapon implementations exist.
bool CSOL_PrecacheTeddyWeaponRewards(
    const std::shared_ptr<const CSOLScenarioEntityAccess>& access,
    csol::scenario::ConfigError& error);
bool CSOL_TeddyWeaponRewardsAvailable();
const char* CSOL_TeddyWeaponRewardsUnavailableReason();

bool CSOL_CreateTeddyWeaponReward(const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    const Vector& origin,int originalWeaponId,csol::scenario::ConfigError& error);
// MP109971F7 creates/Spawns id0 before10997208's RandomLong and11545860
// setter. Never choose the final reward before the real creation callbacks.
bool CSOL_CreateTeddyRandomWeaponReward(const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    const Vector& origin,std::span<const int,6> weapons,csol::scenario::ConfigError& error);

struct CSOLTeddyWeaponRewardState;
class CCSOLTeddyWeaponReward final : public CBaseEntity
{
public:
    void Spawn() override;
    void Touch(CBaseEntity* other) override;
    void OnDestroy() override;
    // Original11545860 setter; only the six proven reward identities belong
    // to this host class. This does not manufacture an absent weapon body.
    bool SetOriginalWeapon(int originalWeaponId,csol::scenario::ConfigError& error);
private:
    friend struct CSOLTeddyWeaponRewardState;
    friend bool CSOL_CreateTeddyWeaponReward(const std::shared_ptr<CCSOLScenarioRuntime>&,
        const Vector&,int,csol::scenario::ConfigError&);
    friend bool CSOL_CreateTeddyRandomWeaponReward(const std::shared_ptr<CCSOLScenarioRuntime>&,
        const Vector&,std::span<const int,6>,csol::scenario::ConfigError&);
    std::shared_ptr<CSOLTeddyWeaponRewardState> m_state;
};
