// SPDX-License-Identifier: GPL-3.0-or-later
#include "scenario_upgrades.hpp"
#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace csol::upgrades
{
ParseResult Decode(std::span<const std::byte> bytes,Message& message)
{
    if(bytes.empty())return ParseResult::Unrelated;
    const unsigned type=std::to_integer<unsigned>(bytes[0]);
    if(type<3||type>15)return ParseResult::Unrelated;
    const bool stat=type>=6&&(type%2)==0;
    if(bytes.size()!=(stat?9:5))return ParseResult::Malformed;
    const auto number=[&](unsigned offset){
        std::uint32_t value{};
        for(unsigned i=0;i<4;++i)value|=std::to_integer<std::uint32_t>(bytes[offset+i])<<(i*8);
        return std::bit_cast<std::int32_t>(value);
    };
    Message result;
    result.value=number(1);
    if(type<6)result.operation=type==3?Operation::Experience:type==4?Operation::Total:Operation::Increase;
    else {result.operation=stat?Operation::Stat:Operation::Limit;result.stat=(type-6)/2;if(stat)result.limit=number(5);}
    message=result;return ParseResult::Accepted;
}
void State::Reset(){*this={};}
void State::ClearPresentation(){notice_=false;}
bool State::Apply(const Message& message,float hudTime,float realTime)
{
    if(!std::isfinite(hudTime)||!std::isfinite(realTime))return false;
    active_=true;
    switch(message.operation)
    {
    case Operation::Experience:
    {
        const float value=float(message.value)/100.0f;
        if(value!=experience_)experienceChanged_=hudTime;
        experience_=value;
        break;
    }
    case Operation::Total:total_=message.value;break;
    case Operation::Increase:
    {
        const auto old=total_;total_=message.value;
        if(total_<=old)break;
        if(!notice_){notice_=true;shownAt_=hudTime;movieStarted_=realTime;}
        return true;
    }
    case Operation::Stat:
        if(message.stat<stats_.size())stats_[message.stat]={message.value,message.limit};
        break;
    case Operation::Limit:
        if(message.stat<stats_.size())stats_[message.stat].limit=message.value;
        break;
    }
    return false;
}
void State::Advance(float hudTime)
{
    if(!std::isfinite(hudTime))return;
    if(notice_&&hudTime>shownAt_+2.0f)notice_=false;
}
Frame State::Layout(int width,int height,float hudTime,float realTime) const
{
    Frame frame;
    if(!active_||width<=0||height<=0)return frame;
    const auto add=[&](Image image,float x,float y,float w,float h)->Quad&{
        auto& q=frame.quads[frame.count++];q.image=image;q.x=x;q.y=y;q.width=w;q.height=h;return q;
    };
    add(Image::LevelBackground,0,float(height-70),100,24);
    auto& label=add(Image::LevelLabel,12,float(height-66),25,14);
    label.u1=25.0f/256;label.v0=1.0f/16;label.v1=15.0f/16;
    char digits[16];const auto number=std::to_chars(digits,digits+sizeof(digits),total_);
    int x=43;
    for(auto it=digits;it!=number.ptr;++it,x+=13)
    {
        // CHud::DrawHudNumber maps a sign/non-digit to cell zero.
        const unsigned digit=*it>='0'&&*it<='9'?unsigned(*it-'0'):0;
        auto& q=add(Image::Digits,float(x),float(height-67),13,18);
        q.u0=float(digit)/10;q.u1=float(digit+1)/10;q.additive=true;
        if(total_==0)q.color={75.0f/255,75.0f/255,75.0f/255,1};
    }
    add(Image::ExperienceBackground,0,float(height-4),float(width),4);
    const float fraction=std::clamp(experience_,0.0f,1.0f);
    auto& bar=add(Image::Experience,0,float(height-4),float(int(float(width)*fraction)),4);
    const float age=hudTime-experienceChanged_;
    if(age>0&&age<0.5f)
    {
        const float green=float(static_cast<unsigned char>(int(age*2.0f*255.0f)))/255.0f;
        bar.color={1,green,green,1};
    }
    if(!notice_)return frame;
    const float y=float(height)*0.7f;
    add(Image::NoticeLeft,0,float(int(y)),128,32);
    add(Image::NoticeCenter,128,float(int(y)),float(width-256),32);
    add(Image::NoticeRight,float(width-128),float(int(y)),128,32);
    // The Bink wrapper (02D185A0) uses frame numbers 2..53, inclusive,
    // stops after the last frame and retains that frame until the panel hides.
    const float ageMovie=std::max(0.0f,realTime-movieStarted_);
    const int movieFrame=ageMovie>=53.0f/30?52:std::clamp(int(ageMovie*30),2,53)-1;
    for(unsigned i=0;i<2;++i)
    {
        auto& q=add(i?Image::MovieBottom:Image::MovieTop,float((width-764)/2),
            float(int(y+(i?32.0f:0.0f)-9.0f)),764,18);
        q.u0=1.0f/384;q.u1=383.0f/384;
        q.v0=float(movieFrame*11+1)/583;q.v1=float(movieFrame*11+10)/583;
    }
    frame.notice=true;frame.textY=int(y);return frame;
}
}
