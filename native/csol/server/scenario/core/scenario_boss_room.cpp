// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_boss_room.h"
#include "scenario_entity_lookup.h"

#include <bit>
#include <cstdlib>
#include <cstring>
#include <utility>

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

bool CCSOLBossRoomCache::ControllerConstructed(std::shared_ptr<const CSOLScenarioEntityAccess> access)
{
    if (!access || !access->Allowed()) return false;
    m_access = std::move(access);
    ++m_generation;
    m_id = -1;
    const float initial = std::bit_cast<float>(std::uint32_t{0x47c34f80});
    m_minimum = Vector(initial, initial, initial);
    return true;
}

CSOLBossRoomResult CCSOLBossRoomCache::Bounds(int id, Vector& minimum, Vector& maximum)
{
    const auto access = m_access;
    if (!access || !access->Allowed() || m_busy) return CSOLBossRoomResult::Unavailable;
    const auto generation = m_generation;
    struct Operation
    {
        bool& busy;
        explicit Operation(bool& value) : busy(value) { busy = true; }
        ~Operation() { busy = false; }
    } operation(m_busy);
    if (m_id == -1 || m_id != id)
    {
        CBaseEntity* cursor{};
        while (true)
        {
            const auto next = CSOL_LookupScenarioEntity(cursor, "boss_room", access);
            if (!access->Allowed() || m_generation != generation || !next.valid)
                return CSOLBossRoomResult::Unavailable;
            cursor = next.entity;
            if (!cursor) break;
            auto* room = dynamic_cast<CCSOLBossRoom*>(cursor);
            if (!room) return CSOLBossRoomResult::Unavailable;
            if (room->RoomId() != id) continue;
            m_id = id;
            m_minimum = room->pev->absmin;
            m_maximum = room->pev->absmax;
            break;
        }
    }
    // Preserve the original id comparison, including a requested id of -1.
    if (m_id != id) return CSOLBossRoomResult::Missing;
    minimum = m_minimum;
    maximum = m_maximum;
    return CSOLBossRoomResult::Found;
}
