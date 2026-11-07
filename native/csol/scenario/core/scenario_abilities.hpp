// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

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
inline constexpr std::array<Resources,55> ResourcesByType{{
    {"resource/hud/buffclassskill_heal.tga","resource/hud/buffclassskill_heal_number.tga","resource/hud/buffclassskill_heal_bg.tga"}, // 1
    {"resource/hud/buffclassskill_damage.tga","resource/hud/buffclassskill_damage_number.tga","resource/hud/buffclassskill_damage_bg.tga"}, // 2
    {"resource/hud/buffclassskill_armorup.tga","resource/hud/buffclassskill_heal_number.tga","resource/hud/buffclassskill_armorup_bg.tga"}, // 3
    {"resource/hud/buffclassskill_speed.tga","resource/hud/buffclassskill_damage_number.tga","resource/hud/buffclassskill_speed_bg.tga"}, // 4
    {"resource/hud/buffclassskill_ammo.tga","resource/hud/buffclassskill_damage_number.tga","resource/hud/buffclassskill_ammo_bg.tga"}, // 5
    {"resource/hud/buffclassskill_shield.tga","resource/hud/buffclassskill_heal_number.tga","resource/hud/buffclassskill_shield_bg.tga"}, // 6
    {"resource/hud/buffclassskill_explosion.tga","resource/hud/buffclassskill_damage_number.tga","resource/hud/buffclassskill_explosion_bg.tga"}, // 7
    {"resource/hud/buffclassskill_maxhealth.tga","resource/hud/buffclassskill_damage_number.tga","resource/hud/buffclassskill_maxhealth_bg.tga"}, // 8
    {"resource/hud/buffclassskill_invincible.tga","resource/hud/buffclassskill_heal_number.tga","resource/hud/buffclassskill_invincible_bg.tga"}, // 9
    {"resource/hud/buffclassskill_auto_berserk.tga",nullptr,"resource/hud/buffclassskill_auto_berserk_bg.tga"}, // 10
    {"resource/hud/buffclassskill_auto_bossdmgup.tga",nullptr,"resource/hud/buffclassskill_auto_bossdmgup_bg.tga"}, // 11
    {"resource/hud/buffclassskill_21s1tr.tga","resource/hud/buffclassskill_damage_number.tga","resource/hud/buffclassskill_21s1tr_bg.tga"}, // 12
    {"resource/hud/buffclassskill_21s1ct.tga",nullptr,"resource/hud/buffclassskill_21s1ct_bg.tga"}, // 13
    {"resource/hud/buffclassskill_21s2tr.tga","resource/hud/buffclassskill_damage_number.tga","resource/hud/buffclassskill_21s2tr_bg.tga"}, // 14
    {"resource/hud/buffclassskill_21s2ct.tga","resource/hud/buffclassskill_heal_number.tga","resource/hud/buffclassskill_21s2ct_bg.tga"}, // 15
    {"resource/hud/buffclassskill_21s3tr.tga","resource/hud/buffclassskill_damage_number.tga","resource/hud/buffclassskill_21s3tr_bg.tga"}, // 16
    {"resource/hud/buffclassskill_21s3ct.tga","resource/hud/buffclassskill_heal_number.tga","resource/hud/buffclassskill_21s3ct_bg.tga"}, // 17
    {"resource/hud/buffclassskill_21s4tr.tga","resource/hud/buffclassskill_damage_number.tga","resource/hud/buffclassskill_21s4tr_bg.tga"}, // 18
    {"resource/hud/buffclassskill_21s4ct.tga","resource/hud/buffclassskill_heal_number.tga","resource/hud/buffclassskill_21s4ct_bg.tga"}, // 19
    {"resource/hud/buffclassskill_22s1tr.tga","resource/hud/buffclassskill_heal_number.tga","resource/hud/buffclassskill_22s1tr_bg.tga"}, // 20
    {"resource/hud/buffclassskill_22s1ct.tga",nullptr,"resource/hud/buffclassskill_22s1ct_bg.tga"}, // 21
    {"resource/hud/buffclassskill_22s2tr.tga","resource/hud/buffclassskill_damage_number.tga","resource/hud/buffclassskill_22s2tr_bg.tga"}, // 22
    {"resource/hud/buffclassskill_22s2ct.tga","resource/hud/buffclassskill_damage_number.tga","resource/hud/buffclassskill_22s2ct_bg.tga"}, // 23
    {"resource/hud/buffclassskill_22s3tr.tga","resource/hud/buffclassskill_damage_number.tga","resource/hud/buffclassskill_22s3tr_bg.tga"}, // 24
    {"resource/hud/buffclassskill_22s3ct.tga","resource/hud/buffclassskill_damage_number.tga","resource/hud/buffclassskill_22s3ct_bg.tga"}, // 25
    {"resource/hud/buffclassskill_22s4tr.tga","resource/hud/buffclassskill_damage_number.tga","resource/hud/buffclassskill_22s4tr_bg.tga"}, // 26
    {"resource/hud/buffclassskill_22s4ct.tga","resource/hud/buffclassskill_damage_number.tga","resource/hud/buffclassskill_22s4ct_bg.tga"}, // 27
    {"resource/hud/buffclassskill_23s1tr.tga","resource/hud/buffclassskill_damage_number.tga","resource/hud/buffclassskill_23s1tr_bg.tga"}, // 28
    {"resource/hud/buffclassskill_23s1ct.tga","resource/hud/buffclassskill_damage_number.tga","resource/hud/buffclassskill_23s1ct_bg.tga"}, // 29
    {"resource/hud/buffclassskill_23s2tr.tga","resource/hud/buffclassskill_damage_number.tga","resource/hud/buffclassskill_23s2tr_bg.tga"}, // 30
    {"resource/hud/buffclassskill_23s2ct.tga","resource/hud/buffclassskill_damage_number.tga","resource/hud/buffclassskill_23s2ct_bg.tga"}, // 31
    {"resource/hud/buffclassskill_23s3tr.tga","resource/hud/buffclassskill_damage_number.tga","resource/hud/buffclassskill_23s3tr_bg.tga"}, // 32
    {"resource/hud/buffclassskill_23s3ct.tga","resource/hud/buffclassskill_damage_number.tga","resource/hud/buffclassskill_23s3ct_bg.tga"}, // 33
    {"resource/hud/buffclassskill_23s4tr.tga","resource/hud/buffclassskill_damage_number.tga","resource/hud/buffclassskill_23s4tr_bg.tga"}, // 34
    {"resource/hud/buffclassskill_23s4ct.tga","resource/hud/buffclassskill_damage_number.tga","resource/hud/buffclassskill_23s4ct_bg.tga"}, // 35
    {"resource/hud/buffclassskill_23s5tr.tga","resource/hud/buffclassskill_damage_number.tga","resource/hud/buffclassskill_23s5tr_bg.tga"}, // 36
    {"resource/hud/buffclassskill_24s1tr.tga","resource/hud/buffclassskill_damage_number.tga","resource/hud/buffclassskill_24s1tr_bg.tga"}, // 37
    {"resource/hud/buffclassskill_24s1ct.tga","resource/hud/buffclassskill_damage_number.tga","resource/hud/buffclassskill_24s1ct_bg.tga"}, // 38
    {"resource/hud/buffclassskill_24s2tr.tga","resource/hud/buffclassskill_damage_number.tga","resource/hud/buffclassskill_24s2tr_bg.tga"}, // 39
    {"resource/hud/buffclassskill_24s2ct.tga","resource/hud/buffclassskill_damage_number.tga","resource/hud/buffclassskill_24s2ct_bg.tga"}, // 40
    {"resource/hud/buffclassskill_24s3tr.tga","resource/hud/buffclassskill_damage_number.tga","resource/hud/buffclassskill_24s3tr_bg.tga"}, // 41
    {"resource/hud/buffclassskill_24s3ct.tga","resource/hud/buffclassskill_damage_number.tga","resource/hud/buffclassskill_24s3ct_bg.tga"}, // 42
    {"resource/hud/buffclassskill_24s4tr.tga","resource/hud/buffclassskill_damage_number.tga","resource/hud/buffclassskill_24s4tr_bg.tga"}, // 43
    {"resource/hud/buffclassskill_24s4ct.tga","resource/hud/buffclassskill_damage_number.tga","resource/hud/buffclassskill_24s4ct_bg.tga"}, // 44
    {"resource/hud/buffclassskill_25s1tr.tga","resource/hud/buffclassskill_damage_number.tga","resource/hud/buffclassskill_25s1tr_bg.tga"}, // 45
    {"resource/hud/buffclassskill_25s1ct.tga","resource/hud/buffclassskill_damage_number.tga","resource/hud/buffclassskill_25s1ct_bg.tga"}, // 46
    {"resource/hud/buffclassskill_25s2tr.tga","resource/hud/buffclassskill_damage_number.tga","resource/hud/buffclassskill_25s2tr_bg.tga"}, // 47
    {"resource/hud/buffclassskill_25s2ct.tga","resource/hud/buffclassskill_damage_number.tga","resource/hud/buffclassskill_25s2ct_bg.tga"}, // 48
    {"resource/hud/buffclassskill_25s3tr.tga","resource/hud/buffclassskill_damage_number.tga","resource/hud/buffclassskill_25s3tr_bg.tga"}, // 49
    {"resource/hud/buffclassskill_25s3ct.tga","resource/hud/buffclassskill_damage_number.tga","resource/hud/buffclassskill_25s3ct_bg.tga"}, // 50
    {"resource/hud/buffclassskill_25s4tr.tga","resource/hud/buffclassskill_damage_number.tga","resource/hud/buffclassskill_25s4tr_bg.tga"}, // 51
    {"resource/hud/buffclassskill_25s4ct.tga","resource/hud/buffclassskill_heal_number.tga","resource/hud/buffclassskill_25s4ct_bg.tga"}, // 52
    {"resource/hud/buffclassskill_25s5ct.tga","resource/hud/buffclassskill_heal_number.tga","resource/hud/buffclassskill_25s5ct_bg.tga"}, // 53
    {"resource/hud/buffclassskill_26s2ct.tga","resource/hud/buffclassskill_heal_number.tga","resource/hud/buffclassskill_26s2ct_bg.tga"}, // 54
    {"resource/hud/buffclassskill_26s3ct.tga","resource/hud/buffclassskill_heal_number.tga","resource/hud/buffclassskill_26s3ct_bg.tga"}, // 55
}};
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
// Complete original panel closure. Build it from the same table used to draw
// the icon so a newly enabled server skill cannot omit its client download.
namespace detail
{
struct ResourceList { std::array<const char*,55*3+11> paths{}; std::size_t count{}; };
inline constexpr auto AllResources = []
{
    ResourceList result;
    const auto add = [&](const char* path)
    {
        if (!path) return;
        for (std::size_t i = 0; i < result.count; ++i)
            if (std::string_view(result.paths[i]) == path) return;
        result.paths[result.count++] = path;
    };
    for (const auto& type : ResourcesByType)
    { add(type.icon); add(type.key); add(type.background); }
    add(ReadyHighlight); add(NumberHighlight);
    for (const auto* image : NumberImages) add(image);
    return result;
}();
}
inline constexpr std::span<const char* const> DownloadResources{
    detail::AllResources.paths.data(),detail::AllResources.count};

enum class Image { Background,Icon,Key,Ready,NumberFlash,Number };
struct Quad { Image image{};int x{},y{},width{77},height{75};float v0{},v1{1};int alpha{255}; };
struct Frame { std::array<Quad,5> quads{};unsigned count{}; };
class State
{
public:
    void Reset();
    void Apply(const Message&);
    bool Restore(const Snapshot&);
    void NotifyReady();
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
