// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

class CCSOLScenarioRuntime;
class CSOLMonsterSpawnCall;
struct CSOLMonsterDeathContext;
namespace csol::scenario { struct ConfigError; }

// Elite ability10 uses CMonster.v50 with self as source and damage bits zero.
// This implements that original recovery path, including its notification
// eligibility gates. Ordinary healer skill2 has a different direct-HP path.
bool CSOL_RecoverMonsterSelf(const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    CSOLMonsterSpawnCall& call, float amount, const CSOLMonsterDeathContext& context,
    csol::scenario::ConfigError& error);
