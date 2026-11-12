// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_boss_room.h"

#include <cstdlib>
#include <cstring>

LINK_ENTITY_TO_CLASS(boss_room, CCSOLBossRoom, CCSEntity)

void CCSOLBossRoom::Spawn()
{
    // 108F2260: retain model bounds without movement, collision or drawing.
    pev->movetype = MOVETYPE_NONE;
    pev->solid = SOLID_NOT;
    const EHANDLE identity(this);
    SET_MODEL(edict(), STRING(pev->model));
    // Host adaptation: SetModel may call a plugin which retires this entity.
    if (identity.Get<CBaseEntity>() != this) return;
    pev->effects |= EF_NODRAW;
}

void CCSOLBossRoom::KeyValue(KeyValueData* data)
{
    // 107DCE30: unknown keys remain unhandled, without base-class dispatch.
    if (!std::strcmp(data->szKeyName, "id"))
    {
        m_id = std::atoi(data->szValue);
        data->fHandled = TRUE;
    }
    else data->fHandled = FALSE;
}
