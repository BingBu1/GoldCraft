// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "damage_notice.h"
#include "damage_numbers.h"
#include "scenario/core/scenario_runtime.h"
#include "scenario/core/scenario_session.h"
#include "scenario/core/scenario_messages.h"
#include "../scenario/core/damage_display_modes.hpp"

namespace
{
cvar_t displayOption{"csol_damage_numbers", "1", FCVAR_SERVER};
bool registered{};
std::uint64_t mapEpoch{};
int messageId{};
std::shared_ptr<const csol::scenario::DamageDisplayModes> ordinaryModes;
}

void CSOL_InitDamageNumbers()
{
    if (!registered) { CVAR_REGISTER(&displayOption); registered = true; }
}

void CSOL_ResetDamageNumbers()
{
    ++mapEpoch;
    messageId = 0;
    ordinaryModes.reset();
}

void CSOL_ActivateDamageNumbers()
{
    CSOL_ResetDamageNumbers();
    const auto epoch = mapEpoch;
    CSOL_InitDamageNumbers();
    CSOLScenarioSdkScope scope;
    if (!scope.Current() || scope.Scenario()) return;
    // Ordinary rules remain the base mode0 contract even when an AMXX mod
    // supplies its own gameplay. Local mode selector1 is not CSO mode1.
    csol::scenario::DamageDisplayModeResources resources;
    csol::scenario::ResourceError error;
    const bool loaded = resources.Load({g_engfuncs.pfnLoadFileForMe, g_engfuncs.pfnFreeFile}, error);
    if (mapEpoch != epoch || !scope.Current()) return;
    if (!loaded)
    {
        ALERT(at_console, "CSOL damage numbers: %s\n", error.detail.message.c_str());
        return;
    }
    if (!g_engfuncs.pfnRegUserMsg) return;
    const int id = REG_USER_MSG("MPToCL", -1);
    if (mapEpoch != epoch || !scope.Current() || id < 64 || id > 255) return;
    ordinaryModes = resources.Snapshot();
    messageId = id;
}

CSOLAttackNumberCall::CSOLAttackNumberCall(CBaseEntity& target, entvars_t* attacker,
    bool (*ownerCurrent)(const void*), const void* owner)
    : m_targetIdentity(&target), m_epoch(mapEpoch), m_message(messageId),
      m_ownerCurrent(ownerCurrent), m_owner(owner)
{
    if (!m_scope.Current() || (m_ownerCurrent && !m_ownerCurrent(m_owner))) return;
    if (m_scope.Scenario())
    {
        m_runtime = CSOL_ScenarioRuntimeForAccess(m_scope.EntityAccess().get());
        m_session = m_runtime ? m_runtime->session : nullptr;
        if (!m_runtime || m_runtime->Closing() || !m_session) return;
        m_generation = m_runtime->Generation();
        m_round = m_session->Map().RoundGeneration();
        m_modes = m_session->DamageDisplayModes();
    }
    else m_modes = ordinaryModes;
    m_target = &target;
    m_sourceIdentity = attacker ? CBaseEntity::Instance(attacker) : nullptr;
    m_source = m_sourceIdentity;
    // RTTI here captures host identity only. The original virtual source and
    // Bot gates still run at Notify, after the display option/mode decision.
    m_targetPlayer = dynamic_cast<CBasePlayer*>(&target);
    m_sourcePlayer = dynamic_cast<CBasePlayer*>(m_sourceIdentity);
    if (m_targetPlayer) m_targetLife = m_targetPlayer->m_iNumSpawns;
    if (m_sourcePlayer) m_sourceLife = m_sourcePlayer->m_iNumSpawns;
}

bool CSOLAttackNumberCall::Current() const
{
    if (mapEpoch != m_epoch || !m_scope.Current() ||
        (m_ownerCurrent && !m_ownerCurrent(m_owner))) return false;
    if (m_scope.Scenario() && (!m_runtime || m_runtime->Closing() ||
        m_runtime->Generation() != m_generation || !m_session || m_runtime->session != m_session ||
        m_session->Map().RoundGeneration() != m_round)) return false;
    if (m_target.Get<CBaseEntity>() != m_targetIdentity ||
        m_source.Get<CBaseEntity>() != m_sourceIdentity) return false;
    return (!m_targetPlayer || m_targetPlayer->m_iNumSpawns == m_targetLife) &&
        (!m_sourcePlayer || (m_sourcePlayer->m_iNumSpawns == m_sourceLife && !m_sourcePlayer->has_disconnected));
}

bool CSOLAttackNumberCall::Notify(float amount, int bits, int hitgroup, int effect, bool suppressed) const
{
    if (!Current()) return false;
    if (m_runtime ? !m_runtime->room.floatingDamageDisplay : !registered || displayOption.value == 0)
        return true;
    if (!m_modes || !m_modes->Contains(m_runtime ? 15 : 0)) return true;
    if (!m_sourceIdentity || !m_sourcePlayer) return true;
    const auto validEntity = [&](CBaseEntity* entity)
    {
        if (!Current() || !entity || !entity->pev || !entity->pev->pContainingEntity ||
            !g_engfuncs.pfnEntOffsetOfPEntity) return false;
        const int offset = OFFSET(entity->pev->pContainingEntity);
        return Current() && offset && !(entity->pev->flags & FL_DORMANT);
    };
    if (!validEntity(m_sourceIdentity)) return Current();
    const bool player = m_sourceIdentity->IsPlayer() != FALSE;
    if (!Current()) return false;
    if (!player) return true;
    const bool bot = m_sourcePlayer->IsBot() != FALSE;
    if (!Current()) return false;
    if (bot) return true;
    const auto value = CSOLTruncateDamageNumber(amount);
    if (!value) return true;
    const bool senderBot = m_sourcePlayer->IsBot() != FALSE;
    if (!Current()) return false;
    if (senderBot) return true;
    // v3EC: the scenario producer here only accepts nonplayers. Its player
    // recovery producer retains the separate special-character rule.
    if (!validEntity(m_targetIdentity) || !validEntity(m_sourceIdentity)) return Current();
    if (m_runtime && m_targetPlayer) return true;
    if (suppressed || !g_engfuncs.pfnIndexOfEdict) return true;
    const int index = ENTINDEX(m_targetIdentity->edict());
    if (!Current()) return false;
    if (index <= 0 || index > 32767) return true;
    const std::uint8_t style = hitgroup == HITGROUP_HEAD ? 4 :
        static_cast<std::uint32_t>(bits) == 0x08000000u ? 1 : 0;
    const auto flags = static_cast<std::uint8_t>((static_cast<std::uint32_t>(bits) >> 31) |
        (effect == 8 ? 2u : 0u));
    const CSOLDamageNumber message{static_cast<std::int16_t>(index), style, flags, *value};
    auto* recipient = m_sourcePlayer->edict();
    if (m_session) m_session->Messages().SendAttackNumber(recipient, message);
    else CSOLWriteGuardedDamageNumber(m_message, recipient, message);
    return Current();
}

bool CSOL_NotifyPlayerAttackNumber(CBasePlayer& target, entvars_t* attacker, float amount, int bits)
{
    if (CSOL_UsesScenarioRules()) return true;
    const CSOLAttackNumberCall call(target, attacker);
    return call.Notify(amount, bits, target.m_LastHitGroup, 0, false);
}
