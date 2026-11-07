// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "message_table.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace csol::scenario_hud
{
enum class ParseResult { Unrelated, Malformed, Accepted };
enum class Kind { Round, Dialogue, Frags, ResetFrags, TeamKills, TimerOffset };
struct Message
{
    Kind kind{};
    unsigned player{},value{},total{};
    float elapsed{};
    std::int32_t secondsOffset{};
};
ParseResult DecodeMP(std::span<const std::byte> bytes,Message& output);
ParseResult DecodeValueSync(std::span<const std::byte> bytes,Message& output);
enum class Image : unsigned { Background,TotalTitle,PersonalTitle,RoundTitle,Digits,RoundDigits,TimerDigits,TimerColon,TextLeft,TextCenter,TextRight,Count };
struct Resource { const char* path; int width,height; };
inline constexpr std::array<Resource,unsigned(Image::Count)> Resources{{
    {"resource/Hud/hud_scoreboard_bg_humanscenario.tga",333,83},
    {"resource/Hud/hud_text_icon_totalkill_left.tga",104,10},
    {"resource/Hud/hud_text_icon_kill_right.tga",63,10},
    {"resource/Hud/hud_text_icon_round.tga",70,10},
    {"resource/Hud/hud_sb_num_humanscenario.tga",140,22},
    {"resource/Hud/hud_sb_num_center.tga",110,13},
    {"resource/Hud/hud_sb_num_bottom.tga",80,12},
    {"resource/Hud/hud_sb_num_bottom_colon.tga",3,10},
    {"resource/hud/zbs/TextBg_Left.tga",128,32},
    {"resource/hud/zbs/TextBg_Center.tga",1,32},
    {"resource/hud/zbs/TextBg_Right.tga",128,32},
}};
inline constexpr char MessagePath[]="maps/ZBS_message.cso";
inline constexpr char LocalizationPath[]="resource/cso_chn.txt";
inline constexpr char FontPath[]="resource/font/df_gb_y9.ttf";
struct Quad { Image image{}; int x{},y{},width{},height{},alpha{255};float u0{},u1{1}; };
struct Frame { std::array<Quad,32> quads{};unsigned count{};std::int32_t seconds{};bool clock{}; };
enum class Font : unsigned { ResultTitle,ResultTeam,Normal24,DefaultTitle,Default,Count };
struct TextDefaults { float x,y,fadeIn,show,fadeOut,flicker;Font font; };
inline constexpr std::array<TextDefaults,20> TextChannels{{
    {.5f,.315f,.2f,3,.2f,-1,Font::ResultTitle},
    {.5f,.325f,.5f,3,.5f,-1,Font::ResultTeam},
    {.5f,.7f,.2f,3,.2f,.6f,Font::ResultTitle},
    {.5f,.325f,.5f,3,.5f,-1,Font::ResultTeam},
    {.5f,.294f,.5f,3,.5f,-1,Font::Normal24},
    {.5f,.716f,.5f,3,.5f,-1,Font::ResultTeam},
    {.5f,.391f,.1f,1.1f,.1f,-1,Font::ResultTeam},
    {.5f,.391f,.1f,1.1f,.1f,-1,Font::ResultTeam},
    {.5f,.44f,.5f,3,.5f,-1,Font::ResultTeam},
    {.5f,.7f,.2f,3,.2f,.6f,Font::Normal24},
    {.5f,.283f,.5f,3,.5f,-1,Font::ResultTeam},
    {.5f,.311f,.5f,3,.5f,-1,Font::ResultTeam},
    {.5f,.3f,0,.5f,.5f,-1,Font::DefaultTitle},
    {.5f,.5f,.1f,3,.2f,-1,Font::ResultTeam},
    {.5f,.39f,.1f,3,.2f,-1,Font::ResultTeam},
    {.5f,.8f,.5f,3,.5f,-1,Font::ResultTeam},
    {.2f,.4f,.1f,5,.5f,-1,Font::Normal24},
    {.8f,.1f,0,5,0,-1,Font::DefaultTitle},
    {.5f,.146f,.5f,3,.5f,-1,Font::Default},
    {.2f,.6f,0,99999,0,-1,Font::Normal24},
}};
struct TextNotice
{
    bool active{};std::uint16_t id{};
    float born{},fadeEnd{},showEnd{},die{},fadeIn{},fadeOut{},flicker{},x{},y{};
    std::array<std::uint8_t,3> color{};
};
int NoticeAlpha(const TextNotice& notice,float now);
struct ImageDefaults { int channel,x,y,width,height; };
// Original 029ADEA4/029C7A90: centered on a 1024x768 reference, scale=1.
inline constexpr std::array<ImageDefaults,4> ImageChannels{{
    {10,512,164,512,128},{11,512,222,256,256},
    {12,512,176,256,128},{17,512,218,571,142},
}};
struct ImageRect { int x{},y{},width{},height{}; };
ImageRect DialogueImageRect(unsigned channel,int screenWidth,int screenHeight);
class State
{
public:
    void Reset();
    void ClearPresentation();
    bool Apply(const Message& message,float clientTime);
    bool Show(const scenario::ScenarioMessage& message,float realTime);
    bool ShowImage(const scenario::ScenarioMessage& message,float clientTime);
    Frame Layout(int width,int localPlayer,float clientTime) const;
    const auto& Notices()const{return notices_;}
    const auto& ImageNotices()const{return imageNotices_;}
    bool Active()const{return active_;}
private:
    std::array<unsigned,33> frags_{};
    std::array<TextNotice,20> notices_{};
    std::array<TextNotice,ImageChannels.size()> imageNotices_{};
    unsigned round_{},totalRounds_{},teamKills_{};
    float started_{};
    std::int32_t secondsOffset_{};
    bool active_{};
};
}
