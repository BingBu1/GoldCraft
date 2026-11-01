// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include "scenario_monster_resources.h"

class CCSOLScenarioSession;
class CSOLMonsterSpawnCall;
namespace csol::scenario { struct ConfigError; }
struct CSOLOberonKnifeEffectState;

class CCSOLOberonKnifeEffect final : public CBaseAnimating
{
public:
    void Spawn() override;
    void OnDestroy() override;
    void FadeThink();

private:
    friend struct CSOLOberonKnifeEffectState;
    friend class CCSOLOberonKnifeEffects;
    std::shared_ptr<CSOLOberonKnifeEffectState> m_state;
};

// One reusable knifeeffect per scenario manager, not one entity per attack.
// The owner must be shared and initialized after model precache/activation.
// Round reset hides the entity; shutdown retires it before map access ends.
class CCSOLOberonKnifeEffects : public std::enable_shared_from_this<CCSOLOberonKnifeEffects>
{
public:
    ~CCSOLOberonKnifeEffects();
    bool Initialize(const std::shared_ptr<CCSOLScenarioSession>& session,
        csol::scenario::ConfigError& error);
    bool Play(CSOLMonsterSpawnCall& creator, int skill,
        csol::scenario::ConfigError& error);
    void Hide();
    void Shutdown();

private:
    std::shared_ptr<CSOLOberonKnifeEffectState> m_state;
};
