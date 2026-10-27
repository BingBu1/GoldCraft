// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_player_scan.h"

#include <cmath>
#include <cstdint>

void CCSOLTriggerPlayerScan::BeginMap()
{
    if (!m_scanning) m_nextPlayer = 0;
}

csol::scenario::TriggerResult CCSOLTriggerPlayerScan::Update(bool enabled,
    csol::scenario::TriggerProgram& program, float elapsed, int aliveCtCount,
    csol::scenario::TriggerActions& actions)
{
    if (m_scanning || !gpGlobals || !std::isfinite(elapsed)) return {};
    if (!enabled) return {true, 0, false};
    const int clientCount = gpGlobals->maxClients;
    if (clientCount <= 0) return {};
    // A changed engine capacity cannot leave an out-of-range client lookup.
    if (m_nextPlayer >= clientCount) m_nextPlayer = 0;
    if (m_nextPlayer == 0) program.BeginPlayerScanCycle();

    struct ScanScope
    {
        bool& scanning;
        explicit ScanScope(bool& value) : scanning(value) { scanning = true; }
        ~ScanScope() { scanning = false; }
    } scope(m_scanning);

    csol::scenario::TriggerResult result{true, 0, false};
    edict_t* edict = INDEXENT(m_nextPlayer + 1);
    if (edict && !edict->free)
    {
        auto* player = dynamic_cast<CBasePlayer*>(CBaseEntity::Instance(edict));
        // Original MP uses exact flags equality, not IsDormant's bit test.
        // Its IsAlive is deadflag == 0 && health > 0. No team filter occurs here.
        if (player && player->pev &&
            static_cast<std::uint32_t>(player->pev->flags) != 0x80000000u &&
            player->pev->deadflag == DEAD_NO && player->pev->health > 0)
        {
            const auto& origin = player->pev->origin;
            result = program.CheckPlayer({origin.x, origin.y, origin.z}, elapsed, aliveCtCount, actions);
        }
    }
    // Advance even for an empty/dead slot. Clear the goal count only when the
    // next full cycle starts, including when its first player slot is empty.
    m_nextPlayer = (m_nextPlayer + 1) % clientCount;
    return result;
}
