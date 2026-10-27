// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
namespace csol
{
struct BeamScopeQuad { int texture, left, top, right, bottom; float u0=0,v0=0,u1=1,v1=1; };
// Geometry recovered from CHudBeamGunScope::Draw, shared by both variants.
// Textures 0..2 are the three aim layers; 3/4 are the gauge background/fill.
inline int BeamScopeLayout(int width, int height, float ratio, std::array<BeamScopeQuad, 5>& out)
{
    if (width <= 0 || height <= 0) return 0;
    const float scale = height > 768 ? height / 768.f : 1.f;
    const float size = scale * 90;
    const float x = (width-size)*.5f, y = (height-size)*.5f;
    for (int i=0;i<3;++i) out[i]={i,int(x),int(y),int(x+size),int(y+size)};
    if (!std::isfinite(ratio) || ratio <= 0) return 3;
    ratio=std::min(ratio,1.f);
    const float w=66*scale,h=160*scale,gx=width*.5f-w,gy=(height-h)*.5f;
    out[3]={3,int(gx),int(gy),int(gx+w),int(gy+h)};
    // CSO's VGUI UV limits (66/128, 160/256) address a padded texture.
    // UploadImage keeps the actual 66x160 size, so crop in normalized UVs.
    out[4]={4,int(gx),int(gy+(1-ratio)*h),int(gx+w),int(gy+h),0,1-ratio,1,1};
    return 5;
}
}
