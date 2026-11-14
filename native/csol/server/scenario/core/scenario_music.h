// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../../../scenario/core/scenario_music.hpp"
#include "../../../scenario/core/round_settings.hpp"
#include "../../../scenario/core/map_resources.hpp"
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
    bool Precache(const csol::scenario::RoundTable& rounds,csol::scenario::ResourceError& error,
                  bool challengeNight=false);
    bool PrecacheFile(std::string_view filename,csol::scenario::ResourceError& error);
    bool Select(std::uint8_t id);
    bool Play(std::string_view filename,bool loop);
    bool Fade();
    // Custom challenge policy: map requests retain their intent while night
    // owns the output. The human explicitly requires looping the original
    // track program here; ordinary original Select(18) remains Play once.
    bool SetNight(std::uint64_t challenge,bool active,csol::music::NightChoice choice=csol::music::NightChoice::Rex);
    bool SetDusk(std::uint64_t challenge);
    bool Replay(CBasePlayer& player) const;
    bool Current() const;
    std::uint32_t Revision() const { return m_mapRevision; }
    void Reset();
private:
    bool Publish(csol::music::Action action,std::string path={});
    bool PublishOutput(csol::music::Action action,std::string path={},std::string nextPath={});
    bool Send(const csol::music::Packet& packet,CBasePlayer* recipient=nullptr) const;
    std::shared_ptr<const CSOLScenarioEntityAccess> m_access;
    std::set<std::string> m_paths;
    csol::music::Packet m_state;
    csol::music::Packet m_mapState;
    std::uint32_t m_mapRevision{};
    std::uint64_t m_challenge{};
    bool m_night{},m_dusk{};
    csol::music::NightChoice m_nightChoice{};
    int m_message{};
    std::uint64_t m_generation{};
    inline static bool s_sending{};
};
// Only the exact currently installed scenario owner may expose this service.
std::shared_ptr<CCSOLScenarioMusic> CSOL_ScenarioMusic(const CSOLScenarioEntityAccess* access);
bool CSOL_ScenarioMusicPrecacheAllowed(const CSOLScenarioEntityAccess* access);
