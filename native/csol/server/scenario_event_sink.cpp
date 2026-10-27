// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_event_sink.h"
#include "scenario_monster_pool.h"
#include "scenario_messages.h"

bool CCSOLScenarioEventSink::HasSolidMonster(int type)
{
    if (m_monsters.State() != CSOLMonsterPoolState::Ready)
    {
        m_valid = false;
        return false;
    }
    return m_monsters.HasSolidMonster(type);
}

void CCSOLScenarioEventSink::BroadcastEvent(std::span<const std::uint8_t, 4> payload)
{
    if (m_valid && !m_messages.SendWaveEvent(payload)) m_valid = false;
}
