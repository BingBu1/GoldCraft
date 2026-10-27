// SPDX-License-Identifier: GPL-3.0-or-later
#include "client/crosshair.hpp"
#include <iostream>
#include <iomanip>
using namespace csol::crosshair;
static bool first;
void Rectangle(int x,int y,int w,int h,int r,int g,int b,int a)
{
    if(!first)std::cout<<',';first=false;
    std::cout<<'['<<x<<','<<y<<','<<w<<','<<h<<','<<r<<','<<g<<','<<b<<','<<a<<']';
}
int main()
{
    State state;Style style;int flags,dynamic,shot;
    while(std::cin>>state.gap>>state.alpha>>shot>>flags>>dynamic>>style.width>>style.height>>style.virtualWidth>>style.type)
    {
        state.shot=shot!=0;Advance(state,ThrowingKnife,flags,0,dynamic!=0);
        std::cout<<std::setprecision(9)<<"{\"gap\":"<<state.gap<<",\"alpha\":"<<state.alpha<<",\"calls\":[";first=true;
        Draw(state,BaseGap(ThrowingKnife,flags,0,dynamic!=0),style,Rectangle);
        std::cout<<"]}\n";
    }
}
