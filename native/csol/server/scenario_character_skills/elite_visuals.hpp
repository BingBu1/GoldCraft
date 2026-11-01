// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../scenario_monster_elite.h"
#include <memory>

namespace csol::scenario::skills
{
bool PrecacheEliteVisuals(const std::shared_ptr<CCSOLScenarioSession>&, ConfigError&);
void ResetEliteVisuals(const CCSOLScenarioSession&);
bool PublishEliteEntityVisual(CCSOLScenarioSession&, const EHANDLE&, CSOLEliteEntityVisual,
    const Vector& offset, float duration, float radius, ConfigError&);
bool PublishEliteAreaVisual(CSOLMonsterSpawnCall&, int ability, ConfigError&);
}
