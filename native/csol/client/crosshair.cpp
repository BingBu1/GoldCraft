// SPDX-License-Identifier: GPL-3.0-or-later
#include "crosshair.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace csol::crosshair
{
int VirtualWidth(const char* size,int width)
{
    if(!size)size="auto";
    const int value=std::atoi(size);
    if(!std::strcmp(size,"small") || value==1)return 1024;
    if(!std::strcmp(size,"medium") || value==2)return 800;
    if(!std::strcmp(size,"large") || value==3)return 640;
    return width<=640?1024:width>=1024?640:800;
}
int BaseGap(const Parameters& p,int flags,float speed,bool dynamic)
{
    int gap=p.gap;
    if(!dynamic)return gap;
    if(!(flags&0x200) && (p.flags&1))gap*=2;
    else if((flags&0x4000) && (p.flags&4))gap=int(gap*.5f);
    else if(int(speed)>p.speed && (p.flags&2))gap=int(gap*1.5f);
    if(p.flags&8)gap=int(gap*1.4f);
    if(p.flags&16)gap=int(gap*1.4f);
    if(p.flags&32)gap=int(gap*.5f);
    return gap;
}
void Advance(State& state,const Parameters& p,int flags,float speed,bool dynamic)
{
    if(state.shot){state.gap=std::min(15.f,state.gap+p.shotDelta);state.alpha=std::max(120,state.alpha-40);}
    else {state.gap=float(double(state.gap)-(double(state.gap)*.013+.1));state.alpha=std::min(255,state.alpha+2);}
    state.gap=std::max(float(BaseGap(p,flags,speed,dynamic)),state.gap);
    state.shot=false;
}
void Draw(const State& state,int baseGap,const Style& s,Fill fill)
{
    if(!fill || s.width<=0 || s.height<=0 || s.virtualWidth<=0)return;
    const int length=(5-int((state.gap-baseGap)*-.5f))*s.width/s.virtualWidth;
    const float distance=state.gap*s.width/s.virtualWidth;
    const int gap=int(distance),x=s.width/2,y=s.height/2;
    const int type=s.type>=0 && s.type<=4?s.type:0;
    const auto rect=[&](int a,int b,int w,int h){fill(a,b,w,h,s.r,s.g,s.b,state.alpha);};
    if(type==2 || type==3)
    {
        const float radius=distance+length/2+(s.virtualWidth==1024?.5f:s.virtualWidth==800?.8f:.1f);
        const int end=int(float(std::sin(0.7853981852531433))*radius+.5f);
        const float square=radius*radius;
        for(int i=0;i<=end;++i)
        {
            const int j=int(std::sqrt(double(square)-i*i));
            rect(x+i,y+j,1,1);rect(x+i,y-j,1,1);rect(x-i,y+j,1,1);rect(x-i,y-j,1,1);
            rect(x+j,y+i,1,1);rect(x+j,y-i,1,1);rect(x-j,y+i,1,1);rect(x-j,y-i,1,1);
        }
    }
    if(type==3)rect(x-1,y-1,3,3);
    if(type==0 || type==1 || type==3)
    {
        rect(x-gap-length+1,y,length,1);rect(x+gap,y,length,1);
        rect(x,y-gap-length,1,length);rect(x,y+gap,1,length);
    }
    if(type!=0 && type!=3)rect(x-1,y-1,3,3);
}
}
