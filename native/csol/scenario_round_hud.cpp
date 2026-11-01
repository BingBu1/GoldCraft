// SPDX-License-Identifier: GPL-3.0-or-later
#include "scenario_round_hud.hpp"
#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <limits>

namespace csol::scenario_hud
{
namespace
{
unsigned Byte(std::byte value){return std::to_integer<unsigned>(value);}
unsigned Word(std::span<const std::byte> b,unsigned at){return Byte(b[at])|(Byte(b[at+1])<<8);}
}
ParseResult DecodeMP(std::span<const std::byte> b,Message& out)
{
    if(b.empty()||Byte(b[0])!=27)return ParseResult::Unrelated;
    if(b.size()<2)return ParseResult::Malformed;
    Message m;const auto op=Byte(b[1]);
    if(op==3)
    {
        if(b.size()!=8)return ParseResult::Malformed;
        m.kind=Kind::Round;m.value=Byte(b[2]);m.total=Byte(b[3]);
        m.elapsed=std::bit_cast<float>(Word(b,4)|(Word(b,6)<<16));
        if(!std::isfinite(m.elapsed))return ParseResult::Malformed;
    }
    else if(op==7)
    {
        if(b.size()!=4)return ParseResult::Malformed;
        m.kind=Kind::Dialogue;m.value=Word(b,2);
    }
    else if(op==25)
    {
        if(b.size()!=4)return ParseResult::Malformed;
        m.kind=Kind::TimerOffset;
        m.secondsOffset=std::bit_cast<std::int16_t>(std::uint16_t(Word(b,2)));
    }
    else if(op==2||op==8)
    {
        if(b.size()!=(op==2?5u:3u)||Byte(b[2])<1||Byte(b[2])>32)return ParseResult::Malformed;
        m.kind=op==2?Kind::Frags:Kind::ResetFrags;m.player=Byte(b[2]);
        if(op==2)m.value=Word(b,3);
    }
    else return ParseResult::Unrelated;
    out=m;return ParseResult::Accepted;
}
ParseResult DecodeValueSync(std::span<const std::byte> b,Message& out)
{
    // Original mode15/17 registration2A5C216: key6/player0 writes the CT
    // cumulative counter3C4BB34. It survives player departure and phases.
    if(b.empty()||Byte(b[0])!=6)return ParseResult::Unrelated;
    if(b.size()!=4||Byte(b[1])!=0)return ParseResult::Malformed;
    out={Kind::TeamKills,0,Word(b,2)};return ParseResult::Accepted;
}
void State::Reset(){*this={};}
void State::ClearPresentation(){notices_={};imageNotices_={};}
bool State::Apply(const Message& m,float now)
{
    if(!std::isfinite(now)||now<0)return false;
    switch(m.kind)
    {
    case Kind::Round:
    {
        if(m.value>255||m.total>255||!std::isfinite(m.elapsed))return false;
        const float started=now-m.elapsed;
        if(!std::isfinite(started))return false;
        round_=m.value;totalRounds_=m.total;
        // Match the original 27/3 receiver, including its negative-epoch
        // fallback. Separate cumulative seconds arrive in another submessage.
        started_=started<0?now:started;
        active_=true;break;
    }
    case Kind::TeamKills:if(m.value>65535)return false;teamKills_=m.value;break;
    case Kind::TimerOffset:
        if(m.secondsOffset<-32768||m.secondsOffset>32767)return false;
        secondsOffset_=m.secondsOffset;break;
    case Kind::Frags:case Kind::ResetFrags:
        if(m.player<1||m.player>32||m.value>65535)return false;
        frags_[m.player]=m.kind==Kind::Frags?m.value:0;break;
    case Kind::Dialogue:return false;
    }
    return true;
}
bool State::Show(const scenario::ScenarioMessage& message,float now)
{
    const int channel=message.textType==-1?1:message.textType;
    if(channel<0||channel>=int(notices_.size())||!std::isfinite(now)||now<0)return false;
    for(float v:{message.fadeIn,message.show,message.fadeOut,message.flickerTime,message.xCenterRatio,message.yCenterRatio})
        if(!std::isfinite(v))return false;
    const auto& defaults=TextChannels[channel];TextNotice n;
    n.active=true;n.id=message.id;n.born=now;n.color=message.color;
    n.fadeIn=message.fadeIn==-1?defaults.fadeIn:message.fadeIn==0?.001f:message.fadeIn;
    const auto show=message.fadeIn==-1?defaults.show:message.show;
    n.fadeOut=message.fadeIn==-1?defaults.fadeOut:message.fadeOut;
    n.flicker=message.flickerTime==-1?defaults.flicker:message.flickerTime;
    n.x=message.xCenterRatio<0?defaults.x:message.xCenterRatio;
    n.y=message.yCenterRatio<0?defaults.y:message.yCenterRatio;
    if(n.fadeIn<0||show<0||n.fadeOut<0)return false;
    n.fadeEnd=now+n.fadeIn;n.showEnd=n.fadeEnd+show;n.die=n.showEnd+n.fadeOut;
    if(!std::isfinite(n.die))return false;
    notices_[channel]=n;return true;
}
bool State::ShowImage(const scenario::ScenarioMessage& message,float now)
{
    if(!std::isfinite(now)||now<0||message.image.empty())return false;
    const auto channel=std::find_if(ImageChannels.begin(),ImageChannels.end(),
        [&](const auto& value){return value.channel-10==message.type;});
    if(channel==ImageChannels.end())return false;
    TextNotice n;
    n.active=true;n.id=message.id;n.born=now;n.flicker=-1;
    n.fadeIn=message.fadeIn==-1?.5f:message.fadeIn==0?.001f:message.fadeIn;
    const float show=message.fadeIn==-1?3.0f:message.show;
    n.fadeOut=message.fadeIn==-1?.5f:message.fadeOut;
    for(float value:{n.fadeIn,show,n.fadeOut})
        if(!std::isfinite(value)||value<0)return false;
    n.fadeEnd=now+n.fadeIn;n.showEnd=n.fadeEnd+show;n.die=n.showEnd+n.fadeOut;
    if(!std::isfinite(n.die))return false;
    imageNotices_[channel-ImageChannels.begin()]=n;
    return true;
}
ImageRect DialogueImageRect(unsigned channel,int width,int height)
{
    if(channel>=ImageChannels.size()||width<=0||height<=0)return {};
    const auto& image=ImageChannels[channel];
    const float sx=float(width)*(1.0f/1024.0f),sy=float(height)*(1.0f/768.0f);
    // The original integer center adjustment happens before scaling (571/2
    // truncates to 285); do not center the already-scaled rectangle instead.
    return {int(float(image.x-image.width/2)*sx),int(float(image.y-image.height/2)*sy),
        int(float(image.width)*sx),int(float(image.height)*sy)};
}
int NoticeAlpha(const TextNotice& n,float now)
{
    if(!n.active||!std::isfinite(now)||now<n.born||now>=n.die)return 0;
    // Original029B8C25: keep the separate float operations and the original
    // non-binary flicker ramp. It is not a general on/off blink.
    if(now<n.fadeEnd)return n.fadeIn>0?std::clamp(int(255.0f-(n.fadeEnd-now)/n.fadeIn*255.0f),0,255):255;
    if(now>=n.showEnd)return n.fadeOut>0?std::clamp(int((n.die-now)/n.fadeOut*255.0f),0,255):0;
    if(n.flicker>1.1920928955078125e-7f)
    {
        float remainder=float(std::fmod(double(now-n.fadeEnd),double(n.flicker)));
        // Truncation is part of CSO's ramp, even for periods above one second.
        remainder-=std::trunc(remainder);
        return std::clamp(int(std::min(1.0f,remainder+n.flicker*.25f)*255.0f),0,255);
    }
    return 255;
}
Frame State::Layout(int width,int player,float now)const
{
    Frame f;if(!active_||width<=0||!std::isfinite(now))return f;
    const int x=(width-333)/2;
    const auto quad=[&](Image image,int dx,int y,int w,int h,float u0=0,float u1=1)
    {if(f.count<f.quads.size())f.quads[f.count++]={image,x+dx,y,w,h,255,u0,u1};};
    quad(Image::Background,0,0,333,83);quad(Image::TotalTitle,12,9,104,10);
    quad(Image::PersonalTitle,217,9,63,10);quad(Image::RoundTitle,130,15,70,10);
    const auto digits=[&](unsigned value,Image image,int left,int y,int cellWidth,int cellHeight,int rectWidth,int align)
    {
        std::array<unsigned,10> list{};unsigned n=0;
        do{list[n++]=value%10;value/=10;}while(value&&n<list.size());
        n=std::max(2u,n);const int size=int(n)*(cellWidth+2)-2;
        if(align==2)left+=(rectWidth-size)/2;else if(align==3)left+=rectWidth-size;
        for(unsigned i=n;i>0;--i){quad(image,left,y,cellWidth,cellHeight,list[i-1]/10.0f,(list[i-1]+1)/10.0f);left+=cellWidth+2;}
    };
    digits(teamKills_,Image::Digits,20,25,14,22,94,3);
    digits(player>=1&&player<=32?frags_[player]:0,Image::Digits,219,25,14,22,94,1);
    digits(round_,Image::RoundDigits,148,31,11,13,37,2);
    const float elapsed=now-started_;
    // 02822C80: cvttss2si, then a signed-short carry from 27/25. Preserve
    // integer wrap and the original negative timer path without C++ overflow.
    const auto whole=std::isfinite(elapsed)&&double(elapsed)>=-2147483648.0&&double(elapsed)<2147483648.0?
        std::int32_t(elapsed):std::numeric_limits<std::int32_t>::min();
    f.clock=true;
    f.seconds=std::bit_cast<std::int32_t>(std::uint32_t(whole)+std::uint32_t(secondsOffset_));
    // Original mode15 RoundTimer, 0282337F: MM:SS, expanding to HH:MM:SS.
    // Each digit group has a minimum of two digits, with fixed pixel spacing.
    const auto hours=f.seconds/3600,minutes=f.seconds/60%60,seconds=f.seconds%60;
    const int timerLeft=(width-(hours?68:43))/2-x;
    const auto timerDigits=[&](std::int32_t value,int offset)
    {
        char text[12];
        const auto converted=std::to_chars(text,text+sizeof(text),value);
        int left=timerLeft+offset;
        const auto digit=[&](int index)
        {quad(Image::TimerDigits,left,59,8,12,float(index)/10.0f,float(index+1)/10.0f);left+=10;};
        if(converted.ptr-text<2)digit(0);
        // The original TGA number renderer maps every formatted character by
        // char-'0', including '-' (-3). Do not invent a minus sprite/clamp.
        for(const char* p=text;p!=converted.ptr;++p)digit(*p-'0');
    };
    if(hours)
    {
        timerDigits(hours,0);
        quad(Image::TimerColon,timerLeft+20,60,3,10);
        timerDigits(minutes,25);
        quad(Image::TimerColon,timerLeft+45,60,3,10);
        timerDigits(seconds,50);
    }
    else
    {
        timerDigits(minutes,0);
        quad(Image::TimerColon,timerLeft+20,60,3,10);
        timerDigits(seconds,25);
    }
    return f;
}
}
