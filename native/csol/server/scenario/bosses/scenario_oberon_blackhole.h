// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include "../monsters/scenario_monster_resources.h"

class CCSOLScenarioSession;
class CSOLMonsterSpawnCall;
namespace csol::scenario { struct ConfigError; }
struct CSOLOberonBlackHoleState;

// Original Oberon type 6: an independent, visible temporary entity. It keeps
// pulling after its creator dies, but cannot survive its map/round authority.
class CCSOLOberonBlackHole final : public CBaseAnimating
{
public:
    void Spawn() override;
    void OnDestroy() override;
    void PullThink();
    static bool Create(const std::shared_ptr<CCSOLScenarioSession>& session,
        CSOLMonsterSpawnCall& creator, float delay, float strength,
        csol::scenario::ConfigError& error);

private:
    friend struct CSOLOberonBlackHoleState;
    std::shared_ptr<CSOLOberonBlackHoleState> m_state;
};
