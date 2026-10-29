// SPDX-License-Identifier: GPL-3.0-or-later
// Run the real menu controller with host input/UI boundaries replaced.
#include "client/runtime.hpp"
#include <VGUI/VGUI.h>
#include <VGUI/IClientVGUI.h>
#include <iostream>
namespace fixture {
bool focused=true,console=false;
std::vector<int> order;
int checks{},fallbacks{},sent{};
void Check(bool ok){++checks;if(!ok)throw std::runtime_error("buy menu client contract failed");}
HWND WINAPI Foreground(){return nullptr;}
DWORD WINAPI Process(HWND,LPDWORD pid){*pid=focused?GetCurrentProcessId():0;return 1;}
class NativeUI final:public IClientVGUI {
public:
    void Initialize(CreateInterfaceFn*,int)override{}void Start()override{}void SetParent(vgui::VPANEL)override{}
    bool UseVGUI1()override{return false;}void HideScoreBoard()override{}void HideAllVGUIMenu()override{order.push_back(1);}
    void ActivateClientUI()override{}void HideClientUI()override{}void Unknown()override{}void Shutdown()override{}
} ui;
}
#define GetForegroundWindow fixture::Foreground
#define GetWindowThreadProcessId fixture::Process
#include "client/buy_menu.cpp"
#undef GetForegroundWindow
#undef GetWindowThreadProcessId
namespace csol::client {
metahook_api_t* api{};mh_interface_t* interfaces{};cl_enginefunc_t engine{};cl_exportfuncs_t downstream{};ref_params_t view{};
std::vector<std::byte> ReadResource(const char*){throw std::runtime_error("fixture has no presentation files");}
void SynchronizeSession(){}void ClearNativeWeaponSelection(){}
}
namespace csol {Image DecodeTGA(std::span<const std::byte>){return{};}}
CreateInterfaceFn Sys_GetFactory(HINTERFACEMODULE){return nullptr;}
int main()
{
    using namespace csol::client;using namespace csol::buy;using fixture::Check;
    try{
        clientUI=&fixture::ui;engine.Con_IsVisible=[](){return fixture::console?1:0;};
        downstream.IN_ClearStates=[](){fixture::order.push_back(2);};downstream.IN_ActivateMouse=[](){fixture::order.push_back(3);};
        oldBuy=[](){++fixture::fallbacks;};engine.pfnServerCmd=[](const char*){++fixture::sent;return 1;};
        visible=true;waiting=true;request=77;
        auto message=Encode({Kind::Result,76,144,4100,100,Result::Success,2});Receive("CSOBuy",int(message.size()),message.data());
        Check(visible&&waiting&&fixture::order.empty());
        message=Encode({Kind::Result,77,144,4100,100,Result::Success,2});Receive("CSOBuy",int(message.size()),message.data());
        Check(!visible&&!waiting);Check(fixture::order==std::vector<int>({1,2,3}));Check(purchases==1);
        Receive("CSOBuy",int(message.size()),message.data());Check(purchases==1&&fixture::order.size()==3);
        usercmd_t cmd{};cmd.buttons=IN_ATTACK|IN_ATTACK2;BuyMenuCreateMove(&cmd,1);Check(cmd.buttons==0&&!swallowMouse);
        visible=true;waiting=true;fixture::order.clear();message=Encode({Kind::Result,77,144,4100,100,Result::Insufficient,2});Receive("CSOBuy",int(message.size()),message.data());
        Check(visible&&!waiting&&fixture::order.empty());
        fixture::console=true;Close();Check(!visible&&restoreMouse&&fixture::order==std::vector<int>{1});
        BuyMenuCreateMove(&cmd,1);Check(restoreMouse&&fixture::order.size()==1);
        fixture::console=false;BuyMenuCreateMove(&cmd,1);Check(!restoreMouse&&fixture::order==std::vector<int>({1,2,3}));
        visible=true;fixture::order.clear();Close(false);Check(!visible&&!restoreMouse&&fixture::order==std::vector<int>{1});
        capability=false;Open();Check(fixture::fallbacks==1&&!visible);
        capability=true;OpenMenu(false);Check(fixture::fallbacks==1&&!visible); // Missing host resources must not loop buy.
        Packet packet{Kind::Item,1,144,4100,5000,Result::Available,2};offers[144]=packet;catalogReady=true;balance=5000;waiting=false;request=1;engine.GetClientTime=[](){return 10.0f;};
        Purchase(*Find(144));Check(waiting&&fixture::sent==1);Purchase(*Find(144));Check(fixture::sent==1);
        waiting=false;balance=100;Purchase(*Find(144));Check(!waiting&&fixture::sent==1);
        category=0;Check(Rows().size()==4);category=1;Check(Rows().front()->id==220);
        ResetBuyMenu();Check(!capability&&!visible&&offers.empty()&&!restoreMouse);
        std::cout<<"buy menu client: "<<fixture::checks<<" checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<" after "<<fixture::checks<<" checks\n";return 1;}
}
