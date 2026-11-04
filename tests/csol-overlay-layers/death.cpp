// SPDX-License-Identifier: GPL-3.0-or-later
// Exact production visibility/reset functions and the WeaponRedraw death prefix.
// Real SDK view/message types. Other reset/render stages are controlled counters.
#include "native/csol/client/runtime.hpp"
#include "native/csol/client/crosshair.hpp"
#include "native/csol/weapon_slots.hpp"
#include <cstdio>
#include <cstring>
#include <vector>

namespace death {
using namespace csol;
cl_enginefunc_t engine{};
ref_params_t view{};
#include "generated/globals.inc"
unsigned checks{},failures{},inventoryResets{},pickupResets{},moduleResets{},selectionCalls{},resetForwards{};
bool forgot{};
std::vector<int> masks;
void Check(bool ok,const char* what)
{++checks;if(!ok){++failures;std::printf("FAIL %s\n",what);}}
int OriginalHide(const char* name,int size,void* data)
{
    Check(std::strcmp(name,"HideWeapon")==0,"actual chain retains HideWeapon name");
    if(size==1&&data)masks.push_back(*static_cast<unsigned char*>(data));
    return 73;
}
int OriginalReset(const char*,int,void*){++resetForwards;return 91;}
void ResetWeaponInventory(bool value){++inventoryResets;forgot=value;}
void ResetPickupHistory(){++pickupResets;}
void ResetWeaponModules(){++moduleResets;}
void WeaponSelectionVisibility(int value,int intermission)
{++selectionCalls;Check(value==hidden&&!intermission,"selection receives unchanged authoritative hidden mask");}
void ResetWeapons(bool);
#include "generated/death.inc"
void SendHide(unsigned char mask)
{const auto calls=selectionCalls;Check(Hide("HideWeapon",1,&mask)==1&&selectionCalls==calls+1,"valid HideWeapon runs actual consumer exactly once");}
void Run()
{
    oldHide=OriginalHide;oldReset=OriginalReset;
    for(int id:{22,197,220})for(int requested=0;requested<256;++requested)
    {
        nativeDeadHud=false;active=id;view.health=100;view.spectator=0;
        SendHide(static_cast<unsigned char>(requested));
        const int own=ExtendedWeaponId(id)?1:0;
        Check(hidden==requested&&masks.back()==(requested|own),"alive stock/extended keeps all server mask bits");
        view.health=0;RedrawDeathPhase();
        Check(nativeDeadHud&&hidden==requested&&masks.back()==(requested|own|9),"dead non-observer adds only weapons and health locally");
        const auto count=masks.size();RedrawDeathPhase();
        Check(masks.size()==count,"steady death does not resend HideWeapon every frame");
        SendHide(static_cast<unsigned char>(requested^32));
        Check(hidden==(requested^32)&&masks.back()==((requested^32)|own|9),"new server HideWeapon while dead preserves both owners");
        view.spectator=1;RedrawDeathPhase();
        Check(!nativeDeadHud&&masks.back()==((requested^32)|own),"observer removes local death-only bits without clearing server mask");
        view.spectator=0;view.health=-5;RedrawDeathPhase();
        Check(nativeDeadHud&&masks.back()==((requested^32)|own|9),"negative health remains dead outside observer");
        view.health=1;RedrawDeathPhase();
        Check(!nativeDeadHud&&masks.back()==((requested^32)|own)&&hidden==(requested^32),"respawn restores original server and extended mask");
    }
    for(bool forget:{false,true})
    {
        registered=true;ammoType=17;active=197;view.health=0;view.spectator=0;
        SendHide(32);RedrawDeathPhase();
        Check(nativeDeadHud,"reset fixture starts in real dead redraw state");
        const auto inv=inventoryResets,pickup=pickupResets,modules=moduleResets;
        ResetWeapons(forget);
        Check(!nativeDeadHud&&masks.back()==0,"ResetWeapons clears stale death owner before releasing native mask");
        Check(active==0&&hidden==0&&clip==0&&reserve==0&&predictedClip==-1&&predictedTotal==-1,"reset clears prior active weapon and mask state");
        Check(inventoryResets==inv+1&&pickupResets==pickup+1&&moduleResets==modules+1&&forgot==forget,"reset keeps inventory/module lifecycle calls");
        Check(registered==!forget&&ammoType==(forget?-1:17),"forgetRegistration preserves its original distinction");
        RedrawDeathPhase();Check(nativeDeadHud&&masks.back()==9,"still-dead next redraw reacquires only current life suppression");
        view.health=100;RedrawDeathPhase();Check(!nativeDeadHud&&masks.back()==0,"revived next redraw restores native HUD");
    }
    active=197;view.health=0;view.spectator=0;SendHide(64);RedrawDeathPhase();
    const auto before=masks.size();ClearNativeWeaponSelection();
    Check(masks.size()==before+2&&masks[before]==65&&masks.back()==73&&hidden==64,"selection clear restores combined death/extended/server ownership");
    unsigned char mask=4;const auto saved=hidden;const auto calls=selectionCalls;
    Check(Hide("HideWeapon",0,&mask)==73&&hidden==saved&&selectionCalls==calls,"malformed mask forwards without changing state");
    Check(Hide("HideWeapon",1,nullptr)==73&&hidden==saved,"null mask retains prior authoritative state");
    oldHide=nullptr;view.health=100;RedrawDeathPhase();Check(!nativeDeadHud,"missing downstream callback does not retain stale death owner");
    oldHide=OriginalHide;view.health=0;RedrawDeathPhase();
    const auto resets=resetForwards;
    Check(Reset("ResetHUD",0,nullptr)==91&&resetForwards==resets+1&&!nativeDeadHud&&masks.back()==0,"ResetHUD forwards once after clearing death owner");
}
}
int main(){death::Run();std::printf("death HUD ownership: %u checks, %u failures\n",death::checks,death::failures);return death::failures?1:0;}
