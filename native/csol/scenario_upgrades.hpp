// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace csol::upgrades
{
enum class ParseResult { Unrelated, Malformed, Accepted };
enum class Operation { Experience, Total, Increase, Stat, Limit };
struct Message { Operation operation{}; unsigned stat{}; std::int32_t value{}, limit{}; };
ParseResult Decode(std::span<const std::byte>, Message&);
struct Stat { std::int32_t level{}, limit{}; };
enum class Image : unsigned { LevelBackground, LevelLabel, Experience, ExperienceBackground,
    NoticeLeft, NoticeCenter, NoticeRight, MovieTop, MovieBottom, Digits, Count };
struct Resource { const char* path; unsigned width, height; };
inline constexpr std::array<Resource, unsigned(Image::Count)> Resources{{
    {"resource/zombiez/zmode_level_bg.tga",100,24},
    {"resource/zombiez/zmode_level_num.tga",256,16},
    {"resource/zombiez/zmode_level_bar.tga",1,4},
    {"resource/zombiez/zmode_level_bar_bg.tga",1,4},
    {"resource/hud/zbs/TextBg_Left.tga",128,32},
    {"resource/hud/zbs/TextBg_Center.tga",1,32},
    {"resource/hud/zbs/TextBg_Right.tga",128,32},
    {"resource/csol/scenario/levelup_line_top.tga",384,583},
    {"resource/csol/scenario/levelup_line_bottom.tga",384,583},
    {"resource/csol/scenario/level_digits.tga",130,18},
}};
inline constexpr char FontPath[]="resource/font/df_gb_y9.ttf";
inline constexpr char SoundPath[]="events/Lvup.wav";
inline constexpr char DigitSpritePath[]="sprites/csol/scenario/level_digits.spr";
struct Quad
{
    Image image{};
    float x{},y{},width{},height{},u0{},v0{},u1{1},v1{1};
    std::array<float,4> color{1,1,1,1};
    bool additive{};
};
struct Frame { std::array<Quad,24> quads{}; unsigned count{}; bool notice{}; int textY{}; };
class State
{
public:
    void Reset();
    void ClearPresentation();
    // Returns the original subtype-5 sound trigger, independently of whether
    // the already visible notice restarts (it does not).
    bool Apply(const Message&, float hudTime, float realTime);
    void Advance(float hudTime);
    Frame Layout(int width,int height,float hudTime,float realTime) const;
    bool Active() const { return active_; }
    bool NoticeVisible() const { return notice_; }
    std::int32_t Total() const { return total_; }
    const std::array<Stat,5>& Stats() const { return stats_; }
    float Experience() const { return experience_; }
private:
    std::array<Stat,5> stats_{};
    std::int32_t total_{};
    float experience_{},experienceChanged_{},shownAt_{},movieStarted_{};
    bool active_{},notice_{};
};
}
