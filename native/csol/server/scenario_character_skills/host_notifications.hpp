// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../../scenario/config.hpp"
#include <memory>
class CCSOLScenarioSession;
class EHANDLE;

namespace csol::scenario::skills
{
// Call during scenario Precache, before any player ability is constructed.
bool PrecacheSkillHost(const std::shared_ptr<CCSOLScenarioSession>&, ConfigError&);
bool PrepareSkillVisual(const std::shared_ptr<CCSOLScenarioSession>&,
    const EHANDLE& owner, int subtype, ConfigError&);
bool PublishHealingDuration(CCSOLScenarioSession&, const EHANDLE& target, float duration, ConfigError&);
bool PublishAmmoDuration(CCSOLScenarioSession&, const EHANDLE& target, float duration, ConfigError&);
bool PublishInfiniteAmmo(CCSOLScenarioSession&, const EHANDLE& target, bool enabled, ConfigError&);
// This is the v40 effect sound; generic activation's character radio line is
// a distinct operation and must not replay this sound a second time.
bool PlaySkillVoice(CCSOLScenarioSession&, const EHANDLE& owner, int subtype, ConfigError&);
float ActivationUiDelay();
}
