// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "phase.hpp"
#include <array>
#include <cmath>
#include <cstdint>

namespace csol::scenario::custom
{
inline constexpr unsigned PhaseIconSize=64;
// Source animation is exported for preview/editing. Only its decoded TGA
// atlas is consumed by the game and belongs in the runtime download closure.
inline constexpr char NightAnimationGif[]="resource/goldcraft/challenge/night_blood_v1.gif";
inline constexpr char NightAnimationAtlas[]="resource/goldcraft/challenge/night_blood_v1.tga";
inline constexpr unsigned NightAnimationFrames=32;
inline constexpr unsigned NightAnimationColumns=8;
inline constexpr unsigned NightAnimationRows=4;
inline constexpr unsigned NightAnimationFrameMs=50;
inline constexpr unsigned NightAnimationWidth=NightAnimationColumns*PhaseIconSize;
inline constexpr unsigned NightAnimationHeight=NightAnimationRows*PhaseIconSize;
inline constexpr unsigned NightFrame(std::uint64_t elapsedMs) noexcept
{return unsigned((elapsedMs/NightAnimationFrameMs)%NightAnimationFrames);}
using PhaseIcon=std::array<std::uint8_t,PhaseIconSize*PhaseIconSize*4>;
// GoldCraft's user-requested phase symbols, not copied original CSOL artwork.
inline PhaseIcon MakePhaseIcon(Phase phase) noexcept
{
    PhaseIcon out{};
    constexpr std::array<std::array<std::uint8_t,3>,3> colors{{{255,213,90},{255,142,73},{163,208,255}}};
    if(phase>=Phase::Ended)return out;
    const auto color=colors[unsigned(phase)];
    for(unsigned y=0;y<PhaseIconSize;++y)for(unsigned x=0;x<PhaseIconSize;++x)
    {
        unsigned coverage{};
        for(unsigned sy=0;sy<2;++sy)for(unsigned sx=0;sx<2;++sx)
        {
            const float a=(float(x)+(float(sx)+.5f)*.5f-32)/30;
            const float b=(float(y)+(float(sy)+.5f)*.5f-32)/30;
            const float radius=a*a+b*b;
            bool filled{};
            if(phase==Phase::Noon)
            {
                const float u=(a+b)*.7071067811865475f,v=(a-b)*.7071067811865475f;
                const auto ray=[](float p,float q){return std::abs(p)<.065f&&std::abs(q)>.55f&&std::abs(q)<.82f;};
                filled=radius<.18f||ray(a,b)||ray(b,a)||ray(u,v)||ray(v,u);
            }
            else if(phase==Phase::Dusk)
                filled=(a*a+(b-.18f)*(b-.18f)<.26f&&b<.12f)||
                    (std::abs(a)<.85f&&std::abs(b-.23f)<.045f)||
                    (std::abs(a)<.62f&&std::abs(b-.47f)<.035f);
            else
                filled=((a+.12f)*(a+.12f)+b*b<.46f&&
                    (a-.22f)*(a-.22f)+(b+.20f)*(b+.20f)>.37f)||
                    (std::abs(a-.60f)+std::abs(b+.43f)<.13f);
            coverage+=filled;
        }
        const auto at=(y*PhaseIconSize+x)*4;
        out[at]=color[0];out[at+1]=color[1];out[at+2]=color[2];out[at+3]=std::uint8_t(coverage*255/4);
    }
    return out;
}
}
