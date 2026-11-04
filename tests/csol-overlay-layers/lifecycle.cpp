// SPDX-License-Identifier: GPL-3.0-or-later
// Full compositor plus actual SDK CVGUI2Extension registration/dispatch class.
// Factory/module lookup and menu painters are controlled boundaries, not UI.
#include "native/csol/client/runtime.hpp"
#include "external/MetaHookSv-20261007/Plugins/VGUI2Extension/src/VGUI2ExtensionInternal.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
const char* GetBaseDirectory(){return "fixture";}
const char* GetCurrentGameLanguage(){return "schinese";}
#include "generated/extension.inc"
#include "native/csol/client/overlay_layers.cpp"

namespace csol::client { metahook_api_t* api{}; }
namespace fixture {
using namespace csol::client;
unsigned checks{},failures{};
void Check(bool ok,const char* what)
{++checks;if(!ok){++failures;std::printf("FAIL %s\n",what);}}
std::string paintTrace,keyTrace;
bool moduleReady=true,factoryReady=true,interfaceReady=true;
unsigned moduleLookups{},factoryLookups{},interfaceLookups{};
struct Extension final:CVGUI2Extension
{
    unsigned registrations{},unregistrations{};
    std::vector<IVGUI2Extension_BaseUICallbacks*> tracked;
    void RegisterBaseUICallbacks(IVGUI2Extension_BaseUICallbacks* p)override
    {++registrations;tracked.push_back(p);CVGUI2Extension::RegisterBaseUICallbacks(p);}
    void UnregisterBaseUICallbacks(IVGUI2Extension_BaseUICallbacks* p)override
    {
        ++unregistrations;
        const auto it=std::find(tracked.begin(),tracked.end(),p);
        Check(it!=tracked.end(),"unregister names an actually registered callback");
        if(it!=tracked.end())tracked.erase(it);
        CVGUI2Extension::UnregisterBaseUICallbacks(p);
    }
} host;
void Wall(){paintTrace+='W';}
void Style(){paintTrace+='H';}
void Skill(){paintTrace+='S';}
void Buy(){paintTrace+='B';}
void Vote(){paintTrace+='V';}
void Requeue(){paintTrace+='R';QueueOverlayPaint(OverlayLayer::WallStatus,Wall);}
struct Producer final:IVGUI2Extension_BaseUICallbacks
{
    int altitude;char key;OverlayLayer layer;void(*painter)();bool handles{};
    Producer(int a,char k,OverlayLayer l,void(*p)()):altitude(a),key(k),layer(l),painter(p){}
    int GetAltitude()const override{return altitude;}
    void Initialize(CreateInterfaceFn*,int)override{}
    void Start(cl_enginefuncs_s*,int)override{}
    void Shutdown()override{}
    void Key_Event(int&,int&,const char*&,VGUI2Extension_CallbackContext* c)override
    {keyTrace+=key;if(handles)c->Result=VGUI2Extension_Result::HANDLED;}
    void CallEngineSurfaceAppProc(void*&,void*&,VGUI2Extension_CallbackContext*)override{}
    void CallEngineSurfaceWndProc(void*&,unsigned int&,unsigned int&,long&,VGUI2Extension_CallbackContext*)override{}
    void Paint(int&,int&,int&,int&,VGUI2Extension_CallbackContext* c)override
    {if(c->IsPost)QueueOverlayPaint(layer,painter);}
    void HideGameUI(VGUI2Extension_CallbackContext*)override{}
    void ActivateGameUI(VGUI2Extension_CallbackContext*)override{}
    void HideConsole(VGUI2Extension_CallbackContext*)override{}
    void ShowConsole(VGUI2Extension_CallbackContext*)override{}
};
void Paint(bool post)
{int x=1,y=2,r=100,b=200;VGUI2Extension_CallbackContext c;c.IsPost=post;host.BaseUI_Paint(x,y,r,b,&c);}
void Keys()
{
    int down=1,key=42;const char* binding="+attack";VGUI2Extension_CallbackContext c;
    host.BaseUI_Key_Event(down,key,binding,&c);
    Check(down==1&&key==42&&std::strcmp(binding,"+attack")==0,"compositor does not mutate key arguments");
}
IBaseInterface* Factory(const char* name,int*)
{
    ++interfaceLookups;Check(std::strcmp(name,VGUI2_EXTENSION_INTERFACE_VERSION)==0,"factory requests matching SDK interface version");
    return interfaceReady?static_cast<IVGUI2Extension*>(&host):nullptr;
}
}
CreateInterfaceFn Sys_GetFactory(HINTERFACEMODULE)
{++fixture::factoryLookups;return fixture::factoryReady?fixture::Factory:nullptr;}

