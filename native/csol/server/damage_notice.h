// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "scenario/core/game_rules_gate.h"
#include "ehandle.h"
#include <cstdint>
#include <memory>

class CCSOLScenarioSession;
namespace csol::scenario { struct DamageDisplayModes; }

void CSOL_InitDamageNumbers();
void CSOL_ActivateDamageNumbers();
void CSOL_ResetDamageNumbers();

// One authoritative damage invocation, with identity retained across its
// callbacks. Nonplayer owners supply their additional pool/wall generation.
class CSOLAttackNumberCall final
{
public:
    CSOLAttackNumberCall(CBaseEntity& target, entvars_s* attacker,
        bool (*ownerCurrent)(const void*) = nullptr, const void* owner = nullptr);
    bool Current() const;
    bool Notify(float amount, int bits, int hitgroup, int effect, bool suppressed) const;
private:
    CSOLScenarioSdkScope m_scope;
    EHANDLE m_target, m_source;
    CBaseEntity* m_targetIdentity{};
    CBaseEntity* m_sourceIdentity{};
    CBasePlayer* m_targetPlayer{};
    CBasePlayer* m_sourcePlayer{};
    int m_targetLife{}, m_sourceLife{};
    std::shared_ptr<CCSOLScenarioRuntime> m_runtime;
    std::shared_ptr<CCSOLScenarioSession> m_session;
    std::shared_ptr<const csol::scenario::DamageDisplayModes> m_modes;
    std::uint64_t m_epoch{}, m_generation{}, m_round{};
    int m_message{};
    bool (*m_ownerCurrent)(const void*){};
    const void* m_owner{};
};

// Ordinary ReGameDLL player path: after armor/acceptance, before health.
// Mode15 owns its separate v1B8 skill callback and does not use this producer.
bool CSOL_NotifyPlayerAttackNumber(CBasePlayer& target, entvars_s* attacker,
    float amount, int bits);
