// SPDX-License-Identifier: GPL-3.0-or-later
#include "scenario_kill.hpp"
#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>

namespace csol::scenario_kill
{
ParseResult DecodeFrags(std::span<const std::byte> bytes,Message& message)
{
    if(bytes.empty() || bytes[0]!=std::byte{27})return ParseResult::Unrelated;
    if(bytes.size()<2)return ParseResult::Malformed;
    if(bytes[1]!=std::byte{2})return ParseResult::Unrelated;
    if(bytes.size()!=5)return ParseResult::Malformed;
    const int player=std::to_integer<unsigned char>(bytes[2]);
    if(player<1 || player>32)return ParseResult::Malformed;
    // Original 02A5B030 reads SHORT, then MOVZX ECX, AX preserves 32768..65000.
    const auto value=std::to_integer<unsigned>(bytes[3]) | (std::to_integer<unsigned>(bytes[4])<<8);
    message={Kind::Frags,player,std::int32_t(value)};
    return ParseResult::Accepted;
}
ParseResult DecodeCombo(std::span<const std::byte> bytes,Message& message)
{
    if(bytes.empty())return ParseResult::Malformed;
    if(bytes[0]!=std::byte{1} && bytes[0]!=std::byte{2})return ParseResult::Unrelated;
    if(bytes.size()!=5)return ParseResult::Malformed;
    std::uint32_t value{};
    for(unsigned i=0;i<4;++i)value|=std::to_integer<std::uint32_t>(bytes[i+1])<<(i*8);
    const auto signedValue=std::bit_cast<std::int32_t>(value);
    // Negative counters have no digit image in the original ten-cell atlas.
    if(signedValue<0)return ParseResult::Malformed;
    message={bytes[0]==std::byte{1}?Kind::Combo:Kind::HeadHits,0,signedValue};
    return ParseResult::Accepted;
}
Animation Animate(float now,float born,int height)
{
    if(!std::isfinite(now)||!std::isfinite(born)||height<=0||height>16384)return {};
    const float elapsed=std::max(0.0f,now-born);
    const float phase=elapsed+elapsed;
    if(phase>=1.0f)return {};
    // Preserve both CVTTSS2SI operations from client 0284C206.
    const int alpha=int(255.0f-255.0f*phase);
    if(alpha<=0)return {};
    return {alpha,int(float(int((float(height)/768.0f)*-20.0f))*phase)};
}
void State::Reset(){*this=State{};}
void State::ClearPresentation()
{
    comboPending_=headPending_=false;
    killVisible_=comboVisible_=perfectVisible_=false;
    previous_=frags_[localPlayer_];
}
void State::Apply(const Message& message)
{
    if(message.kind==Kind::Frags)
    {
        if(message.player>=1&&message.player<=32)frags_[message.player]=message.value;
    }
    else if(message.value>=0)
    {
        if(message.kind==Kind::Combo){combo_=message.value;comboPending_=true;}
        else headPending_=true;
    }
}
void State::Advance(int localPlayer,float now)
{
    if(localPlayer<1||localPlayer>32||!std::isfinite(now))return;
    if(localPlayer_ && localPlayer_!=localPlayer)
    {
        localPlayer_=localPlayer;ClearPresentation();
    }
    localPlayer_=localPlayer;
    time_=now;
    // Original 0284C0D3: head-hit alone is consumed without PERFECT.
    if(headPending_ && comboPending_){perfectVisible_=true;perfectBorn_=now;}
    headPending_=false;
    const auto frags=frags_[localPlayer];
    if(frags>previous_ && !comboPending_){killVisible_=true;killBorn_=now;}
    previous_=frags;
    if(comboPending_){comboVisible_=true;comboBorn_=now;comboPending_=false;}
    if(now-killBorn_>=0.5f)killVisible_=false;
    if(now-comboBorn_>=0.5f)comboVisible_=false;
    if(now-perfectBorn_>=0.5f)perfectVisible_=false;
}
bool State::NeedsResources()const
{
    return comboPending_||headPending_||killVisible_||comboVisible_||perfectVisible_||
        (localPlayer_ && frags_[localPlayer_]>previous_);
}
Frame State::Layout(int width,int height)const
{
    Frame frame;
    if(width<=0||height<=0||width>16384||height>16384)return frame;
    auto rectangle=[&](Image image,float born,int xOffset=0,int yOffset=0)
    {
        const auto& resource=Resources[unsigned(image)];
        const int w=int((float(resource.width)/1024.0f)*float(width));
        const int h=int((float(resource.height)/768.0f)*float(height));
        const int x=int(double(float(width)*0.5f)-double(w)*0.5)+xOffset;
        const int y=int(double(float(height)*0.25f)-double(h)*0.5)+yOffset;
        const auto animation=Animate(time_,born,height);
        return Quad{image,x,y+animation.dy,x+w,y+h+animation.dy,animation.alpha};
    };
    // Same draw order as CHudZombieScenarioVer2: combo, plain, perfect.
    if(comboVisible_)
    {
        auto combo=rectangle(combo_>=30?Image::Combo30:combo_>=20?Image::Combo20:
            combo_>=10?Image::Combo10:Image::Combo,comboBorn_);
        if(combo.alpha>0)
        {
            frame.quads[frame.count++]=combo;
            std::array<char,11> digits;
            const auto result=std::to_chars(digits.data(),digits.data()+digits.size(),combo_);
            if(result.ec==std::errc{})
            {
                const auto count=int(result.ptr-digits.data());
                const float digitWidth=(float(width)/1024.0f)*56.0f;
                const float digitHeight=((float(height)/768.0f)*52.0f)*1.057692289352417f;
                // Numeric panel: right aligned against the left of COMBO;
                // one screen pixel between digits, matching +194 = 1.0f.
                int x=combo.left-int(float(count)*digitWidth+float(count-1));
                for(int i=0;i<count;++i)
                {
                    const int digit=digits[i]-'0';
                    const int right=int(float(x)+digitWidth);
                    frame.quads[frame.count++]={Image::Digits,x,combo.top,right,
                        combo.top+int(digitHeight),combo.alpha,float(digit*57)/569.0f,0,
                        float(digit*57+56)/569.0f,1};
                    x=int(float(x)+(digitWidth+1.0f));
                }
            }
        }
    }
    if(killVisible_)
    {
        const auto quad=rectangle(Image::Kill,killBorn_);
        if(quad.alpha>0)frame.quads[frame.count++]=quad;
    }
    if(perfectVisible_)
    {
        // PERFECT is relative to the COMBO panel, not independently centred.
        const auto combo=rectangle(Image::Combo,perfectBorn_);
        const auto animation=Animate(time_,perfectBorn_,height);
        const int x=combo.left+int((float(width)/1024.0f)*77.0f);
        const int y=combo.top+int((float(height)/768.0f)*-38.0f);
        if(animation.alpha>0)frame.quads[frame.count++]={Image::Perfect,x,y,
            x+int((float(width)/1024.0f)*177.0f),y+int((float(height)/768.0f)*92.0f),animation.alpha};
    }
    return frame;
}
}
