// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace csol::crosshair
{
struct Parameters { int gap, shotDelta, speed, flags; };
// Original HW weapon record 220: +3c, +40, +5c, +60.
inline constexpr Parameters ThrowingKnife{6, 2, 0, 1};
struct State { float gap{}; int alpha=255; bool shot{}; };
struct Style
{
    int width{}, height{}, virtualWidth=640, type{};
    int r=50, g=250, b=50;
    bool translucent=true;
};
using Fill = void (*)(int,int,int,int,int,int,int,int);
int VirtualWidth(const char* size,int screenWidth);
void Advance(State& state,const Parameters& parameters,int playerFlags,float speed,bool dynamic);
void Draw(const State& state,int baseGap,const Style& style,Fill fill);
int BaseGap(const Parameters& parameters,int playerFlags,float speed,bool dynamic);
}
