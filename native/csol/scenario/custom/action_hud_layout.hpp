// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>

namespace csol::scenario::custom
{
struct StaminaHudLayout{int x{},y{},width{},height{},padding{},barHeight{},fontHeight{},kickY{};};
inline StaminaHudLayout StaminaLayout(int width,int height,float hudScale=1) noexcept
{
    if(width<320||height<240||width>16384||height>16384)return {};
    const float scale=std::clamp(float(height)/768.f,.8f,2.f);
    const int padding=std::max(6,int(9*scale));
    if(!std::isfinite(hudScale)||hudScale<=0)hudScale=1;
    const float occupiedScale=std::min(std::min(float(width)/640,float(height)/360),std::clamp(hudScale,.5f,2.f));
    const int h=int(46*scale),kickY=height-padding-26;
    int x=width/2+165+padding;
    // User's arrows place the stamina bar along the bottom at its existing
    // right-offset anchor. Leave room for the native secondary-ammo glyphs.
    int w=std::min(int(280*scale),int(width-280*occupiedScale)-padding-x);
    int y=height-padding-h;
    if(w<int(180*scale))
    {
        // A narrow viewport or enlarged original HUD cannot fit both rows
        // along one bottom edge. Keep the stamina panel above its weapon card.
        w=std::min(width-2*padding,int(280*scale));x=width-padding-w;
        y=std::min(int(height-149*occupiedScale),height-188)-padding-h;
    }
    if(w<1||y<padding)return {};
    return {x,y,w,h,padding,std::max(5,int(8*scale)),std::clamp(int(14*scale+1)/2*2,12,28),kickY};
}
}
