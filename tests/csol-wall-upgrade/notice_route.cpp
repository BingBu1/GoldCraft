// SPDX-License-Identifier: GPL-3.0-or-later
// Real round-HUD receiver, local-text scheduler and fade state. The downstream
// effects dispatcher and absent graphics surface are explicit test boundaries.
#include "../../native/csol/client/runtime.hpp"
#include "../../native/csol/client/scenario/core/wall_upgrade_hud.hpp"
namespace fixture { ULONGLONG WINAPI Tick(); }
#define GetTickCount64 fixture::Tick
#include "../../native/csol/client/scenario/core/scenario_round_hud_client.cpp"
#undef GetTickCount64
#include <VGUI/ILocalize.h>
#include <fstream>
#include <iostream>
#include <source_location>

namespace fixture {
int checks{}, dispatches{}, conversions{}, engineLocalizeRequests{}, soundCount{}, resourceReads{};
ULONGLONG ticks{100000};
bool vguiPresent{};
std::map<std::string,pfnUserMsgHook> hooks;
std::vector<std::byte> catalog;
std::string playerName="Tester";
void Check(bool value,std::source_location where=std::source_location::current())
{++checks;if(!value)throw std::runtime_error("wall route check line "+std::to_string(where.line()));}
ULONGLONG WINAPI Tick(){return ticks;}
HMODULE WINAPI VGuiModule(const char* name)
{Check(std::strcmp(name,"vgui2.dll")==0);return vguiPresent?reinterpret_cast<HMODULE>(2):nullptr;}
struct Localize final:vgui::ILocalize {
 bool AddFile(IFileSystem*,const char*)override{return false;}
 void RemoveAll()override{}
 wchar_t* Find(const char*)override{return nullptr;}
 int ConvertANSIToUnicode(const char* text,wchar_t* output,int bytes)override{
  ++conversions;Check(bytes==59);return MultiByteToWideChar(CP_UTF8,0,text,-1,output,bytes/2);}
 int ConvertUnicodeToANSI(const wchar_t*,char*,int)override{return 0;}
 vgui::StringIndex_t FindIndex(const char*)override{return 0;}
 void ConstructString(wchar_t*,int,const wchar_t*,int,...)override{}
 const char* GetNameByIndex(vgui::StringIndex_t)override{return nullptr;}
 wchar_t* GetValueByIndex(vgui::StringIndex_t)override{return nullptr;}
 vgui::StringIndex_t GetFirstStringIndex()override{return 0;}
 vgui::StringIndex_t GetNextStringIndex(vgui::StringIndex_t)override{return 0;}
 void AddString(const char*,wchar_t*,const char*)override{}
 void SetValueByIndex(vgui::StringIndex_t,wchar_t*)override{}
 bool SaveToFile(IFileSystem*,const char*)override{return false;}
 int GetLocalizationFileCount()override{return 0;}
 const char* GetLocalizationFileName(int)override{return nullptr;}
 const char* GetFileNameByIndex(vgui::StringIndex_t)override{return nullptr;}
 void ReloadLocalizationFiles()override{}
 void ConstructString(wchar_t*,int,const char*,KeyValues*)override{}
 void ConstructString(wchar_t*,int,vgui::StringIndex_t,KeyValues*)override{}
} localize;
IBaseInterface* EngineFactory(const char* name,int*)
{if(std::strcmp(name,VGUI_LOCALIZE_INTERFACE_VERSION)==0)++engineLocalizeRequests;return nullptr;}
IBaseInterface* VGuiFactory(const char* name,int*)
{Check(std::strcmp(name,VGUI_LOCALIZE_INTERFACE_VERSION)==0);return &localize;}
int Previous(const char* name,int,void*)
{if(std::strcmp(name,"InitHUD")==0)csol::client::ResetWallUpgradeHud();return 73;}
int WallDispatch(const char*,int size,void* data)
{++dispatches;return csol::client::ConsumeWallUpgradeHud(size,data)?1:73;}
int Send(const char* name,std::initializer_list<unsigned char> bytes)
{std::vector<unsigned char> data(bytes);return hooks.at(name)(name,int(data.size()),data.data());}
void Log(const char*,...){}
}
CreateInterfaceFn Sys_GetFactory(HINTERFACEMODULE module)
{return module==reinterpret_cast<HINTERFACEMODULE>(2)?fixture::VGuiFactory:nullptr;}
namespace csol::client {
metahook_api_t* api{};cl_enginefunc_t engine{};
void SynchronizeSession(){}
std::vector<std::byte> ReadResource(const char* path)
{fixture::Check(std::strcmp(path,"resource/cso_chn.txt")==0);++fixture::resourceReads;return fixture::catalog;}
}
namespace csol::scenario {
// A wall27/11 notification is formatted locally and never loads the dialogue
// table. Any accidental excursion into those unrelated paths fails explicitly.
bool ReadMessageTableResource(std::span<const std::byte>,MessageTable&,ConfigError&)
{throw std::runtime_error("unexpected dialogue-table load");}
const ScenarioMessage* MessageTable::Find(std::uint16_t)const
{throw std::runtime_error("unexpected dialogue lookup");}
std::optional<std::string> DialogueImagePath(std::string_view)
{throw std::runtime_error("unexpected dialogue image");}
std::optional<std::string> DialogueSoundPath(std::string_view)
{throw std::runtime_error("unexpected dialogue sound");}
}
int main(int argc,char** argv)
{
 try {
  using namespace fixture;using namespace csol::client;
  Check(argc==5);std::ifstream file(argv[2],std::ios::binary|std::ios::ate);Check(bool(file));
  catalog.resize(std::size_t(file.tellg()));file.seekg(0);file.read(reinterpret_cast<char*>(catalog.data()),std::streamsize(catalog.size()));Check(bool(file));
  metahook_api_t storage{};api=&storage;
  storage.GetEngineFactory=[]()->CreateInterfaceFn{return EngineFactory;};
  storage.GetPluginModuleHandleByBaseFileName=[](const char*)->HINTERFACEMODULE{return nullptr;};
  storage.HookUserMsg=[](const char* name,pfnUserMsgHook hook)->pfnUserMsgHook{const auto old=hooks.contains(name)?hooks[name]:Previous;hooks[name]=hook;return old;};
  engine.pfnGetPlayerInfo=[](int slot,hud_player_info_t* p){Check(slot==2);p->name=playerName.data();};
  engine.GetClientTime=[](){return float(9000);};engine.Con_DPrintf=Log;
  engine.pfnPlaySoundByNameAtPitch=[](const char*,float,int){++soundCount;};
  hooks["MPToCL"]=WallDispatch;InitializeScenarioRoundHud();InitializeScenarioRoundHud();
  Check(Send("MPToCL",{27,10,1,3})==1 && WallUpgradeHudVisible() && dispatches==1);
  Check(Send("MPToCL",{27,11,2})==1 && !state.Notices()[1].active && conversions==0);
  vguiPresent=true;Check(Send("MPToCL",{27,11,2})==1 && state.Notices()[1].active && dispatches==3);
  Check(localLines[1]==std::vector<std::wstring>{L"Tester将防御工事的墙体耐久升至等级3"});
  const auto first=state.Notices()[1];
  Check(first.color==std::array<std::uint8_t,3>{220,255,128} && first.x==.5f && first.y==.325f);
  Check(first.fadeIn==.5f && first.showEnd-first.fadeEnd==3 && first.fadeOut==.5f && first.die==4);
  Check(csol::scenario_hud::NoticeAlpha(first,RealTime())==0);
  ticks+=250;Check(csol::scenario_hud::NoticeAlpha(first,RealTime())==127);
  ticks+=250;Check(csol::scenario_hud::NoticeAlpha(first,RealTime())==255);
  ticks=103750;Check(csol::scenario_hud::NoticeAlpha(first,RealTime())==127);
  ticks=104000;Check(csol::scenario_hud::NoticeAlpha(first,RealTime())==0);
  Check(conversions==1 && engineLocalizeRequests==0 && soundCount==0 && resourceReads==1);
  playerName="玩家%s";ticks=104500;
  Check(Send("MPToCL",{27,10,1,7})==1);Check(Send("MPToCL",{27,11,2})==1);
  Check(localLines[1]==std::vector<std::wstring>{L"玩家%s将防御工事的墙体耐久升至等级7"} && state.Notices()[1].born==4.5f);
  Check(Send("MPToCL",{27,11,0})==1 && conversions==2);
  Check(Send("MPToCL",{27,11,2,0})==1 && conversions==2);
  ScenarioRoundHudFrame();DrawScenarioRoundHud(9000,0);Check(requested && resourceReads==1);
  Check(Send("ResetHUD",{0})==73 && WallUpgradeHudVisible() && !state.Notices()[1].active && localLines[1].empty());
  Check(Send("MPToCL",{27,11,2})==1 && localLines[1][0].ends_with(L"等级7"));
  Check(Send("InitHUD",{0})==73 && !WallUpgradeHudVisible() && !state.Notices()[1].active && localLines[1].empty());
  Check(Send("MPToCL",{27,10,1,1})==1);Check(Send("MPToCL",{27,11,2})==1 && localLines[1][0].ends_with(L"等级1"));
  DrawScenarioRoundHud(0,1);Check(!requested);ShutdownScenarioRoundHud();ShutdownWallUpgradeHud(nullptr);
  Check(!state.Notices()[1].active && localLines[1].empty() && !WallUpgradeHudVisible() && soundCount==0);
  std::cout<<"wall route production consumer checks: "<<checks<<'\n';return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
