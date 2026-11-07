// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_wall_upgrade.h"
#include "game_rules_gate.h"

#include <cmath>

LINK_ENTITY_TO_CLASS(zs_wallupmachine, CCSOLScenarioWallUpgradeMachine, CCSEntity)

// Original113924E0: an otherwise unchanged CBaseEntity. The manager scans
// its classname; an unknown-entity discard would silently enable upgrades.
class CCSOLScenarioWallUpgradeDisabled final : public CBaseEntity {};
LINK_ENTITY_TO_CLASS(zbsbreak_dontupgrade, CCSOLScenarioWallUpgradeDisabled, CCSEntity)

class CSOLWallMachineCall
{
public:
    explicit CSOLWallMachineCall(CCSOLScenarioWallUpgradeMachine& machine)
        : m_machine(&machine), m_handle(&machine), m_access(machine.m_access),
          m_lifetime(machine.m_lifetime), m_generation(*machine.m_lifetime) {}
    bool Current() const
    {
        return m_access && m_access->Allowed() && m_lifetime &&
            *m_lifetime == m_generation && m_handle.Get<CBaseEntity>() == m_machine &&
            !(m_machine->pev->flags & FL_KILLME);
    }
private:
    CCSOLScenarioWallUpgradeMachine* m_machine;
    EHANDLE m_handle;
    std::shared_ptr<const CSOLScenarioEntityAccess> m_access;
    std::shared_ptr<std::uint64_t> m_lifetime;
    std::uint64_t m_generation;
};

void CCSOLScenarioWallUpgradeMachine::Precache()
{
    if (!m_access)
    {
        CSOLScenarioSdkScope scope;
        if (!scope.Scenario() || !scope.Current()) return;
        m_access = scope.EntityAccess();
    }
    const CSOLWallMachineCall call(*this);
    if (!call.Current()) return;
    PRECACHE_MODEL(const_cast<char*>(csol::wall_upgrade::Model));
    if (!call.Current()) return;
    PRECACHE_MODEL(const_cast<char*>(csol::wall_upgrade::Sprite));
}

void CCSOLScenarioWallUpgradeMachine::Spawn()
{
    CSOLScenarioSdkScope scope;
    if (!scope.Scenario() || !scope.Current()) return;
    m_access = scope.EntityAccess();
    const CSOLWallMachineCall call(*this);
    m_spawned = false;
    Precache();
    if (!call.Current()) return;
    pev->movetype = MOVETYPE_TOSS;
    pev->solid = SOLID_BBOX;
    SET_MODEL(ENT(pev), csol::wall_upgrade::Model);
    if (!call.Current()) return;
    Vector mins, maxs;
    auto* header = GET_MODEL_PTR(edict());
    if (!call.Current()) return;
    const bool bounds = ::ExtractBbox(header, 0, mins, maxs) != 0;
    if (!call.Current()) return;
    if (!bounds) { pev->solid = SOLID_NOT; return; }
    for (int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(mins[axis]) || !std::isfinite(maxs[axis]) || mins[axis] > maxs[axis])
        { pev->solid = SOLID_NOT; return; }
    UTIL_SetSize(pev, mins, maxs);
    if (!call.Current()) return;
    SetUse(&CCSOLScenarioWallUpgradeMachine::UseWallUpgrade);
    SetThink(&CCSOLScenarioWallUpgradeMachine::MachineThink);
    pev->nextthink = gpGlobals->time + csol::wall_upgrade::FirstThink;
    m_spawned = true;
}

bool CCSOLScenarioWallUpgradeMachine::Bind(std::shared_ptr<CCSOLScenarioWallUpgrade> state)
{
    const CSOLWallMachineCall call(*this);
    if (!call.Current() || !m_spawned || !state || !state->Current() ||
        state->EntityAccess() != m_access) return false;
    m_state = std::move(state);
    return true;
}

void CCSOLScenarioWallUpgradeMachine::MachineThink()
{
    const CSOLWallMachineCall call(*this);
    const auto state = m_state;
    if (!call.Current() || !state || !state->Current()) return;
    state->ShowIndicator(EHANDLE(this));
    if (!call.Current() || !state->Current()) return;
    pev->nextthink = gpGlobals->time + csol::wall_upgrade::ThinkInterval;
}

void CCSOLScenarioWallUpgradeMachine::UseWallUpgrade(CBaseEntity* activator,
    CBaseEntity*, USE_TYPE, float)
{
    const CSOLWallMachineCall call(*this);
    const auto state = m_state;
    if (!call.Current() || !state || !state->Current() || !activator) return;
    const EHANDLE player(activator);
    const bool isPlayer = activator->IsPlayer() != FALSE;
    if (!call.Current() || !isPlayer || player.Get<CBaseEntity>() != activator) return;
    state->Use(player);
    if (!call.Current() || !state->Current()) return;
    if (state->Level() == csol::wall_upgrade::MaximumLevel)
        state->RemoveIndicator(EHANDLE(this));
}

bool CCSOLScenarioWallUpgradeMachine::StatusLevel(int& level) const
{
    if (!m_access || !m_access->Allowed() || !m_state || !m_state->Current() ||
        !m_spawned || (pev->flags & FL_KILLME)) return false;
    level = m_state->Level();
    return true;
}

void CCSOLScenarioWallUpgradeMachine::OnDestroy()
{
    ++*m_lifetime;
    m_state.reset();
    m_spawned = false;
    CBaseAnimating::OnDestroy();
}
