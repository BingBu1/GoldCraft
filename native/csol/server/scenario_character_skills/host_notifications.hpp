// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../../scenario/config.hpp"
#include "../../character_skill_gate.hpp"
#include <memory>
#include <array>
#include <cstdint>
#include <span>
class CCSOLScenarioSession;
class CBaseEntity;
template<typename T> class EntityHandle;
using EHANDLE = EntityHandle<CBaseEntity>;

namespace csol::scenario::skills
{
// Call during scenario Precache, before any player ability is constructed.
bool PrecacheSkillHost(const std::shared_ptr<CCSOLScenarioSession>&, ConfigError&);
bool SkillHostReady(const CCSOLScenarioSession&);
bool PrepareSkillVisual(const std::shared_ptr<CCSOLScenarioSession>&,
    const EHANDLE& owner, int subtype, ConfigError&);
bool PublishHealingDuration(CCSOLScenarioSession&, const EHANDLE& target, float duration, ConfigError&);
bool PublishAmmoDuration(CCSOLScenarioSession&, const EHANDLE& target, float duration, ConfigError&);
bool PublishInfiniteAmmo(CCSOLScenarioSession&, const EHANDLE& target, bool enabled, ConfigError&);
bool PublishSkillGate(CCSOLScenarioSession&, const EHANDLE& target,
    const csol::skillgate::Snapshot&, ConfigError&);
bool PublishStatusDuration(CCSOLScenarioSession&, const EHANDLE& target, int status,
    float duration, std::array<std::uint8_t, 3> color, ConfigError&);
bool PlayActivationRadio(CCSOLScenarioSession&, const EHANDLE&, ConfigError&);
bool SkillMapHasFrozenWrath(CCSOLScenarioSession&, bool& present, ConfigError&);
bool PublishLoveBombExplosion(CCSOLScenarioSession&, std::array<float, 3> origin, ConfigError&);
bool PublishSkillCircle(CCSOLScenarioSession&, std::array<float, 3> origin, bool damage, ConfigError&);
bool PublishLotusSkillEffect(CCSOLScenarioSession&, std::array<float, 3> groundOrigin, float duration, ConfigError&);
bool PublishShieldSkillEffect(CCSOLScenarioSession&, const EHANDLE& owner, int subtype, ConfigError&);
bool PublishEscortSkillEffect(CCSOLScenarioSession&, const EHANDLE& owner, ConfigError&);
bool PublishStoredAttackSkillEffect(CCSOLScenarioSession&, const EHANDLE& owner, ConfigError&);
bool PublishGroupSkillEffect(CCSOLScenarioSession&, const EHANDLE& owner,
    int subtype, float duration, ConfigError&);
bool PublishSlowWallMonsterEffect(CCSOLScenarioSession&, const EHANDLE& target, ConfigError&);
bool PublishEliteDeath(CCSOLScenarioSession&, int appearance, ConfigError&);
bool PublishEliteAbilities(CCSOLScenarioSession&, int appearance, std::span<const int> abilities, ConfigError&);
bool PublishEliteHealth(CCSOLScenarioSession&, const EHANDLE& target, int appearance, float ratio, ConfigError&);
bool PublishEliteInvincible(CCSOLScenarioSession&, const EHANDLE& target, float duration, ConfigError&);
bool PublishEliteSide(CCSOLScenarioSession&, const EHANDLE& target, bool oldDoubleSide,
    float height, float duration, ConfigError&);
bool PublishClearSkillStatus(CCSOLScenarioSession&, const EHANDLE& target, int status, ConfigError&);
bool PublishDragonShieldRetirementEffect(CCSOLScenarioSession&, const EHANDLE& target, ConfigError&);
bool PublishAuraSkillEffect(CCSOLScenarioSession&, const EHANDLE& owner,
    std::uint32_t& effectId, bool enabled, ConfigError&);
bool PublishReflectionSkillEffect(CCSOLScenarioSession&, const EHANDLE& owner,
    std::uint32_t& effectId, bool enabled, ConfigError&);
bool PublishReflectionSkillPulse(CCSOLScenarioSession&, const EHANDLE& owner, ConfigError&);
bool PublishSkillScreenRGBA(CCSOLScenarioSession&, const EHANDLE& target, int status,
    float duration, std::array<std::uint8_t, 4> color, ConfigError&);
bool PublishShieldReturnCircle(CCSOLScenarioSession&, std::array<float, 3> origin, ConfigError&, bool damage = false);
bool PublishSmokeSkillStart(CCSOLScenarioSession&, const EHANDLE& owner,
    std::array<float, 3> origin, ConfigError&);
bool PublishSmokeSkillWave(CCSOLScenarioSession&, std::array<float, 3> origin, ConfigError&);
// This is the v40 effect sound; generic activation's character radio line is
// a distinct operation and must not replay this sound a second time.
bool PlaySkillVoice(CCSOLScenarioSession&, const EHANDLE& owner, int subtype, ConfigError&);
float ActivationUiDelay();
}
