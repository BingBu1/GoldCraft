// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "weapon_call_guard.h"
#include "wpn_skull3.h"
#include "../scenario/core/scenario_runtime.h"
#include "../scenario/core/scenario_session.h"

CSOLWeaponCall::CSOLWeaponCall(CBasePlayerWeapon* weapon) : m_weapon(weapon)
{
    if (!weapon) return;
    m_scope.emplace();
    if (!m_scope->Current()) return;
    m_owner = weapon->m_pPlayer;
    m_weaponHandle = weapon;
    m_ownerHandle = m_owner;
    if (m_owner)
    {
        m_life = m_owner->m_iNumSpawns;
        m_deadflag = m_owner->pev->deadflag;
        m_alive = m_deadflag == DEAD_NO && m_owner->pev->health > 0;
    }
    if (m_scope->Scenario())
    {
        m_runtime = CSOL_ScenarioRuntimeForAccess(m_scope->EntityAccess().get());
        if (m_runtime && m_runtime->session)
            m_round = m_runtime->session->Map().RoundGeneration();
    }
}

bool CSOLWeaponCall::Current(bool requireActive) const
{
    if (!m_weapon) return true;
    if (!m_scope || !m_scope->Current()) return false;
    if (m_scope->Scenario() && (!m_runtime || m_runtime->Closing() || !m_runtime->session ||
        m_runtime->session->Map().RoundGeneration() != m_round)) return false;
    return m_owner && m_weaponHandle.Get<CBaseEntity>() == m_weapon &&
        m_ownerHandle.Get<CBaseEntity>() == m_owner &&
        !(m_weapon->pev->flags & FL_KILLME) && !(m_owner->pev->flags & (FL_KILLME | FL_DORMANT)) &&
        m_weapon->m_pPlayer == m_owner && (!requireActive || m_owner->m_pActiveItem == m_weapon) &&
        m_owner->m_iNumSpawns == m_life && !m_owner->has_disconnected &&
        m_owner->pev->deadflag == m_deadflag && (!m_alive || m_owner->pev->health > 0);
}

CBasePlayerWeapon* CSOL_Skull3CallWeapon(CBasePlayerItem* item)
{
    auto* weapon = dynamic_cast<CBasePlayerWeapon*>(item);
    return weapon && CSOL_IsSkull3(*weapon) ? weapon : nullptr;
}
