// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../scenario_music.hpp"
#include "../scenario/round_settings.hpp"
#include "../scenario/map_resources.hpp"
#include "scenario_entity_access.h"
#include <memory>
#include <set>
#include <utility>

class CBasePlayer;
class CCSOLScenarioMusic
{
public:
    explicit CCSOLScenarioMusic(std::shared_ptr<const CSOLScenarioEntityAccess> access):m_access(std::move(access)){}
    // Table music is registered during world loading. Trigger files register
    // from their own Spawn while the selected session is still Precached.
    bool Precache(const csol::scenario::RoundTable& rounds,csol::scenario::ResourceError& error);
    bool PrecacheFile(std::string_view filename,csol::scenario::ResourceError& error);
    bool Select(std::uint8_t id);
    bool Play(std::string_view filename,bool loop);
    bool Fade();
    bool Replay(CBasePlayer& player) const;
    bool Current() const;
    std::uint32_t Revision() const { return m_state.revision; }
    void Reset();
private:
    bool Publish(csol::music::Action action,std::string path={});
    bool Send(const csol::music::Packet& packet,CBasePlayer* recipient=nullptr) const;
    std::shared_ptr<const CSOLScenarioEntityAccess> m_access;
    std::set<std::string> m_paths;
    csol::music::Packet m_state;
    int m_message{};
    std::uint64_t m_generation{};
    inline static bool s_sending{};
};
// Only the exact currently installed scenario owner may expose this service.
std::shared_ptr<CCSOLScenarioMusic> CSOL_ScenarioMusic(const CSOLScenarioEntityAccess* access);
bool CSOL_ScenarioMusicPrecacheAllowed(const CSOLScenarioEntityAccess* access);