int main()
{
    using namespace fixture;using namespace csol::client;
    metahook_api_t hooks{};api=&hooks;
    hooks.GetPluginModuleHandleByBaseFileName=[](const char* name)->HINTERFACEMODULE
    {++moduleLookups;Check(std::strcmp(name,"VGUI2Extension")==0,"only intended plugin module requested");return moduleReady?reinterpret_cast<HINTERFACEMODULE>(1):nullptr;};
    for(int missing=0;missing<3;++missing)
    {
        moduleReady=missing!=0;factoryReady=missing!=1;interfaceReady=missing!=2;
        InitializeOverlayCompositor();
        Check(!overlay_detail::active&&host.tracked.empty(),"missing module/factory/interface cannot activate compositor");
        paintTrace.clear();QueueOverlayPaint(OverlayLayer::HudStyle,Style);
        Check(paintTrace=="H","missing compositor immediately paints fallback");
        ShutdownOverlayCompositor();
    }
    moduleReady=factoryReady=interfaceReady=true;
    InitializeOverlayCompositor();
    Check(overlay_detail::active&&host.tracked.size()==1,"initialization registers exactly one active compositor");
    const auto loads=moduleLookups,registered=host.registrations;
    for(int i=0;i<4;++i)InitializeOverlayCompositor();
    Check(host.registrations==registered&&moduleLookups==loads,"duplicate Init does not resolve or register twice");
    Check(host.tracked.front()->GetAltitude()==1,"compositor keeps altitude one without altering menu altitude");

    Producer wall(20,'W',OverlayLayer::WallStatus,Wall),style(21,'H',OverlayLayer::HudStyle,Style),
        skill(22,'S',OverlayLayer::CharacterSkills,Skill),buy(31,'B',OverlayLayer::BuyMenu,Buy),vote(35,'V',OverlayLayer::MapVote,Vote);
    for(auto* p:{&wall,&style,&skill,&buy,&vote})host.RegisterBaseUICallbacks(p);
    paintTrace.clear();Paint(false);Check(paintTrace.empty(),"pre-paint does not queue or consume overlays");
    Paint(true);Check(paintTrace=="WHSBV","actual extension dispatcher paints compositor bottom to top");
    keyTrace.clear();Keys();Check(keyTrace=="VBSHW","actual key dispatch retains descending original altitude");
    vote.handles=true;keyTrace.clear();Keys();Check(keyTrace=="V","top menu still owns handled key before lower menus");vote.handles=false;
    for(auto* p:{&wall,&style,&skill,&buy,&vote})host.UnregisterBaseUICallbacks(p);
    paintTrace.clear();QueueOverlayPaint(OverlayLayer::ScenarioAbilities,Requeue);Paint(true);
    Check(paintTrace=="R","painter requeue cannot run in the same pass");
    Paint(true);Check(paintTrace=="RW","requeued paint runs on following actual extension post-pass");
    Paint(true);Check(paintTrace=="RW","empty following pass cannot redraw old queue");

    QueueOverlayPaint(OverlayLayer::HudStyle,Style);
    host.BaseUI_Shutdown();
    Check(!overlay_detail::active,"SDK Shutdown deactivates paint queue");
    paintTrace.clear();Paint(true);Check(paintTrace.empty(),"SDK Shutdown clears pending painters");
    const auto suspended=host.registrations;
    InitializeOverlayCompositor();
    Check(overlay_detail::active&&host.registrations==suspended&&host.tracked.size()==1,
        "Init after SDK Shutdown reactivates retained callback without duplicate registration");
    host.BaseUI_Shutdown();
    const auto before=host.unregistrations;
    ShutdownOverlayCompositor();
    Check(host.unregistrations==before+1&&host.tracked.empty(),"explicit shutdown after SDK callback unregisters retained compositor");
    const auto stopped=host.unregistrations;ShutdownOverlayCompositor();
    Check(host.unregistrations==stopped,"repeated explicit Shutdown is idempotent");
    InitializeOverlayCompositor();
    Check(overlay_detail::active&&host.tracked.size()==1,"reinitialization after shutdown owns exactly one callback");
    paintTrace.clear();QueueOverlayPaint(OverlayLayer::ScenarioAbilities,Requeue);Paint(true);
    Check(paintTrace=="R","no duplicate compositor consumes next-frame requeue in same pass");
    Paint(true);Check(paintTrace=="RW","next-frame queue remains intact after lifecycle restart");
    ShutdownOverlayCompositor();
    Check(host.tracked.empty(),"normal explicit shutdown unregisters compositor");
    paintTrace.clear();QueueOverlayPaint(OverlayLayer::HudStyle,Style);
    Check(paintTrace=="H","disabled compositor restores immediate fallback without stale queue");
    InitializeOverlayCompositor();QueueOverlayPaint(OverlayLayer::HudStyle,Style);ShutdownOverlayCompositor();
    InitializeOverlayCompositor();paintTrace.clear();Paint(true);
    Check(paintTrace.empty(),"normal shutdown and reinit discard queued old-session paint");
    ShutdownOverlayCompositor();
    std::printf("overlay SDK lifecycle: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
