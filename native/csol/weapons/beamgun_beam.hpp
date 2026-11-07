// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace csol
{
using BeamPoint = std::array<float, 3>;
struct BeamVertex { BeamPoint position; float u{}, v{}, shade{}; };

// CSO's zero-noise beam: flags 0x100 stretch the sprite, 0x40 shade the
// source. Preserve the original segment count and 1/count texture increment.
template<class Project, class Emit>
void EmitBeamStrip(const BeamPoint& start, const BeamPoint& end, float width,
    const BeamPoint& right, const BeamPoint& up, Project project, Emit emit)
{
    BeamPoint delta{};
    float squared = 0;
    for(int n=0;n<3;++n){delta[n]=end[n]-start[n];squared+=delta[n]*delta[n];}
    const float length=std::sqrt(squared);
    if(!std::isfinite(length) || length<=0)return;
    const int count=std::clamp(int(length*.075+3),2,128);
    const float step=1.f/(count-1), textureStep=1.f/count;
    BeamPoint previous=start, previousNormal{};
    auto previousScreen=project(previous);
    float texture=0;
    for(int i=1;i<count;++i)
    {
        const float fraction=i*step;
        BeamPoint point{};
        for(int n=0;n<3;++n)point[n]=start[n]+delta[n]*fraction;
        const auto screen=project(point);
        const float dx=screen[0]-previousScreen[0],dy=screen[1]-previousScreen[1];
        const float screenLength=float(std::sqrt(double(dx*dx+dy*dy)));
        BeamPoint normal{};
        if(screenLength>0 && std::isfinite(screenLength))
        {
            const float inverse=1.f/screenLength;
            const float nx=dx*inverse,ny=dy*inverse;
            for(int n=0;n<3;++n)normal[n]=width*(up[n]*nx-right[n]*ny);
        }
        if(i==1)previousNormal=normal;
        const auto vertex=[&](const BeamPoint& center,const BeamPoint& offset,float side,float v,float shade){
            BeamVertex result{};
            for(int n=0;n<3;++n)result.position[n]=center[n]+side*offset[n];
            result.u=side>0?0.f:1.f;result.v=v;result.shade=shade;emit(result);
        };
        vertex(previous,previousNormal,1,texture,(i-1)*step);
        vertex(previous,previousNormal,-1,texture,(i-1)*step);
        texture+=textureStep;
        vertex(point,normal,-1,texture,fraction);
        vertex(point,normal,1,texture,fraction);
        previous=point;previousNormal=normal;previousScreen=screen;
    }
}
}
