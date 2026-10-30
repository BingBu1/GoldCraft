// SPDX-License-Identifier: GPL-3.0-or-later
#include "scenario_abilities.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace csol::abilities
{
namespace
{
constexpr std::array<Resources,55> resources{{
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
}
const Resources* ResourceFor(unsigned type)
{return type>=1&&type<=resources.size()?&resources[type-1]:nullptr;}
ParseResult Decode(std::span<const std::byte> bytes,Message& message)
{
    if(bytes.empty())return ParseResult::Unrelated;
    const auto type=std::to_integer<unsigned>(bytes[0]);
    if(type<0x24||type>0x26)return ParseResult::Unrelated;
    if(bytes.size()!=(type==0x25?7:11))return ParseResult::Malformed;
    const auto real=[&](unsigned at){
        std::uint32_t bits{};
        for(unsigned i=0;i<4;++i)bits|=std::to_integer<std::uint32_t>(bytes[at+i])<<(i*8);
        return std::bit_cast<float>(bits);
    };
    const auto word=std::uint16_t(std::to_integer<unsigned>(bytes[1])|(std::to_integer<unsigned>(bytes[2])<<8));
    Message result;
    result.operation=type==0x24?Operation::Cooldown:type==0x25?Operation::Number:Operation::Progress;
    result.type=type==0x25?int(std::bit_cast<std::int16_t>(word)):int(word);
    result.value=type==0x25?0:real(3);
    result.duration=real(type==0x25?3:7);
    if(!std::isfinite(result.value)||!std::isfinite(result.duration))return ParseResult::Malformed;
    message=result;return ParseResult::Accepted;
}
void State::Reset(){*this={};}
ParseResult DecodeSnapshot(std::span<const std::byte> bytes,Snapshot& output)
{
    if(bytes.size()!=15||bytes[0]!=std::byte{1})return ParseResult::Malformed;
    const auto real=[&](unsigned offset){
        std::uint32_t bits{};
        for(unsigned i=0;i<4;++i)bits|=std::to_integer<std::uint32_t>(bytes[offset+i])<<(8*i);
        return std::bit_cast<float>(bits);
    };
    Snapshot value{std::to_integer<unsigned>(bytes[1])|(std::to_integer<unsigned>(bytes[2])<<8),
        real(3),real(7),real(11)};
    State check;
    if(!check.Restore(value))return ParseResult::Malformed;
    output=value;return ParseResult::Accepted;
}
bool State::Restore(const Snapshot& value)
{
    if(!std::isfinite(value.cooldown)||!std::isfinite(value.elapsed)||!std::isfinite(value.duration)||
        value.cooldown<0||value.elapsed<0||value.elapsed>value.cooldown||value.duration<0||
        (value.type&&!ResourceFor(value.type))||
        (!value.type&&(value.cooldown!=0||value.elapsed!=0||value.duration!=0)))return false;
    Reset();
    if(!value.type||value.cooldown<0.001f)return true;
    type_=value.type;visible_=true;cooldown_=value.cooldown;
    elapsed_=value.elapsed;notificationTime_=value.duration;
    fraction_=elapsed_/cooldown_;readyArmed_=fraction_<=0.99f;
    // Restoring a ready icon is not a new cooldown-complete notification.
    return true;
}
void State::Hide()
{
    visible_=false;
    cooldown_=elapsed_=notificationTime_=std::numeric_limits<float>::max();
}
void State::Apply(const Message& message)
{
    if(!std::isfinite(message.value)||!std::isfinite(message.duration))return;
    if(message.operation==Operation::Number)
    {
        number_=message.type;numberTime_=message.duration;
        if(flashes_<=0){numberFlash_=true;drawnAlpha_=0;flashes_=1;}
        return;
    }
    // The original message consumer hides the panel below 0.001, including
    // zero. Cooldown and externally supplied progress are separate messages.
    if(message.value<0.001f){Hide();return;}
    visible_=true;type_=unsigned(message.type);number_=0;
    cooldown_=message.operation==Operation::Cooldown?message.value:0;
    override_=message.operation==Operation::Progress?message.value:-1;
    elapsed_=0;notificationTime_=message.duration;readyArmed_=true;drawnAlpha_=0;
    fraction_=0;
}
void State::Advance(double delta)
{
    if(!visible_||!std::isfinite(delta)||delta<0)return;
    elapsed_=std::min(float(double(elapsed_)+delta),cooldown_);
    if(elapsed_>notificationTime_)notificationTime_=std::numeric_limits<float>::max();
    numberTime_=float(double(numberTime_)-delta);
    if(numberTime_<=0){numberTime_=0;number_=0;}
    fraction_=override_>=std::numeric_limits<float>::epsilon()?override_:elapsed_/cooldown_;
    fraction_=std::clamp(fraction_,0.0f,1.0f);
    if(readyArmed_&&fraction_>0.99f)
    {readyArmed_=false;flashes_=2;numberFlash_=false;}
    if(flashes_>0)
    {
        // Original OnThink restores child visibility first. The shared flash
        // phase survives SetType/Init; those reset only the drawn alpha.
        flashAlpha_=float(double(flashAlpha_)+double(float(direction_)*800.0f)*delta);
        if(flashAlpha_>255){flashAlpha_=255;direction_=-1;}
        if(flashAlpha_<0){--flashes_;flashAlpha_=0;direction_=1;}
        drawnAlpha_=int(flashAlpha_);
    }
}
Frame State::Layout(int width,int height,bool elevated)const
{
    Frame frame;
    const auto* resource=ResourceFor(type_);
    if(!visible_||!resource||width<=0||height<=0)return frame;
    const int x=width/2-38,y=height-(elevated?261:166);
    const int upper=int((1.0f-fraction_)*75.0f),lower=int(fraction_*75.0f);
    const auto add=[&](Image image,int top,int rows,int alpha=255){
        auto& q=frame.quads[frame.count++];q.image=image;q.x=x;q.y=y+top;
        q.height=rows;q.v0=float(top)/75;q.v1=float(top+rows)/75;q.alpha=alpha;
    };
    // Two independently truncated clip rectangles are retained, including the
    // possible one-pixel gap; neither source image is stretched vertically.
    if(lower)add(Image::Background,upper,lower);
    if(upper)add(Image::Icon,0,upper);
    if(drawnAlpha_>0)add(numberFlash_?Image::NumberFlash:Image::Ready,0,75,drawnAlpha_);
    if(resource->key)add(Image::Key,0,75);
    if(number_>=1&&number_<=int(NumberImages.size())&&numberTime_>0)add(Image::Number,0,75);
    return frame;
}
}
