// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../scenario/core/game_rules_gate.h"
#include "ehandle.h"
#include <optional>
class CBasePlayerItem;

// A null weapon leaves a native host path unchanged. Enabled calls retain the
// scenario access token through nested ReAPI/Fakemeta callbacks, including post
// hooks. Current never invokes a virtual player/weapon eligibility function.
class CSOLWeaponCall final
{
public:
    explicit CSOLWeaponCall(CBasePlayerWeapon* weapon);
    explicit CSOLWeaponCall(CBasePlayerWeapon& weapon) : CSOLWeaponCall(&weapon) {}
    bool Current(bool requireActive = true) const;
    bool Enabled() const { return m_weapon != nullptr; }
private:
    std::optional<CSOLScenarioSdkScope> m_scope;
    CBasePlayerWeapon* m_weapon{};
    CBasePlayer* m_owner{};
    EHANDLE m_weaponHandle, m_ownerHandle;
    int m_life{}, m_deadflag{};
    bool m_alive{};
    std::shared_ptr<CCSOLScenarioRuntime> m_runtime;
    std::uint64_t m_round{};
};

// Enable shared host guards only for the independently verified SKULL-3 class.
CBasePlayerWeapon* CSOL_Skull3CallWeapon(CBasePlayerItem* item);
