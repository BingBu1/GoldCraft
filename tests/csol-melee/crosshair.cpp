// SPDX-License-Identifier: GPL-3.0-or-later
// Exact production DrawCrosshair/Custom and melee descriptor are extracted by
// run_crosshair.py. Engine drawing and weapon lookup are controlled boundaries.
#include "../../native/csol/client/weapons/weapon_module.hpp"
#include "../../native/csol/weapons/melee_identity.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>
namespace csol::client {
cl_enginefunc_t engine{};
ref_params_t view{};
void InitializeMeleeWeapons(){}
void ResetMeleeWeapons(){}
void ReceiveMeleeState(const WeaponCurrent&){}
bool PredictMeleeWeapon(local_state_t*,local_state_t*,usercmd_t*,int,double,unsigned){return false;}
void MeleeDiagnostics(std::ostream&){}
#include "actual_melee_module.inc"
namespace {
constexpr int KnifeId=ThrowingKnifeId;
bool registered=true,nightVision=false,known=true;
int active{},hidden{},fieldOfView=90,playerFlags=0x200,crosshairDraws{};
float playerSpeed{},hud=1;
SCREENINFO screen{};
crosshair::State crosshairState{};
std::vector<std::array<int,8>> rectangles;
void Fill(int x,int y,int w,int h,int r,int g,int b,int a){rectangles.push_back({x,y,w,h,r,g,b,a});}
decltype(engine.pfnFillRGBA) originalFill=Fill;
WeaponDefinition definition{};
const WeaponDefinition* FindWeaponDefinition(int){return known?&definition:nullptr;}
#include "actual_crosshair.inc"
int checks{},failures{};
void Check(bool value,const char* reason){++checks;if(!value){++failures;std::cerr<<reason<<'\n';}}
bool Draw(int id){active=id;rectangles.clear();DrawCrosshair(0);return !rectangles.empty();}
void Run(){
    constexpr int rifleIds[]{197},sniperIds[]{235};
    static const WeaponModule rifle{.ids=rifleIds,.order=1,.ammoKind=hud_style::AmmoKind::Magazine,.crosshair={6,3,0,1}};
    static const WeaponModule sniper{.ids=sniperIds,.order=2,.ammoKind=hud_style::AmmoKind::Magazine,.dynamicCrosshair=false,.crosshair={6,4,0,3}};
    const WeaponModuleRegistration rifleRegistration{rifle},sniperRegistration{sniper};
    engine.pfnGetCvarFloat=[](const char* name){return !std::strcmp(name,"hud_draw")?hud:!std::strcmp(name,"cl_crosshair_translucent")?1.f:0.f;};
    engine.pfnGetCvarString=[](const char* name)->char*{static char size[]="small",color[]="50 250 50",empty[]="";return !std::strcmp(name,"cl_crosshair_size")?size:!std::strcmp(name,"cl_crosshair_color")?color:empty;};
    engine.pfnFillRGBABlend=Fill;screen.iWidth=1024;screen.iHeight=768;view.health=100;
    for(int id:{melee::DragonSwordId,melee::SkullAxeId}){
        const auto* module=FindWeaponModule(id);Check(module && module->ammoKind==hud_style::AmmoKind::None,"melee keeps no ammo HUD");
        Check(module && module->crosshair.gap==7 && module->crosshair.shotDelta==3 && module->crosshair.flags==0 && module->crosshair.speed==0,"actual melee descriptor matches original weapon 29");
        crosshairState={};Check(Draw(id),"melee has crosshair");
        Check(rectangles.size()==4,"melee draws four bars");
        Check(rectangles.size()==4 && rectangles[0]==std::array<int,8>{501,384,5,1,50,250,50,255},"original knife gap and bar position");
        playerFlags=0;crosshairState={};Check(Draw(id)&&crosshairState.gap==7,"knife jumping does not enlarge zero-flag record");playerFlags=0x200;
        for(int mask:{1,4,64,69}){hidden=mask;Check(!Draw(id),"server hide mask suppresses melee");}hidden=0;
        view.health=0;Check(!Draw(id),"dead melee hidden");view.health=100;
        view.spectator=1;Check(!Draw(id),"spectator melee hidden");view.spectator=0;
        fieldOfView=40;Check(!Draw(id),"scoped melee hidden");fieldOfView=90;
        hud=0;Check(!Draw(id),"hud_draw hides melee");hud=1;
        known=false;Check(!Draw(id),"missing definition does not draw");known=true;
        rectangles.clear();DrawCrosshair(1);Check(rectangles.empty(),"intermission hidden");
        Check(Draw(197),"rifle still draws");Check(!Draw(235),"unzoomed sniper stays suppressed");Check(Draw(id),"return from sniper restores melee");
        Check(!Draw(29),"stock knife remains native only");Check(!Draw(28),"stock rifle remains native only");Check(!Draw(1001),"unknown module rejected");
    }
    registered=false;Check(!Draw(KnifeId),"unregistered throwing knife rejected");registered=true;Check(Draw(KnifeId),"throwing knife legacy path preserved");
    std::cout<<"melee crosshair checks="<<checks<<" failures="<<failures<<'\n';
}
}
}
int main(){csol::client::Run();return csol::client::failures?1:0;}
