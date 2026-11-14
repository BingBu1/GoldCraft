// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_music.h"
#include "game_rules_gate.h"

bool CCSOLScenarioMusic::Current() const
{
    if(!m_access||!m_access->Allowed())return false;
    auto* rules=CSOL_ScenarioOwnerRules(m_access.get());
    if(!rules)return false;
    CSOLScenarioSdkScope scope(rules);
    return scope.Scenario()&&scope.Current()&&scope.EntityAccess()==m_access;
}

bool CCSOLScenarioMusic::PrecacheFile(std::string_view filename,csol::scenario::ResourceError& error)
{
    error={};
    if(s_sending||!Current())return false;
    const auto generation=m_generation;
    const auto path=csol::music::ResourcePath(filename);
    if(!path){error={std::string(filename),{0,"invalid scenario BGM path"}};return false;}
    if(m_paths.contains(*path))return true;
    if(!CSOL_ScenarioMusicPrecacheAllowed(m_access.get()))
    {error={*path,{0,"scenario BGM was not registered during map loading"}};return false;}
    if(!g_engfuncs.pfnGetFileSize||!g_engfuncs.pfnPrecacheGeneric)
    {error={*path,{0,"scenario BGM file/precache interface is unavailable"}};return false;}
    const int bytes=g_engfuncs.pfnGetFileSize(path->c_str());
    if(!Current()||m_generation!=generation)return false;
    if(bytes<=0){error={*path,{0,"scenario BGM is missing or empty"}};return false;}
    // A downstream hook can reset this service. Keep the engine argument
    // independent of m_paths until the entire precache callback returns.
    PRECACHE_GENERIC(const_cast<char*>(path->c_str()));
    if(!Current()||m_generation!=generation)return false;
    m_paths.insert(*path);
    return true;
}

bool CCSOLScenarioMusic::Precache(const csol::scenario::RoundTable& rounds,csol::scenario::ResourceError& error,
                                bool challengeNight)
{
    error={};
    if(s_sending||!Current())return false;
    const auto generation=m_generation;
    if(!g_engfuncs.pfnRegUserMsg){error={"",{0,"scenario BGM message registration is unavailable"}};return false;}
    const int message=REG_USER_MSG(csol::music::MessageName,-1);
    if(!Current()||generation!=m_generation)return false;
    if(message<64||message>255){error={"",{0,"scenario BGM message registration failed"}};return false;}
    m_message=message;
    if(!PrecacheFile("Scenario_Ready.mp3",error))return false;
    for(std::size_t index=0;index<rounds.rows.size();++index)
    {
        csol::scenario::RoundSettings settings;
        if(!csol::scenario::ReadRoundSettings(rounds,index,settings,error.detail))return false;
        const auto track=csol::music::FindTrack(settings.music);
        if(!track){error={"",{index,"unsupported scenario music ID: "+std::to_string(settings.music)}};return false;}
        if(!track->filename.empty()&&!PrecacheFile(track->filename,error))return false;
    }
    if(challengeNight)
    {
        const auto night=csol::music::FindTrack(18);
        if(!night||night->action!=csol::music::Action::Play||night->filename.empty())
        {error={"",{0,"original challenge night track is unavailable"}};return false;}
        for(const auto file:csol::music::NightFiles)
            if(!PrecacheFile(file,error))return false;
    }
    return Current()&&m_generation==generation;
}

