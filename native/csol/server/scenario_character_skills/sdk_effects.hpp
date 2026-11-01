// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "host.hpp"
#include "damage.hpp"
class CCSOLScenarioRuntime;
class CBaseEntity;
template<typename T> class EntityHandle;
using EHANDLE = EntityHandle<CBaseEntity>;

namespace csol::scenario::skills
{
class SdkEffects : public HostEffects
{
public:
    virtual std::uint64_t OwnedBuffIdentity() const { return 0; }
    virtual void BindRuntime(std::shared_ptr<CCSOLScenarioRuntime> runtime) = 0;
    virtual bool IncomingDamage(ActivationState&, const EHANDLE& source, float& damage, ConfigError&) = 0;
    virtual bool BeforeProtectionDamage(ActivationState&, const EHANDLE& source, float& damage, ConfigError&) = 0;
};
bool HasSdkEffects(int subtype) noexcept;
std::shared_ptr<SdkEffects> CreateSdkEffects(
    const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session,
    const SkillTraits& traits, const CSOLScenarioActiveSkill* owner, DamageState& damage);
}
