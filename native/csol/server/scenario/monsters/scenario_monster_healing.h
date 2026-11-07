// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

class CSOLMonsterSpawnCall;
class CCSOLMonsterPool;
class CCSOLScenarioMessages;
struct CSOLOrdinaryMonsterState;
namespace csol::scenario { struct ConfigError; }

// The caller retains the owner and all managers for the entire invocation.
// This check is identity-only and must not invoke engine/plugin callbacks.
struct CSOLMonsterHealingGuard
{
    bool (*current)(const void*){};
    const void* context{};
    bool Current() const { return current && current(context); }
};

bool CSOL_UseMonsterHealing(CSOLMonsterSpawnCall& call, CCSOLMonsterPool& pool,
    const CCSOLScenarioMessages& messages, CSOLOrdinaryMonsterState& state,
    CSOLMonsterHealingGuard owner, csol::scenario::ConfigError& error);
