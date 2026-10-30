// SPDX-License-Identifier: GPL-3.0-or-later
#include "scenario_music.hpp"
#include <array>
#include <cmath>

namespace csol::music
{
Cue Countdown::Tick(float now)
{
    // MP114BFD30: default room option is false (HW37AA668E), so music has
    // its own 20-second countdown. Restart and thaw both write root+248;
    // neither short freeze time nor wave timing substitutes for this clock.
    if(m_complete||!std::isfinite(now)||!std::isfinite(m_start))return Cue::None;
    const float end=m_start+20.0f;
    if(now>=end-1.0f){m_complete=true;return Cue::Round;}
    if(m_ready)return Cue::None;
    m_ready=true;return Cue::Ready;
}
std::optional<Track> FindTrack(std::uint8_t id)
{
    // Original client SHA f4145136, switch2A5BF5C -> 2A5B180.
    static constexpr std::array<Track,39> tracks{{
        {Action::Fade,{}}, {Action::Play,"Scenario_Ready.mp3"},
        {Action::Play,"Scenario_Normal.mp3"}, {Action::Play,"Scenario_Rush.mp3"},
        {Action::Loop,"Scenario_XMas.mp3"}, {Action::Loop,"Scenario_Normal.mp3"},
        {Action::Loop,"Scenario_Rush.mp3"}, {Action::Loop,"HS_Scenario_Ready.mp3"},
        {Action::Play,"ZE_Ready.mp3"}, {Action::Loop,"Chall_Ambience.mp3"},
        {Action::Loop,"ZE_Ambience.mp3"}, {Action::Play,"Zsh_start.mp3"},
        {Action::Loop,"Zsh_daybgm.mp3"}, {Action::Loop,"Zsh_nightbgm.mp3"},
        {Action::Play,"sm_start.mp3"}, {Action::Play,"BGM_start.mp3"},
        {Action::Play,"BGM_zombie_attack.mp3"}, {Action::Play,"ZB4_start.mp3"},
        {Action::Play,"Play_BGM_Rex.mp3"}, {Action::Loop,"Zombi_Ambience.mp3"},
        {Action::Play,"ZombieZ_Start.mp3"}, {Action::Loop,"Laststand.mp3"},
        {Action::Play,"touchdown_start.mp3"}, {Action::Loop,"prophunt_Intro_Studio.mp3"},
        {Action::Loop,"prophunt_burningtime_Studio.mp3"}, {},
        {Action::Play,"ZHE_start.mp3"}, {Action::Loop,"ZHE_runaway1.mp3"},
        {Action::Loop,"ZHE_runaway2.mp3"}, {Action::Loop,"ZHE_chaser.mp3"},
        {Action::Play,"zombie_rise_start.mp3"}, {Action::Play,"Protection_GameOver.mp3"},
        {Action::Loop,"svp_me.mp3"}, {Action::Play,"zombie_expedite_start.mp3"},
        {Action::Play,"ZS_Rift_Start.mp3"}, {Action::Loop,"ZS_Rift_Play.mp3"},
        {Action::Play,"ZS_Rift_Fail.mp3"}, {Action::Play,"ZS_Rift_Clear.mp3"},
        {Action::Loop,"ZS_Rift_Boss.mp3"}
    }};
    if(id>=tracks.size() || id==25)return {};
    return tracks[id];
}

std::optional<std::string> ResourcePath(std::string_view filename)
{
    if(filename.empty() || filename.size()>MaxPath)return {};
    std::string name(filename);
    for(char& c:name)
    {
        if(c=='\\')c='/';
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||
            c=='_'||c=='-'||c=='.'||c=='/'||c==' '))return {};
    }
    if(name.starts_with("bgm/"))name.erase(0,4);
    if(name.size()<5 || name.size()+4>MaxPath || !name.ends_with(".mp3"))return {};
    std::size_t start{};
    for(;;)
    {
        const auto end=name.find('/',start);
        const auto part=std::string_view(name).substr(start,end==name.npos?end:end-start);
        if(part.empty()||part=="."||part==".."||part.front()==' '||part.back()==' '||part.back()=='.')return {};
        if(end==name.npos)break;
        start=end+1;
    }
    return "bgm/"+name;
}

std::optional<std::string> Command(const Packet& packet)
{
    if(!packet.revision)return {};
    if(packet.action==Action::Stop || packet.action==Action::Fade)
    {
        if(!packet.path.empty())return {};
        // B hw mp3 command has no fadeout branch. Its cd fadeout operates on
        // the same stream owner as mp3 play/loop, including MP3 state.
        return packet.action==Action::Fade?"cd fadeout\n":"mp3 stop\n";
    }
    if(packet.action!=Action::Play && packet.action!=Action::Loop)return {};
    const auto path=ResourcePath(packet.path);
    if(!path || *path!=packet.path)return {};
    // Register the path as a track before queuing playback. On the matched
    // GoldSrc engine, playfile/loopfile share a request buffer which a queued
    // Stop can clear before Play consumes it when another stream is active.
    // play/loop retain the track ID across Stop and use the same file resolver.
    return std::string(packet.action==Action::Loop?"mp3 loop \"":"mp3 play \"")+*path+"\"\n";
}

std::vector<std::uint8_t> Encode(const Packet& packet)
{
    if(!Command(packet))return {};
    std::vector<std::uint8_t> bytes{1,static_cast<std::uint8_t>(packet.action)};
    for(unsigned i=0;i<4;++i)bytes.push_back(static_cast<std::uint8_t>(packet.revision>>(8*i)));
    bytes.insert(bytes.end(),packet.path.begin(),packet.path.end());bytes.push_back(0);
    return bytes;
}

std::optional<Packet> Decode(std::span<const std::byte> bytes)
{
    if(bytes.size()<7 || bytes.size()>MaxPath+7 || bytes[0]!=std::byte{1} || bytes.back()!=std::byte{})return {};
    Packet packet{static_cast<Action>(std::to_integer<std::uint8_t>(bytes[1]))};
    for(unsigned i=0;i<4;++i)packet.revision|=std::uint32_t(std::to_integer<std::uint8_t>(bytes[2+i]))<<(8*i);
    packet.path.assign(reinterpret_cast<const char*>(bytes.data()+6),bytes.size()-7);
    if(!Command(packet))return {};
    return packet;
}
}
