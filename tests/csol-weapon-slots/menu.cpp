// SPDX-License-Identifier: GPL-3.0-or-later
#include "client/runtime.hpp"
#include "client/weapon_inventory.hpp"
#include <in_buttons.h>
#include <iostream>
#include <map>
#include <sstream>
#include <cstring>

namespace csol::client
{
cl_enginefunc_t engine{};
cl_exportfuncs_t downstream{};
ref_params_t view{};
metahook_api_t* api{};
void SynchronizeSession(){}
void ReceiveExtendedWeaponDefinition(const WeaponDefinition&){}
void ReceiveExtendedWeaponState(const WeaponCurrent&){}
void AddWeaponPickup(int){}
void ClearNativeWeaponSelection(){}
void* Resolve(const char*,mh_gamesymbol_kind_t){return nullptr;}
}
namespace
{
using namespace csol;
using namespace csol::client;
std::map<std::string,pfnUserMsgHook> messages;
std::map<std::string,xcommand_t> commands;
std::vector<std::string> sent;
int checks{},failures{},nativeCalls{},cancelCalls{};
float now=10,fast{};
bool submenuOnCancel{};
unsigned nativeMenuSlots{};
bool nativeMenuDisplayed{},clearPacketExact{},mirrorAtClear{},nativeAtCancel{},mirrorAtCancel{};
int nativeClearCalls{};
std::vector<std::string> menuCalls;
cvar_t protocol{};
constexpr std::array<int,4> ids{725,220,29,4};
constexpr std::array<const char*,4> names{"weapon_beamgun","weapon_tknifeex2","weapon_knife","weapon_hegrenade"};

void Check(bool value,const std::string& label)
{
    ++checks;
    if(!value){++failures;std::cerr<<"FAIL "<<label<<'\n';}
}
void Menu(unsigned slots,unsigned timeout=255)
{
    unsigned char bytes[]{static_cast<unsigned char>(slots),static_cast<unsigned char>(slots>>8),
        static_cast<unsigned char>(timeout),0,'a',0};
    messages.at("ShowMenu")("ShowMenu",sizeof(bytes),bytes);
}
bool MenuActive();
template<int Slot>void NativeSlot()
{
    ++nativeCalls;
    if(nativeMenuDisplayed&&(nativeMenuSlots&(1u<<Slot)))nativeMenuDisplayed=false;
}
void NativeCancel()
{
    ++cancelCalls;menuCalls.emplace_back("cancel");
    nativeAtCancel=nativeMenuDisplayed;mirrorAtCancel=MenuActive();
    // Stock cancelselect only forwards Escape; CHudMenu remains displayed.
    if(submenuOnCancel)Menu(1u<<9);
}
int NativeMessage(const char* name,int size,void* data)
{
    if(std::strcmp(name,"ShowMenu")||!data||size<5)return 1;
    const auto* bytes=static_cast<const unsigned char*>(data);
    nativeMenuSlots=bytes[0]|(unsigned(bytes[1])<<8);
    nativeMenuDisplayed=nativeMenuSlots!=0;
    menuCalls.emplace_back(nativeMenuDisplayed?"show":"clear");
    if(!nativeMenuDisplayed)
    {
        ++nativeClearCalls;mirrorAtClear=MenuActive();
        clearPacketExact=size==5;
        for(int i=0;i<size;++i)clearPacketExact=clearPacketExact&&bytes[i]==0;
    }
    return 1;
}
void Send(const char* name,const std::vector<std::byte>& bytes)
{
    messages.at(name)(name,int(bytes.size()),const_cast<std::byte*>(bytes.data()));
}
void Stock(int id,int slot,const char* name)
{
    std::vector<unsigned char> bytes(name,name+std::strlen(name)+1);
    for(int value:{255,255,255,255,slot,0,id,0})bytes.push_back(static_cast<unsigned char>(value));
    ObserveLegacyWeaponDefinition(int(bytes.size()),bytes.data());
}
void Reset(float fastSwitch)
{
    ResetWeaponInventory(true);now+=10;fast=fastSwitch;view={};view.health=100;
    submenuOnCancel=false;nativeCalls=cancelCalls=0;sent.clear();
    nativeMenuSlots=0;nativeMenuDisplayed=clearPacketExact=mirrorAtClear=nativeAtCancel=mirrorAtCancel=false;
    nativeClearCalls=0;menuCalls.clear();
    Send(WeaponDefinitionMessage,EncodeWeaponDefinition({725,0,8,15,16,200,100,80,0,names[0]}));
    Send(WeaponDefinitionMessage,EncodeWeaponDefinition({220,1,6,14,-1,29,-1,1,24,names[1]}));
    Stock(29,2,names[2]);Stock(4,3,names[3]);
    WeaponOwnership own{};for(int id:ids)own[id]=true;
    for(const auto& part:EncodeWeaponInventory(own,1))Send(WeaponInventoryMessage,part);
    Send(WeaponCurrentMessage,EncodeWeaponCurrent({725,1,80,200,100}));
}
int Selection()
{
    std::ostringstream out;WeaponSelectionDiagnostics(out);
    const auto text=out.str();const auto offset=text.find("\"id\":");
    return offset==std::string::npos?-1:std::atoi(text.c_str()+offset+5);
}
bool MenuActive()
{
    std::ostringstream out;WeaponSelectionDiagnostics(out);
    return out.str().find("\"menu\":1")!=std::string::npos;
}
void Slot(int slot){commands.at("slot"+std::to_string(slot+1))();}
void Confirm(int slot,const std::string& label)
{
    usercmd_t cmd{};cmd.buttons=IN_ATTACK|IN_JUMP;
    WeaponInventoryCreateMove(.01f,&cmd,1);
    Check(sent==std::vector<std::string>{names[slot]},label+" command");
    Check(!(cmd.buttons&IN_ATTACK)&&(cmd.buttons&IN_JUMP),label+" consumes attack only");
    Check(Selection()==0,label+" closes selection");
}
void CancelChecks()
{
    for(float mode:{0.f,1.f})for(int slot=0;slot<4;++slot)
    {
        Reset(mode);Menu(1u<<9);
        const auto label="cancel fast="+std::to_string(int(mode))+" slot="+std::to_string(slot+1);
        Check(nativeMenuDisplayed&&nativeMenuSlots==(1u<<9),label+" native menu received");
        commands.at("cancelselect")();
        Check(cancelCalls==1,label+" forwards native cancel");
        Check(!MenuActive(),label+" releases menu lease");
        Check(!nativeMenuDisplayed&&nativeClearCalls==1&&clearPacketExact,label+" exact zero packet closes native menu");
        Check(!mirrorAtClear,label+" mirror cleared before native clear callback");
        Check(!nativeAtCancel&&!mirrorAtCancel,label+" both menus closed before Escape callback");
        Check(menuCalls==std::vector<std::string>{"show","clear","cancel"},label+" clear precedes Escape");
        Slot(slot);Check(nativeCalls==0,label+" not captured by closed menu");
        if(mode)Check(sent==std::vector<std::string>{names[slot]},label+" immediate command");
        else
        {
            Check(sent.empty()&&Selection()==ids[slot],label+" pending without command");
            Confirm(slot,label);
        }
    }
}
void MenuChecks()
{
    for(float mode:{0.f,1.f})
    {
        Reset(mode);Menu(1u<<9);
        for(int slot=0;slot<4;++slot)
        {
            Slot(slot);Check(MenuActive()&&nativeMenuDisplayed&&Selection()==0&&sent.empty(),"invalid menu key preserves both menus");
        }
        Check(nativeCalls==4,"invalid menu keys still reach native");
        Slot(9);Check(!MenuActive()&&!nativeMenuDisplayed&&nativeCalls==5,"valid zero exit releases both menus");
        Slot(0);
        if(mode)Check(sent==std::vector<std::string>{names[0]},"fast switch after zero exit");
        else Confirm(0,"confirm after zero exit");
    }
    Reset(1);Menu(1u<<9,1);now+=2;Slot(1);
    Check(sent==std::vector<std::string>{names[1]}&&nativeCalls==0,"expired menu releases slot");
    Reset(1);Menu(1u<<9);Menu(0);Slot(2);
    Check(sent==std::vector<std::string>{names[2]}&&nativeCalls==0,"server clear releases slot");
    Reset(0);Slot(1);commands.at("cancelselect")();
    Check(Selection()==0&&cancelCalls==0,"cancel pending weapon selection is local");
    Check(nativeClearCalls==0&&menuCalls.empty(),"weapon-only cancel does not send native menu clear");
    usercmd_t cmd{};cmd.buttons=IN_ATTACK;WeaponInventoryCreateMove(.01f,&cmd,1);
    Check((cmd.buttons&IN_ATTACK)&&sent.empty(),"cancelled selection preserves subsequent attack");
    commands.at("cancelselect")();Check(cancelCalls==1,"idle cancel reaches native");
    Check(nativeClearCalls==0&&menuCalls==std::vector<std::string>{"cancel"},"idle cancel preserves native Escape behavior");
    Reset(1);Menu(1u<<9);submenuOnCancel=true;commands.at("cancelselect")();
    Check(MenuActive(),"synchronous replacement menu survives cancel callback");
    Check(nativeMenuDisplayed&&nativeMenuSlots==(1u<<9),"native replacement menu also survives");
    Check(nativeClearCalls==1&&!nativeAtCancel&&!mirrorAtCancel&&
        menuCalls==std::vector<std::string>{"show","clear","cancel","show"},"replacement opens after clearing both old menus");
    Slot(0);Check(nativeCalls==1&&sent.empty(),"replacement menu still owns invalid key");
    Reset(1);Menu(1u<<9);NativeCancel();
    Check(nativeMenuDisplayed&&MenuActive()&&nativeClearCalls==0,"stock cancelselect alone leaves native menu displayed");
    Reset(1);Menu(1u<<9);ResetWeaponInventory(false);Slot(0);
    Check(!MenuActive()&&nativeCalls==1&&sent.empty(),"inventory reset clears menu and ownership");
}
}
int main()
{
    metahook_api_t hooks{};api=&hooks;
    hooks.HookCmd=[](const char* name,xcommand_t callback)->xcommand_t{
        commands[name]=callback;
        if(!std::strcmp(name,"cancelselect"))return NativeCancel;
        constexpr std::array<xcommand_t,10> slots{NativeSlot<0>,NativeSlot<1>,NativeSlot<2>,NativeSlot<3>,NativeSlot<4>,
            NativeSlot<5>,NativeSlot<6>,NativeSlot<7>,NativeSlot<8>,NativeSlot<9>};
        if(!std::strncmp(name,"slot",4))return slots[std::atoi(name+4)-1];
        return NativeSlot<10>;};
    hooks.HookUserMsg=[](const char* name,pfnUserMsgHook callback)->pfnUserMsgHook{messages[name]=callback;return NativeMessage;};
    engine.pfnHookUserMsg=[](const char* name,pfnUserMsgHook callback)->int{messages[name]=callback;return 1;};
    engine.pfnAddCommand=[](const char* name,xcommand_t callback)->int{commands[name]=callback;return 1;};
    engine.pfnRegisterVariable=[](const char*,const char*,int)->cvar_t*{return &protocol;};
    engine.Cvar_SetValue=[](const char*,float value){protocol.value=value;};
    engine.GetClientTime=[]()->float{return now;};
    engine.pfnGetCvarFloat=[](const char* name)->float{return !std::strcmp(name,"hud_fastswitch")?fast:1.f;};
    engine.pfnServerCmd=[](const char* command)->int{sent.emplace_back(command);return 1;};
    engine.pfnGetScreenInfo=[](SCREENINFO* screen)->int{screen->iWidth=1280;screen->iHeight=720;return 1;};
    engine.pfnSPR_GetList=[](char*,int* count)->client_sprite_t*{*count=0;return nullptr;};
    InitializeWeaponInventory();
    CancelChecks();MenuChecks();
    std::cout<<"weapon menu "<<checks<<" checks, "<<failures<<" failures\n";
    return failures?1:0;
}
