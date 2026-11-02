// SPDX-License-Identifier: GPL-3.0-or-later
// This component test retains real SDK layouts and wall/account methods.
// GameRules ownership and Minecraft mining are explicit controlled services;
// the complete Gate restart transaction is NOT linked by this executable.
#include "fixture.hpp"
#include "../../native/csol/server/game_rules_gate.h"
#include "../../native/csol/server/scenario_grenade_blockers.h"
#include "../../native/csol/server/scenario_drops.h"
#include "../../native/server/bridge.h"
#include <cstdlib>

struct CSOLScenarioRulesRecord {};
namespace wall_test
{
namespace { std::shared_ptr<CSOLScenarioEntityAccess> access; }
std::shared_ptr<CSOLScenarioEntityAccess> NewDifficultyMap()
{
    access = std::make_shared<CSOLScenarioEntityAccess>();
    return access;
}
}

CSOLScenarioSdkScope::CSOLScenarioSdkScope() { Bind(nullptr); }
CSOLScenarioSdkScope::CSOLScenarioSdkScope(CGameRules* rules) { Bind(rules); }
CSOLScenarioSdkScope::~CSOLScenarioSdkScope() = default;
void CSOLScenarioSdkScope::Bind(CGameRules* rules)
{
    m_rules = rules;
    m_access = wall_test::access;
    if (m_access) m_record = std::make_shared<CSOLScenarioRulesRecord>();
}
bool CSOLScenarioSdkScope::Current() const
{ return m_access == wall_test::access && (!m_access || m_access->Allowed()); }
void CSOLScenarioSdkScope::RequestClose()
{ if (wall_test::access) wall_test::access->Revoke(); }
CGameRules* CSOL_ScenarioOwnerRules(const CSOLScenarioEntityAccess*) { return nullptr; }
bool CSOL_UsesScenarioRules() { return bool(wall_test::access); }
bool GoldCraft_MapMiningKeepEntity(edict_t*) { return false; }
bool CSOL_ScenarioItemRemoving(CBaseEntity*)
{
    std::cerr << "UNEXPECTED DIFFICULTY EDGE: item removal\n";
    std::abort();
}
void CSOL_SetBreakableGrenadeBlock(CBaseEntity&, bool)
{
    std::cerr << "UNEXPECTED DIFFICULTY EDGE: grenade blocker configuration\n";
    std::abort();
}
