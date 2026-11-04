// SPDX-License-Identifier: GPL-3.0-or-later
// A narrow fixture for the real effect implementation. The independent oracle
// executes the supplied CSO client/HW instructions with the same engine inputs.
#include "../../native/csol/client/cannon.cpp"
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <deque>

namespace csol::client {
cl_enginefunc_t engine{};
cl_exportfuncs_t downstream{};
ref_params_t view{};
int CameraIsThirdPerson(){return 0;}
const WeaponDefinition* FindWeaponDefinition(int){return nullptr;}
WeaponModuleRegistration::WeaponModuleRegistration(const WeaponModule&) noexcept {}
}
namespace {
std::deque<TEMPENTITY> particles;
float direction[3], randomFloat[5], sampleTime;
int randomLong[4], floatIndex, longIndex;
int calls, capacity;
void Require(bool ok){if(!ok)std::exit(2);}
void Vector(const float* v){std::cout<<'['<<v[0]<<','<<v[1]<<','<<v[2]<<']';}
}
int main()
{
    using namespace csol::client;
    efx_api_t fx{};event_api_t ev{};
    engine.pEfxAPI=&fx;engine.pEventAPI=&ev;
    engine.pfnAngleVectors=[](const float*,float* f,float* r,float* u){
        std::copy_n(direction,3,f);std::fill_n(r,3,0.f);std::fill_n(u,3,0.f);u[2]=1;
    };
    engine.pfnRandomFloat=[](float lo,float hi)->float{
        const float value=randomFloat[floatIndex++%5];Require(value>=lo&&value<=hi);return value;
    };
    engine.pfnRandomLong=[](int lo,int hi)->int{
        if(lo==0&&hi==15)return 0;
        const int value=randomLong[longIndex++%4];Require(value>=lo&&value<=hi);return value;
    };
    ev.EV_FindModelIndex=[](const char* path)->int{Require(!std::strcmp(path,"sprites/flame_puff01.spr"));return 10;};
    ev.EV_PlaySound=[](int,float*,int,const char*,float,float,int,int){};
    fx.R_DefaultSprite=[](float* origin,int model,float fps)->TEMPENTITY*{
        ++calls;Require(model==10&&fps==20);if(int(particles.size())==capacity)return nullptr;
        particles.emplace_back();auto& p=particles.back();
        // These defaults are separately executed from the original HW allocator
        // by the oracle; the fixture uses the matching host SDK named fields.
        p.flags=FTENT_SPRANIMATE;p.frameMax=22;p.fadeSpeed=.5f;p.bounceFactor=1;
        p.die=float(double(sampleTime)+float(22.f/fps));p.entity.curstate.framerate=fps;
        std::copy_n(origin,3,p.entity.origin);return &p;
    };
    std::cout<<std::setprecision(9);
    float source[3],angles[3]{},velocity[3];int owner,ducking,updates,hit;
    while(std::cin>>sampleTime>>owner>>ducking>>capacity>>updates>>hit)
    {
        for(auto& v:source)std::cin>>v;for(auto& v:direction)std::cin>>v;
        for(auto& v:velocity)std::cin>>v;for(auto& v:randomFloat)std::cin>>v;for(auto& v:randomLong)std::cin>>v;
        particles.clear();calls=floatIndex=longIndex=0;
        Require(FireEffect(owner,ducking!=0,source,angles,velocity));
        std::cout<<"{\"calls\":"<<calls<<",\"particles\":[";
        bool first=true;
        for(auto& p:particles)
        {
            if(!first)std::cout<<',';first=false;
            for(int n=0;n<updates;++n)p.callback(&p,.01f,sampleTime);
            if(hit)p.hitcallback(&p,nullptr);
            std::cout<<"{\"flags\":"<<p.flags<<",\"die\":"<<p.die<<",\"frameMax\":"<<p.frameMax
                <<",\"fadeSpeed\":"<<p.fadeSpeed<<",\"bounceFactor\":"<<p.bounceFactor
                <<",\"frame\":"<<p.entity.curstate.frame<<",\"framerate\":"<<p.entity.curstate.framerate
                <<",\"scale\":"<<p.entity.curstate.scale<<",\"target\":"<<p.entity.prevstate.scale
                <<",\"mode\":"<<p.entity.curstate.rendermode<<",\"amount\":"<<p.entity.curstate.renderamt
                <<",\"baselineAmount\":"<<p.entity.baseline.renderamt
                <<",\"fx\":"<<p.entity.curstate.renderfx<<",\"color\":["<<int(p.entity.curstate.rendercolor.r)<<','<<int(p.entity.curstate.rendercolor.g)<<','<<int(p.entity.curstate.rendercolor.b)
                <<"],\"owner\":"<<p.clientIndex<<",\"origin\":";Vector(p.entity.origin);
            std::cout<<",\"angles\":";Vector(p.entity.angles);std::cout<<",\"angularVelocity\":";Vector(p.entity.baseline.angles);
            std::cout<<",\"velocity\":";Vector(p.entity.baseline.origin);std::cout<<'}';
        }
        std::cout<<"]}\n";
    }
}
