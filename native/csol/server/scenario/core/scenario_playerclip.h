// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ehandle.h"
#include "pm_defs.h"
#include "scenario_entity_access.h"
#include "../../../scenario/core/playerclip_movement.hpp"
#include <memory>

class CBaseEntity;
class CCSOLScenarioMessages;

// Call inside CBaseTrigger::InitTrigger after its SOLID_TRIGGER assignment,
// before SET_MODEL links the entity. Other modes and targetnames are unchanged.
void CSOL_ConfigureScenarioPlayerClip(CBaseEntity& entity);

class CCSOLScenarioPlayerClips
{
public:
    explicit CCSOLScenarioPlayerClips(std::shared_ptr<const CSOLScenarioEntityAccess> access) :
        m_access(std::move(access)) {}
    // ServerActivate, after map entities exist. No edict ownership is taken.
    bool Collect(const CCSOLScenarioMessages& messages);
    // Refresh changed or removed clip descriptors from the authoritative map.
    // Full replay is for a newly connected client or a forced HUD reset.
    bool Update(const CCSOLScenarioMessages& messages, edict_t* recipient = nullptr, bool replay = false);
    bool Inject(playermove_t& move);
private:
    struct Clip
    {
        EHANDLE entity;
        csol::playerclip::Descriptor descriptor;
        model_s* model{};
        bool announced{};
    };
    bool Current() const;
    bool Read(Clip& clip, bool& alive) const;
    std::shared_ptr<const CSOLScenarioEntityAccess> m_access;
    std::array<Clip, csol::playerclip::Capacity> m_clips{};
    std::size_t m_count{};
    csol::playerclip::Movement m_movement;
    bool m_collected{}, m_busy{};
};
