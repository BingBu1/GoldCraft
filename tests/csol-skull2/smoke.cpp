// SPDX-License-Identifier: GPL-3.0-or-later
#define SKULL2_CLIENT_FIXTURE_NO_MAIN
#include "client.cpp"
#include "../../native/csol/client/skull1.hpp"

int main(int argc,char** argv)
{
    using namespace fixture;
    if(argc!=2)return 2;
    const int selection=std::atoi(argv[1]);
    if(selection<0||selection>5)return 2;
    Setup(false);
    fx.R_TempModel=[](float*,float*,float*,float,int,int)->TEMPENTITY*{return nullptr;};
    using Init=void(*)();
    constexpr Init initialize[]{InitializeSkull1,InitializeSkull2,InitializeSkull5};
    constexpr int permutations[][3]{{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
    const auto& order=permutations[selection];
    for(int index:order)initialize[index]();
    Check(!GunSmokeEnabled(),"fresh process original byte defaults off in every initialization order");
    const auto shot=[&](int id,const char* event,bool localEvent,bool enabled){
        fixtureActiveId=id;auto args=Args(localEvent);args.iparam2=7;
        const auto count=smokeCount;hooks.at(event)(&args);
        Check(smokeCount==count+(localEvent&&enabled?3:0),"original shared byte gates actual weapon smoke");
        if(localEvent&&enabled)for(unsigned i=count;i<smokeCount;++i)
            Check(smoke[i].entity.curstate.rendermode==kRenderTransAdd,"smoke remains additive");
    };
    const auto all=[&](bool enabled){
        for(bool localEvent:{false,true}){
            shot(130,"events/skull1.sc",localEvent,enabled);
            shot(130,"events/skull1_rapid.sc",localEvent,enabled);
            shot(312,"events/skull2_a.sc",localEvent,enabled);
            shot(312,"events/skull2_left.sc",localEvent,enabled);
            shot(312,"events/skull2_right.sc",localEvent,enabled);
            shot(112,"events/skull5.sc",localEvent,enabled);
        }
    };
    all(false);
    commands.at("gunsmoke")();
    Check(GunSmokeEnabled()&&oldSmokeCalls==1,"one toggle and preserved native handler");
    all(true);
    for(int i=2;i>=0;--i)initialize[order[i]]();
    InitializeWeaponModules();InitializeWeaponModules();
    Check(GunSmokeEnabled(),"HUD initialization never resets user choice");
    commands.at("gunsmoke")();
    Check(!GunSmokeEnabled()&&oldSmokeCalls==2,"repeat initialization never doubles or recurses command");
    all(false);
    commands.at("gunsmoke")();
    Check(GunSmokeEnabled()&&oldSmokeCalls==3,"all modules still share one command after reinitialization");
    all(true);
    ShutdownSharedRandom();
    std::cout<<"three weapon gunsmoke order "<<selection<<": "<<checks<<" checks, "<<failures<<" failures\n";
    return failures?1:0;
}
