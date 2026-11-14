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
    // Keep authored controls outside both actual skill positions (77x75 at
    // center-38) and the original centered330px buy-hint row. Reserving both
    // avoids shifting these controls when the native panel is raised.
    const int x=width/2+165+padding;
    const int w=std::min(width-x-padding,int(280*scale));
    if(!std::isfinite(hudScale)||hudScale<=0)hudScale=1;
    const float occupiedScale=std::min(std::min(float(width)/640,float(height)/360),std::clamp(hudScale,.5f,2.f));
    const int h=int(46*scale),kickY=int(height-149*occupiedScale)-padding-26;
    const int y=kickY-padding-h;
    if(w<1||y<padding||kickY+26>height-padding)return {};
    return {x,y,w,h,padding,std::max(5,int(8*scale)),std::clamp(int(14*scale+1)/2*2,12,28),kickY};
}
}
