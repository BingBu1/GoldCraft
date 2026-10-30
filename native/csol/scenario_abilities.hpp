// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace csol::abilities
{
enum class ParseResult { Unrelated, Malformed, Accepted };
enum class Operation { Cooldown, Number, Progress };
struct Message { Operation operation{}; int type{}; float value{},duration{}; };
ParseResult Decode(std::span<const std::byte>,Message&);
// GoldCraft replay snapshot, separate from the original ZBSVer2 operations.
// Version 1 covers the elapsed-time abilities implemented by this server.
struct Snapshot { unsigned type{}; float cooldown{}, elapsed{}, duration{}; };
ParseResult DecodeSnapshot(std::span<const std::byte>,Snapshot&);
struct Resources { const char* icon;const char* key;const char* background; };
const Resources* ResourceFor(unsigned type);
inline constexpr char ReadyHighlight[]="resource/hud/buffclassskill_highlight.tga";
inline constexpr char NumberHighlight[]="resource/hud/buffclassskill_highlight2.tga";
// These are the actual number overlays supplied with the original panel.
inline constexpr std::array<const char*,9> NumberImages{{
    "resource/hud/buffclassskill_num1.tga","resource/hud/buffclassskill_num2.tga",
    "resource/hud/buffclassskill_num3.tga","resource/hud/buffclassskill_num4.tga",
    "resource/hud/buffclassskill_num5.tga","resource/hud/buffclassskill_num6.tga",
    "resource/hud/buffclassskill_num7.tga","resource/hud/buffclassskill_num8.tga",
    "resource/hud/buffclassskill_num9.tga"}};
// Only the two currently implemented server ability types are required at
// map precache. Other type images remain lazily loaded on a matching message.
inline constexpr std::array<const char*,17> DownloadResources{{
    "resource/hud/buffclassskill_heal.tga",
    "resource/hud/buffclassskill_heal_number.tga",
    "resource/hud/buffclassskill_heal_bg.tga",
    "resource/hud/buffclassskill_23s1ct.tga",
    "resource/hud/buffclassskill_damage_number.tga",
    "resource/hud/buffclassskill_23s1ct_bg.tga",
    ReadyHighlight,NumberHighlight,
    NumberImages[0],NumberImages[1],NumberImages[2],NumberImages[3],NumberImages[4],
    NumberImages[5],NumberImages[6],NumberImages[7],NumberImages[8]}};
enum class Image { Background,Icon,Key,Ready,NumberFlash,Number };
struct Quad { Image image{};int x{},y{},width{77},height{75};float v0{},v1{1};int alpha{255}; };
struct Frame { std::array<Quad,5> quads{};unsigned count{}; };
class State
{
public:
    void Reset();
    void Apply(const Message&);
    bool Restore(const Snapshot&);
    void Advance(double delta);
    Frame Layout(int width,int height,bool elevated)const;
    bool Visible()const{return visible_;}
    unsigned Type()const{return type_;}
    int Number()const{return number_;}
    float Fraction()const{return fraction_;}
    float Elapsed()const{return elapsed_;}
    float NumberTime()const{return numberTime_;}
    int FlashCount()const{return flashes_;}
    int FlashAlpha()const{return drawnAlpha_;}
private:
    void Hide();
    unsigned type_{};
    int number_{},flashes_{},direction_{1},drawnAlpha_{};
    float cooldown_{},elapsed_{},notificationTime_{},numberTime_{},override_{-1},fraction_{},flashAlpha_{};
    bool visible_{},readyArmed_{},numberFlash_{};
};
}
