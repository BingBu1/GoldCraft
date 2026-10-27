// SPDX-License-Identifier: GPL-3.0-or-later
// Dispatch semantics follow ReGameDLL_CS CBaseAnimating (GPL-2.0-or-later
// with its Half-Life Engine linking exception), also verified in CSO MP.
#include "precompiled.h"
#include "scenario_monster_factory.h"

#include <cmath>
#include <cstddef>

namespace
{
bool SpanWithin(int offset, int count, std::size_t stride, int length)
{
    return length >= 0 && offset >= 0 && count >= 0 && offset <= length &&
        static_cast<std::size_t>(count) <= static_cast<std::size_t>(length - offset) / stride;
}
}

bool CSOL_DispatchMonsterAnimationEvents(CSOLMonsterSpawnCall& call,
    bool (*ownerValid)(const void*), const void* ownerContext)
{
    const auto current = [&]() -> CCSOLScenarioMonster*
    {
        if (ownerValid && !ownerValid(ownerContext)) return nullptr;
        return call.Current();
    };
    auto* monster = current();
    if (!monster || !g_engfuncs.pfnGetModelPtr) return false;
    const int sequence = monster->pev->sequence;
    const int modelIndex = monster->pev->modelindex;
    auto* header = static_cast<studiohdr_t*>(GET_MODEL_PTR(monster->edict()));
    monster = current();
    if (!monster || monster->pev->sequence != sequence || monster->pev->modelindex != modelIndex)
        return false;
    // Original missing-model path returns before updating event clocks.
    if (!header) return true;
    if (header->length < static_cast<int>(sizeof(studiohdr_t)) ||
        !SpanWithin(header->seqindex, header->numseq, sizeof(mstudioseqdesc_t), header->length)) return false;
    if (sequence < 0 || sequence >= header->numseq) return false;
    const auto* description = reinterpret_cast<const mstudioseqdesc_t*>(
        reinterpret_cast<const std::byte*>(header) + header->seqindex) + sequence;
    if (description->numframes < 0 ||
        !SpanWithin(description->eventindex, description->numevents, sizeof(mstudioevent_t), header->length))
        return false;
    const float start = monster->pev->frame +
        ((monster->m_flLastEventCheck - monster->pev->animtime) * monster->m_flFrameRate) * monster->pev->framerate;
    const float end = monster->pev->frame + (0.1f * monster->m_flFrameRate) * monster->pev->framerate;
    const float nextCheck = monster->pev->animtime + 0.1f;
    if (!std::isfinite(start) || !std::isfinite(end) || !std::isfinite(nextCheck)) return false;
    monster->m_fSequenceFinished = end >= 256.0f || end <= 0.0f;
    monster->m_flLastEventCheck = nextCheck;
    int index{};
    MonsterEvent_t event{};
    while ((index = GetAnimationEvent(header, monster->pev, &event, start, end, index)) != 0)
    {
        monster->HandleAnimEvent(&event);
        monster = current();
        if (!monster || monster->pev->sequence != sequence || monster->pev->modelindex != modelIndex)
            return false;
    }
    return true;
}
