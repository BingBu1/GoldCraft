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

const Resources* ResourceFor(unsigned type)
{return type>=1&&type<=ResourcesByType.size()?&ResourcesByType[type-1]:nullptr;}
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
void State::NotifyReady()
{
    if(!visible_)return;
    readyArmed_=false;flashes_=2;numberFlash_=false;
}
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
