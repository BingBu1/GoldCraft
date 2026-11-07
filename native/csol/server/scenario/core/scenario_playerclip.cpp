// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_playerclip.h"
#include "scenario_playerclip_models.h"
#include "scenario_messages.h"
#include "game_rules_gate.h"
#include "triggers.h"

namespace
{
struct Busy
{
    bool& value;
    explicit Busy(bool& b) : value(b) { value = true; }
    ~Busy() { value = false; }
};
bool Send(const CCSOLScenarioMessages& messages, csol::playerclip::Operation operation,
    const csol::playerclip::Descriptor& descriptor, edict_t* recipient)
{
    std::array<std::uint8_t, 30> bytes;
    const auto size = csol::playerclip::Write({operation, descriptor}, bytes);
    return size && messages.SendPlayerClip(std::span(bytes).first(size), recipient);
}
}

void CSOL_ConfigureScenarioPlayerClip(CBaseEntity& entity)
{
    const CSOLScenarioSdkScope scope;
    if (scope.Current() && scope.Scenario() && !FStringNull(entity.pev->targetname) &&
        FStrEq(STRING(entity.pev->targetname), "playerclip"))
        entity.pev->solid = SOLID_NOT;
}

bool CCSOLScenarioPlayerClips::Current() const
{
    if (!m_access || !m_access->Allowed()) return false;
    auto* owner = CSOL_ScenarioOwnerRules(m_access.get());
    return owner && owner == g_pGameRules;
}

bool CCSOLScenarioPlayerClips::Read(Clip& clip, bool& alive) const
{
    alive = false;
    if (!Current()) return false;
    auto* entity = clip.entity.Get<CBaseEntity>();
    if (!entity || (entity->pev->flags & FL_KILLME)) return true;
    // InitTrigger's CSO flag survives later targetname changes. Classification
    // is fixed by Collect; solidity can still disable/re-enable the trigger.
    if (!dynamic_cast<CBaseTrigger*>(entity) || entity->pev->solid != SOLID_NOT)
        return true;
    // The EHANDLE preserves this edict and serial, so its index cannot change.
    // Query the engine once at collection, not for every player's PM command.
    const int index = clip.descriptor.entity ? clip.descriptor.entity : ENTINDEX(entity->edict());
    if (!Current() || clip.entity.Get<CBaseEntity>() != entity) return false;
    const int model = entity->pev->modelindex;
    if (index <= 0 || index > 65535 || model <= 0 || model > 65535) return false;
    csol::playerclip::Descriptor next{static_cast<std::uint16_t>(index), static_cast<std::uint16_t>(model)};
    for (unsigned i = 0; i != 3; ++i)
    { next.origin[i] = entity->pev->origin[i]; next.angles[i] = entity->pev->angles[i]; }
    if (!csol::playerclip::Valid(next)) return false;
    if (!clip.model || clip.descriptor.model != next.model)
    {
        clip.model = CSOL_ScenarioBrushModel(model);
        if (!Current() || clip.entity.Get<CBaseEntity>() != entity || !clip.model) return false;
    }
    clip.descriptor = next;
    alive = true;
    return true;
}

bool CCSOLScenarioPlayerClips::Collect(const CCSOLScenarioMessages& messages)
{
    if (!Current() || m_busy || m_collected || !g_engfuncs.pfnFindEntityByString) return false;
    const Busy busy(m_busy);
    if (!messages.PlayerClipsReady()) return false;
    edict_t* start{};
    while (true)
    {
        auto* edict = FIND_ENTITY_BY_STRING(start, "targetname", "playerclip");
        if (!Current()) return false;
        if (!edict) break;
        const EHANDLE found(edict);
        const int index = ENTINDEX(edict);
        if (!Current() || found.Get() != edict || edict->free) return false;
        if (!index) break;
        start = edict;
        auto* entity = CBaseEntity::Instance(edict);
        if (!entity || !dynamic_cast<CBaseTrigger*>(entity)) continue;
        if (m_count == m_clips.size()) return false;
        auto& clip = m_clips[m_count];
        clip.entity = entity;
        bool alive{};
        if (!Read(clip, alive) || !alive) return false;
        ++m_count;
    }
    m_collected = true;
    return true;
}

bool CCSOLScenarioPlayerClips::Update(const CCSOLScenarioMessages& messages,
    edict_t* recipient, bool replay)
{
    if (!Current() || m_busy || !m_collected || (replay && !recipient)) return false;
    const Busy busy(m_busy);
    const EHANDLE client(recipient);
    const auto current = [&] { return Current() && (!recipient || client.Get() == recipient); };
    if (replay && (!Send(messages, csol::playerclip::Operation::Reset, {}, recipient) || !current())) return false;
    for (std::size_t i = 0; i < m_count; ++i)
    {
        auto clip = m_clips[i];
        bool alive{};
        if (!Read(clip, alive) || !current()) return false;
        const bool changed = alive && (!m_clips[i].announced || clip.descriptor != m_clips[i].descriptor);
        if (alive && (replay || changed))
        {
            if (!Send(messages, csol::playerclip::Operation::Set, clip.descriptor, recipient) || !current()) return false;
        }
        else if (!alive && m_clips[i].announced && !replay)
        {
            if (!Send(messages, csol::playerclip::Operation::Remove, m_clips[i].descriptor, recipient) || !current()) return false;
        }
        // A single-client snapshot cannot mark a change delivered globally.
        if (!recipient)
        {
            clip.announced = alive;
            m_clips[i] = clip;
        }
    }
    return true;
}

bool CCSOLScenarioPlayerClips::Inject(playermove_t& move)
{
    if (!Current() || m_busy || !m_collected) return false;
    const Busy busy(m_busy);
    std::array<csol::playerclip::CollisionModel, csol::playerclip::Capacity> models;
    std::size_t count{};
    for (std::size_t i = 0; i < m_count; ++i)
    {
        auto clip = m_clips[i];
        bool alive{};
        if (!Read(clip, alive)) return false;
        if (alive) models[count++] = {clip.descriptor, clip.model};
    }
    return Current() && m_movement.Inject(move, std::span(models).first(count));
}