bool CCSOLScenarioMusic::Send(const csol::music::Packet& packet,CBasePlayer* recipient) const
{
    if(s_sending||m_message<64||m_message>255||!Current()||
        !g_engfuncs.pfnMessageBegin||!g_engfuncs.pfnWriteByte||!g_engfuncs.pfnMessageEnd)return false;
    const auto bytes=csol::music::Encode(packet);
    if(bytes.empty())return false;
    const auto generation=m_generation;
    const int message=m_message;
    struct SendingScope { SendingScope(){s_sending=true;} ~SendingScope(){s_sending=false;} } scope;
    if(recipient)
    {
        if(recipient->has_disconnected||(recipient->pev->flags&(FL_FAKECLIENT|FL_DORMANT|FL_KILLME)))return true;
        MESSAGE_BEGIN(MSG_ONE,message,nullptr,recipient->pev);
    }
    else MESSAGE_BEGIN(MSG_ALL,message);
    for(auto value:bytes)WRITE_BYTE(value);
    MESSAGE_END();
    return Current()&&generation==m_generation;
}
bool CCSOLScenarioMusic::Publish(csol::music::Action action,std::string path)
{
    if(s_sending||!Current())return false;
    const auto generation=m_generation;
    auto revision=m_mapRevision+1;if(!revision)++revision;
    csol::music::Packet desired{action,revision,std::move(path)};
    if(!csol::music::Command(desired))return false;
    if(!m_night&&!m_dusk&&!PublishOutput(desired.action,desired.path))return false;
    if(!Current()||generation!=m_generation)return false;
    m_mapState=std::move(desired);m_mapRevision=revision;
    return true;
}
bool CCSOLScenarioMusic::PublishOutput(csol::music::Action action,std::string path,std::string nextPath)
{
    if(s_sending||!Current())return false;
    const auto generation=m_generation;
    auto revision=m_state.revision+1;if(!revision)++revision;
    csol::music::Packet packet{action,revision,std::move(path),std::move(nextPath)};
    if(!csol::music::Command(packet))return false;
    if(!Send(packet)||generation!=m_generation)return false;
    m_state=std::move(packet);
    return true;
}
bool CCSOLScenarioMusic::SetNight(std::uint64_t challenge,bool active,csol::music::NightChoice choice)
{
    if(s_sending||!Current()||!challenge||challenge<m_challenge||choice>csol::music::NightChoice::Rex)return false;
    if(challenge==m_challenge&&active==m_night&&!m_dusk)return !active||choice==m_nightChoice;
    const auto generation=m_generation;
    if(active)
    {
        const auto path=csol::music::ResourcePath(csol::music::NightFiles[choice==csol::music::NightChoice::Zsh?0:1]);
        const auto tail=choice==csol::music::NightChoice::Rex?
            csol::music::ResourcePath(csol::music::NightFiles[2]):std::optional<std::string>{std::string{}};
        if(!path||!tail||!m_paths.contains(*path)||(!tail->empty()&&!m_paths.contains(*tail))||
            !PublishOutput(choice==csol::music::NightChoice::Rex?csol::music::Action::PlayThenLoop:
                csol::music::Action::Loop,*path,*tail))return false;
    }
    else if((m_night||m_dusk)&&!PublishOutput(m_mapState.action,m_mapState.path))return false;
    if(!Current()||m_generation!=generation)return false;
    m_challenge=challenge;m_night=active;m_dusk=false;if(active)m_nightChoice=choice;
    return true;
}
bool CCSOLScenarioMusic::SetDusk(std::uint64_t challenge)
{
    if(s_sending||!Current()||!challenge||challenge<m_challenge||
        (challenge==m_challenge&&m_night))return false;
    if(challenge==m_challenge&&m_dusk)return true;
    const auto path=csol::music::ResourcePath(csol::music::NightFiles[3]);
    const auto generation=m_generation;
    if(!path||!m_paths.contains(*path)||!PublishOutput(csol::music::Action::Play,*path))return false;
    if(!Current()||m_generation!=generation)return false;
    m_challenge=challenge;m_dusk=true;m_night=false;return true;
}
bool CCSOLScenarioMusic::Select(std::uint8_t id)
{
    const auto track=csol::music::FindTrack(id);
    if(!track)return false;
    return track->filename.empty()?Fade():Play(track->filename,track->action==csol::music::Action::Loop);
}
bool CCSOLScenarioMusic::Play(std::string_view filename,bool loop)
{
    const auto path=csol::music::ResourcePath(filename);
    return path&&m_paths.contains(*path)&&Publish(loop?csol::music::Action::Loop:csol::music::Action::Play,*path);
}
bool CCSOLScenarioMusic::Fade(){return Publish(csol::music::Action::Fade);}
bool CCSOLScenarioMusic::Replay(CBasePlayer& player) const
{
    // Reconnect resumes the selected track from its beginning. GoldSrc's MP3
    // console interface has no seek; phase-aligned seeking is not claimed.
    return !s_sending&&(!m_state.revision||Send(m_state,&player));
}
void CCSOLScenarioMusic::Reset()
{
    if(!s_sending&&m_state.revision&&Current())PublishOutput(csol::music::Action::Stop);
    ++m_generation;
    m_state={};m_mapState={};m_paths.clear();m_message=0;
    m_mapRevision=0;m_challenge=0;m_night=m_dusk=false;m_nightChoice={};
}
