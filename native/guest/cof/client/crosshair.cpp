// SPDX-License-Identifier: GPL-3.0-or-later
#include "crosshair.hpp"
#pragma clang fp contract(off)
namespace guest::cof::client
{
bool PistolCrosshair(const CrosshairFrame& frame,std::array<CrosshairRect,3>& out) noexcept
{
    if(frame.width<=0||frame.height<=0||(frame.hidden&0xD)||!frame.hudActive||!frame.hasWeapon||
       frame.aiming||frame.telescope||frame.enabled==0||frame.scalar>10)return false;
    if(frame.originalWeapon!=3&&frame.originalWeapon!=7&&frame.originalWeapon!=19&&frame.originalWeapon!=21)return false;
    const int x=frame.width/2,y=frame.height/2,length=frame.width/100;
    const float gap=static_cast<float>(frame.scalar)*3.0F;
    const float horizontal=static_cast<float>(x),vertical=static_cast<float>(y);
    out={CrosshairRect{int(horizontal+gap),y,length,1},
         CrosshairRect{int(horizontal-(static_cast<float>(length)+gap)),y,length,1},
         CrosshairRect{x-1,int(vertical+gap),2,length}};
    return true;
}
}
