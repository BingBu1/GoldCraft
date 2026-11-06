// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/client/runtime.hpp"
#include "../../native/csol/client/wall_status.hpp"
#include "../../native/csol/client/wall_upgrade_hud.hpp"
#include "../../native/csol/client/scenario_round_hud_client.hpp"
#include "../../native/csol/wall_status.hpp"
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include "../csol-wall-status/surface_stub.hpp"
#include <IVGUI2Extension.h>
#include <source_location>
#include <VGUI/ILocalize.h>
#include <VGUI/IScheme.h>
#include <Color.h>
#include <fstream>
#include <algorithm>

namespace csol::client { void InvalidateWallResources(); }

namespace fixture
{
std::map<std::string, pfnUserMsgHook> receivers;
int checks{}, forwarded{}, draws{}, lastX{}, lastY{};
std::wstring last;
IVGUI2Extension_BaseUICallbacks* overlay{};
int registrations{},unregistrations{},pushes{},pops{},flushes{};
int resourceReads{}; bool missingToken{}, resetOnRead{};
int imageReads{}, fontReads{}, textureUploads{}, textureDeletes{}, quads{}, notices{}, measureCalls{};
int nextFont{7}, currentFont{}, nextTexture{20};
int screenWidth{1280}, screenHeight{720}, schemeTall{13}, schemeLoads{}, schemeFontRequests{}, localizeRequests{}, engineLocalizeRequests{};
bool vguiPresent{true}, badScheme{}, invalidateOnScheme{}, noSchemeFont{};
bool badNotice{}, resetOnPlayer{}, invalidateOnUpload{}, levelOnMeasure{};
std::wstring notice;
std::string playerName{"Tester"};
struct TextDraw { std::wstring text; int x,y; };
std::vector<TextDraw> textDraws;
std::vector<std::byte> fontBytes, catalogBytes, schemeBytes;
float hud = 1, center = 1;
void Check(bool value, const std::source_location where = std::source_location::current())
{
    ++checks;
    if (!value) throw std::runtime_error("wall status check line " + std::to_string(where.line()));
}
int Previous(const char*, int, void*) { ++forwarded; return 73; }
void Diagnostic(const char*, ...) {}
int Send(const char* name, std::vector<std::byte> bytes)
{ return receivers.at(name)(name, int(bytes.size()), bytes.data()); }
std::vector<std::byte> Packet(int index, std::int32_t value)
{
    std::vector<std::byte> bytes{std::byte(index)};
    const auto bits = std::bit_cast<std::uint32_t>(value);
    for (unsigned i = 0; i < 4; ++i) bytes.push_back(std::byte(bits >> (8 * i)));
    return bytes;
}
void Wall(std::int32_t health = 25000)
{ Send("StatusValue", Packet(0, 3)); Send("StatusValue", Packet(1, health)); }
}
namespace fixture
{
struct Surface final:vgui::TestSurface {
 vgui::VPANEL GetEmbeddedPanel()override{return 1;}
 void GetScreenSize(int& w,int& h)override{w=screenWidth;h=screenHeight;}
 vgui::HFont CreateFont()override{return nextFont++;}
 bool AddGlyphSetToFont(vgui::HFont,const char* name,int height,int weight,int,int,int flags,int,int)override{
  Check(std::string(name)=="Microsoft YaHei" && height==16);return true;}
 void GetTextSize(vgui::HFont f,const wchar_t* text,int& w,int& h)override{
  Check(f==7 || f==77);++measureCalls;w=f==7?160:int(std::wcslen(text))*schemeTall;h=f==7?16:schemeTall;
  if(levelOnMeasure){levelOnMeasure=false;const unsigned char p[]{27,10,1,8};csol::client::ConsumeWallUpgradeHud(4,p);}}
 void PushMakeCurrent(vgui::VPANEL,bool)override{++pushes;}
 void PopMakeCurrent(vgui::VPANEL)override{++pops;}
 void DrawSetTextFont(vgui::HFont f)override{currentFont=int(f);}
 void DrawSetTextColor(int r,int g,int b,int a)override{Check(r==255 && g==(currentFont==7?178:255) && b==(currentFont==7?0:255) && a==255);}
 void DrawSetTextPos(int x,int y)override{lastX=x;lastY=y;}
 void DrawPrintText(const wchar_t* text,int len)override{++draws;last.assign(text,len);textDraws.push_back({last,lastX,lastY});}
 void DrawFlushText()override{++flushes;}
 int CreateNewTextureID(bool procedural)override{Check(procedural);return nextTexture++;}
 void DrawSetTextureRGBA(int,const unsigned char* bytes,int w,int h,int filter,bool reload)override{
  ++textureUploads;Check(w==204 && h==28 && filter==1 && reload && bytes[3]==255);
  if(invalidateOnUpload){invalidateOnUpload=false;csol::client::InvalidateWallResources();}}
 bool DeleteTextureByID(int id)override{Check(id>=20);++textureDeletes;return true;}
 void DrawTexturedRect(int x0,int y0,int x1,int y1)override{++quads;Check(x0==10 && y0==433 && x1==214 && y1==461);}
} surface;
struct Localize final:vgui::ILocalize {
 bool AddFile(IFileSystem*,const char*)override{return false;}
 void RemoveAll()override{}
 wchar_t* Find(const char*)override{return nullptr;}
 int ConvertANSIToUnicode(const char* text,wchar_t* output,int bytes)override{
  Check(bytes==59);const int result=MultiByteToWideChar(CP_UTF8,0,text,-1,output,bytes/2);
  if(!result){const auto n=std::min(std::strlen(text),std::size_t(bytes/2-1));for(std::size_t i=0;i<n;++i)output[i]=text[i];output[n]=0;}return result;}
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
struct Scheme final:vgui::IScheme {
 const char* GetResourceString(const char* name)override{Check(std::strcmp(name,"CSOLWallUpgradeRevision")==0);return badScheme?"0":"1";}
 vgui::IBorder* GetBorder(const char*)override{return nullptr;}
 vgui::HFont GetFont(const char* name,bool proportional)override{
  ++schemeFontRequests;Check(std::strcmp(name,"Default")==0 && !proportional);return noSchemeFont?0:77;}
 Color GetColor(const char*,Color fallback)override{return fallback;}
} scheme;
struct Schemes final:vgui::ISchemeManager {
 vgui::HScheme LoadSchemeFromFile(const char* path,const char* tag)override{
  ++schemeLoads;Check(std::strcmp(path,"resource/csol/wall/v1/WallUpgradeScheme.res")==0 && std::strcmp(tag,"CSOLWallUpgradeV1")==0);
  if(invalidateOnScheme){invalidateOnScheme=false;csol::client::InvalidateWallResources();}return 42;}
 void ReloadSchemes()override{Check(false);}
 vgui::HScheme GetDefaultScheme()override{Check(false);return 0;}
 vgui::HScheme GetScheme(const char*)override{Check(false);return 0;}
 vgui::IImage* GetImage(const char*,bool)override{return nullptr;}
 vgui::HTexture GetImageID(const char*,bool)override{return 0;}
 vgui::IScheme* GetIScheme(vgui::HScheme handle)override{Check(handle==42);return &scheme;}
 void Shutdown(bool)override{Check(false);}
 int GetProportionalScaledValue(int)override{Check(false);return 0;}
 int GetProportionalNormalizedValue(int)override{Check(false);return 0;}
} schemes;
struct Extension final:IVGUI2Extension {
 void RegisterBaseUICallbacks(IVGUI2Extension_BaseUICallbacks* p)override{overlay=p;++registrations;}
 void UnregisterBaseUICallbacks(IVGUI2Extension_BaseUICallbacks*)override{overlay=nullptr;++unregistrations;}
 void RegisterGameUICallbacks(IVGUI2Extension_GameUICallbacks*)override{}
 void RegisterGameUIOptionDialogCallbacks(IVGUI2Extension_GameUIOptionDialogCallbacks*)override{}
 void RegisterGameUITaskBarCallbacks(IVGUI2Extension_GameUITaskBarCallbacks*)override{}
 void RegisterGameUIBasePanelCallbacks(IVGUI2Extension_GameUIBasePanelCallbacks*)override{}
 void RegisterGameConsoleCallbacks(IVGUI2Extension_GameConsoleCallbacks*)override{}
 void RegisterClientVGUICallbacks(IVGUI2Extension_ClientVGUICallbacks*)override{}
 void RegisterKeyValuesCallbacks(IVGUI2Extension_KeyValuesCallbacks*)override{}
 void UnregisterGameUICallbacks(IVGUI2Extension_GameUICallbacks*)override{}
 void UnregisterGameUIOptionDialogCallbacks(IVGUI2Extension_GameUIOptionDialogCallbacks*)override{}
 void UnregisterGameUITaskBarCallbacks(IVGUI2Extension_GameUITaskBarCallbacks*)override{}
 void UnregisterGameUIBasePanelCallbacks(IVGUI2Extension_GameUIBasePanelCallbacks*)override{}
 void UnregisterGameConsoleCallbacks(IVGUI2Extension_GameConsoleCallbacks*)override{}
 void UnregisterClientVGUICallbacks(IVGUI2Extension_ClientVGUICallbacks*)override{}
 void UnregisterKeyValuesCallbacks(IVGUI2Extension_KeyValuesCallbacks*)override{}

 const char* GetBaseDirectory()const override{return "";}
 const char* GetCurrentLanguage()const override{return "schinese";}
} extension;
IBaseInterface* SurfaceFactory(const char* name,int*){
 if(std::strcmp(name,VGUI_LOCALIZE_INTERFACE_VERSION)==0)++engineLocalizeRequests;
 return std::strcmp(name,VGUI_SURFACE_INTERFACE_VERSION)==0?&surface:nullptr;}
IBaseInterface* VGuiFactory(const char* name,int*){
 if(std::strcmp(name,VGUI_LOCALIZE_INTERFACE_VERSION)==0){++localizeRequests;return &localize;}
 if(std::strcmp(name,VGUI_SCHEME_INTERFACE_VERSION)==0)return &schemes;
 return nullptr;}
HMODULE WINAPI VGuiModule(const char* name){Check(std::strcmp(name,"vgui2.dll")==0);return vguiPresent?reinterpret_cast<HMODULE>(2):nullptr;}
IBaseInterface* ExtensionFactory(const char*,int*){return &extension;}
void Draw(int intermission){csol::client::DrawWallStatus(intermission);if(overlay){int n{};VGUI2Extension_CallbackContext c;c.IsPost=true;overlay->Paint(n,n,n,n,&c);}}
}
CreateInterfaceFn Sys_GetFactory(HINTERFACEMODULE module){return module==reinterpret_cast<HINTERFACEMODULE>(2)?fixture::VGuiFactory:fixture::ExtensionFactory;}
namespace csol::client
{
metahook_api_t* api{};
cl_enginefunc_t engine{};
ref_params_t view{};
void SynchronizeSession() {}
bool ShowScenarioRoundHudText(unsigned channel,std::wstring_view text,const std::array<std::uint8_t,3>& color)
{fixture::Check(channel==1 && color==std::array<std::uint8_t,3>{220,255,128});++fixture::notices;fixture::notice=text;return true;}
std::vector<std::byte> ReadResource(const char* path)
{
    using namespace fixture;
    if(std::string(path)=="resource/Hud/ZBS/zbslevelupwallbg.tga"){
      ++imageReads;std::vector<std::byte> bytes(18+204*28*4,std::byte{255});
      std::fill_n(bytes.begin(),18,std::byte{});bytes[2]=std::byte{2};bytes[12]=std::byte{204};bytes[14]=std::byte{28};bytes[16]=std::byte{32};bytes[17]=std::byte{0x28};return bytes;}
    if(std::string(path)=="resource/font/df_gb_y9.ttf"){++fontReads;return fontBytes;}
    if(std::string(path)=="resource/csol/wall/v1/WallUpgradeScheme.res")return schemeBytes;
    Check(std::strcmp(path, "resource/cso_chn.txt") == 0); ++resourceReads;
    if (resetOnRead) { resetOnRead = false; InvalidateWallResources(); }
    if (!missingToken && !badNotice) return catalogBytes;
    std::u16string content = u"\ufeff\"lang\" { \"Tokens\" { \"CSO_ZBSVER2_StatusBar_WallLevel\" \"墙体耐久等级：Level %d($5000)\" ";
    if (!missingToken) content += u"\"CSO_ZBSVER2_StatusBar_WallLevelMax\" \"墙体耐久等级：Level MAX\" ";
    content += u"\"CSO_GuardWall\" \"墙体防御\" \"CSO_ZBS_WallUpgradeMsg\" ";
    content += badNotice?u"\"bad %s %d %d\" ":u"\"%s将防御工事的墙体耐久升至等级%d\" ";
    content += u"} }";
    std::vector<std::byte> bytes;
    for (char16_t c : content) { bytes.push_back(std::byte(c)); bytes.push_back(std::byte(c >> 8)); }
    return bytes;
}
}
int main(int argc,char** argv)
{
    using namespace fixture;
    using namespace csol::client;
    try
    {
        Check(argc==5);
        std::ifstream input(argv[1],std::ios::binary|std::ios::ate);Check(bool(input));
        fontBytes.resize(std::size_t(input.tellg()));input.seekg(0);
        input.read(reinterpret_cast<char*>(fontBytes.data()),std::streamsize(fontBytes.size()));Check(bool(input));
        std::ifstream catalog(argv[2],std::ios::binary|std::ios::ate);Check(bool(catalog));
        catalogBytes.resize(std::size_t(catalog.tellg()));catalog.seekg(0);
        catalog.read(reinterpret_cast<char*>(catalogBytes.data()),std::streamsize(catalogBytes.size()));Check(bool(catalog));
        std::ifstream schemeInput(argv[3],std::ios::binary|std::ios::ate);Check(bool(schemeInput));
        schemeBytes.resize(std::size_t(schemeInput.tellg()));schemeInput.seekg(0);
        schemeInput.read(reinterpret_cast<char*>(schemeBytes.data()),std::streamsize(schemeBytes.size()));Check(bool(schemeInput));
        metahook_api_t hooks{}; api = &hooks;
        hooks.GetEngineFactory=[]()->CreateInterfaceFn{return SurfaceFactory;};
        hooks.GetPluginModuleHandleByBaseFileName=[](const char*)->HINTERFACEMODULE{return reinterpret_cast<HINTERFACEMODULE>(1);};
        hooks.HookUserMsg = [](const char* name, pfnUserMsgHook callback)->pfnUserMsgHook {
            const auto old = receivers.contains(name) ? receivers[name] : Previous;
            receivers[name] = callback; return old;
        };
        engine.pfnGetCvarFloat = [](const char* name)->float { return std::strcmp(name, "hud_draw") ? center : hud; };
        engine.pfnGetScreenInfo = [](SCREENINFO* s)->int { s->iWidth = 1280; s->iHeight = 720; return 1; };
        engine.pfnDrawConsoleStringLen = [](const char*, int* w, int* h) { *w = 160; *h = 16; };
        engine.pfnDrawConsoleString = [](int,int,char*)->int { Check(false);return 0; };
        engine.Con_DPrintf = Diagnostic;
        engine.pfnGetPlayerInfo=[](int slot,hud_player_info_t* player){Check(slot==2);player->name=playerName.data();
          if(resetOnPlayer){resetOnPlayer=false;ResetWallStatus();}};
        InitializeWallStatus(); InitializeWallStatus();Check(registrations==1);
        view.health = 100;
        Wall(); Draw(0); Check(draws == 1 && last == L"墙体耐久: 25000" && lastX == 560 && lastY == 376);
        Wall(1000000); Draw(0); Check(last == L"墙体耐久: 1000000");
        center = 0; Draw(0); Check(lastX == 10 && lastY == 463);
        auto before = draws; center = std::numeric_limits<float>::infinity(); Draw(0);
        Check(draws == before + 1 && lastY == 463); center = 1;
        before = forwarded;
        const std::vector<std::byte> legacy{std::byte{1}, std::byte{2}, std::byte{0}};
        Check(Send("StatusValue", legacy) == 73 && forwarded == before + 1);
        before = draws; Draw(0); Check(draws == before);
        for (int index : {8, 255}) Check(Send("StatusValue", Packet(index, 3)) == 1);
        for (int type : {-1, 14, std::numeric_limits<int>::max()}) Check(Send("StatusValue", Packet(0, type)) == 1);
        for (int size : {0, 1, 2, 4, 6, 20}) Check(Send("StatusValue", std::vector<std::byte>(size)) == 1);
        Check(forwarded == 1);
        for (int type = 0; type <= 13; ++type)
        {
            Wall(); Send("StatusValue", Packet(0, type));
            if (type == 3) Send("StatusValue", Packet(1, 1));
            before = draws; Draw(0); Check(draws == before + (type == 3));
        }
        Send("StatusValue", Packet(0, 9));
        before=draws; Draw(0); Check(draws==before); // No stale wall health while waiting for field1.
        for (int level : {1, 2, 27, 28, 255, -1})
        {
            Send("StatusValue", Packet(1, level)); Draw(0);
            const auto expected = level >= 28 ? std::wstring(L"墙体耐久等级：Level MAX") :
                L"墙体耐久等级：Level " + std::to_wstring(level) + L"($5000)";
            Check(last==expected);
        }
        Check(resourceReads==1); Draw(0); Check(resourceReads==1);
        InvalidateWallResources(); resetOnRead=true; before=draws; Draw(0); Check(draws==before);
        Draw(0); Check(draws==before+1 && resourceReads==3);
        InvalidateWallResources(); missingToken=true; before=draws; Draw(0); Check(draws==before);
        missingToken=false; InvalidateWallResources(); Draw(0); Check(draws==before+1);
        Send("StatusValue", Packet(0, 3)); before=draws; Draw(0); Check(draws==before);
        Send("StatusValue", Packet(1, 800)); Draw(0); Check(last==L"墙体耐久: 800");
        Send("StatusValue", Packet(0, 9)); Send("StatusValue", Packet(1, 8)); before=draws;
        hud=0; Draw(0); hud=1; Draw(1); view.health=0; Draw(0); view.health=100;
        view.spectator=1; Draw(0); view.spectator=0; Send("HideWeapon",{std::byte{4}}); Draw(0);
        Check(draws==before); Send("HideWeapon",{std::byte{0}}); Draw(0); Check(draws==before+1);
        for (const char* reset : {"ResetHUD","InitHUD"})
        { Send("StatusValue", Packet(0,9)); Send("StatusValue",Packet(1,10)); before=draws;
          Send(reset,{std::byte{0}}); Draw(0); Check(draws==before); }
        for (int health : {0, -1, std::numeric_limits<int>::min()})
        { Wall(health); before = draws; Draw(0); Check(draws == before); }
        Wall(); before = draws;
        hud = 0; Draw(0); hud = 1; Draw(1);
        view.health = 0; Draw(0); view.health = 100;
        view.spectator = 1; Draw(0); view.spectator = 0;
        Send("HideWeapon", {std::byte{4}}); Draw(0); Check(draws == before);
        Send("HideWeapon", {std::byte{0}}); Draw(0); Check(draws == before + 1);
        for (const char* name : {"ResetHUD", "InitHUD"})
        {
            Wall(); before = draws; Check(Send(name, {std::byte{0}}) == 73);
            Draw(0); Check(draws == before);
        }
        Wall(); ResetWallStatus(); before = draws; Draw(0); Check(draws == before);
        VGUI2Extension_CallbackContext beforeUI,afterUI;afterUI.IsPost=true;
        Wall();before=draws;overlay->ShowConsole(&beforeUI);Draw(0);Check(draws==before);
        overlay->HideConsole(&afterUI);Draw(0);Check(draws==before+1);
        before=draws;overlay->ActivateGameUI(&beforeUI);Draw(0);Check(draws==before);
        overlay->HideGameUI(&afterUI);Draw(0);Check(draws==before+1);
        Check(pushes==pops && pops==flushes && flushes==draws);

        ResetWallStatus();
        auto panel=[](int enabled,int level){const unsigned char p[]{27,10,static_cast<unsigned char>(enabled),static_cast<unsigned char>(level)};
            Check(ConsumeWallUpgradeHud(sizeof(p),p));};
        auto announce=[](int slot){const unsigned char p[]{27,11,static_cast<unsigned char>(slot)};
            Check(ConsumeWallUpgradeHud(sizeof(p),p));};
        if(std::strcmp(argv[4],"font-first")!=0){announce(2);Check(notices==1 && localizeRequests==1 && engineLocalizeRequests==0);notices=0;}
        Check(!ConsumeWallUpgradeHud(0,nullptr));
        for(const auto& packet:std::vector<std::vector<unsigned char>>{{27},{26,10,1,1},{27,12}})
            Check(!ConsumeWallUpgradeHud(int(packet.size()),packet.data()));
        panel(1,1);Check(WallUpgradeHudVisible());before=draws;Draw(0);
        Check(draws==before+2 && imageReads==1 && fontReads==1 && textureUploads==1);
        Check(textDraws[textDraws.size()-2].text==L"墙体防御" && textDraws[textDraws.size()-2].x==29 &&
            textDraws[textDraws.size()-2].y==441 && last==L"Level 1" && lastX==108 && lastY==441 && schemeFontRequests==1);
        const auto cachedReads=resourceReads,cachedMeasures=measureCalls;
        Draw(0);Draw(0);Check(resourceReads==cachedReads && measureCalls==cachedMeasures && imageReads==1 && fontReads==1);
        for(int level:{0,2,27,28,255}){panel(1,level);Draw(0);Check(last==(level>=28?L"Level MAX":L"Level "+std::to_wstring(level)));}
        for(const auto& packet:std::vector<std::vector<unsigned char>>{{27,10},{27,10,0},{27,10,0,9,0},{27,11},{27,11,2,0}})
            Check(ConsumeWallUpgradeHud(int(packet.size()),packet.data()));
        Draw(0);Check(WallUpgradeHudVisible() && last==L"Level MAX" && notices==0);
        panel(0,3);before=draws;Draw(0);Check(draws==before && !WallUpgradeHudVisible());
        // Notice uses the current server level, including while panel is hidden.
        playerName="name%s%d";announce(2);Check(notices==1 && notice==L"name%s%d将防御工事的墙体耐久升至等级3");
        panel(1,28);playerName="玩家";announce(2);Check(notices==2 && notice==L"玩家将防御工事的墙体耐久升至等级28");
        announce(0);announce(33);Check(notices==2);
        resetOnPlayer=true;announce(2);Check(notices==2 && !WallUpgradeHudVisible());
        panel(1,8);badNotice=true;InvalidateWallResources();announce(2);Check(notices==2);
        badNotice=false;InvalidateWallResources();announce(2);Check(notices==3 && notice==L"玩家将防御工事的墙体耐久升至等级8");
        // Actual ReGameDLL order: first spawn ResetHUD, InitHUD, then server27/10;
        // later respawn sends only ResetHUD and must retain the wall panel.
        Send("ResetHUD",{std::byte{0}});Send("InitHUD",{std::byte{0}});Check(!WallUpgradeHudVisible());
        panel(1,7);Draw(0);Check(last==L"Level 7");
        Wall();Send("ResetHUD",{std::byte{0}});before=draws;Draw(0);
        Check(WallUpgradeHudVisible() && draws==before+2 && last==L"Level 7");
        Send("StatusValue",legacy);Draw(0);Check(WallUpgradeHudVisible() && last==L"Level 7");
        Send("InitHUD",{std::byte{0}});before=draws;Draw(0);Check(draws==before && !WallUpgradeHudVisible());
        panel(1,27);const unsigned char reset[]{9};Check(!ConsumeWallUpgradeHud(1,reset));
        Check(!WallUpgradeHudVisible());announce(2);Check(notice==L"玩家将防御工事的墙体耐久升至等级1");
        panel(1,6);Draw(0);before=draws;
        hud=0;Draw(0);hud=1;Draw(1);Send("HideWeapon",{std::byte{4}});Draw(0);Send("HideWeapon",{std::byte{0}});
        overlay->ShowConsole(&beforeUI);Draw(0);overlay->HideConsole(&afterUI);
        overlay->ActivateGameUI(&beforeUI);Draw(0);overlay->HideGameUI(&afterUI);Check(draws==before);
        Draw(0);Check(draws==before+2);
        // The wall panel's original enabled state has no player-health gate.
        view.health=0;view.spectator=1;Draw(0);Check(last==L"Level 6");view.health=100;view.spectator=0;
        InvalidateWallResources();resetOnRead=true;before=draws;Draw(0);Check(draws==before);
        Draw(0);Check(draws==before+2 && last==L"Level 6");
        InvalidateWallResources();invalidateOnUpload=true;before=draws;const auto deleted=textureDeletes;
        Draw(0);Check(draws==before && textureDeletes==deleted+1);Draw(0);Check(draws==before+2);
        panel(1,5);levelOnMeasure=true;before=draws;Draw(0);Check(draws==before);
        Draw(0);Check(draws==before+2 && last==L"Level 8");
        const auto previousLoads=schemeLoads;Draw(0);Check(schemeLoads==previousLoads);
        screenWidth=1920;screenHeight=1080;schemeTall=18;Draw(0);
        Check(schemeLoads==previousLoads+1 && textDraws[textDraws.size()-2].x==19 && lastY==439);
        InvalidateWallResources();badScheme=true;before=draws;Draw(0);Check(draws==before);
        badScheme=false;InvalidateWallResources();noSchemeFont=true;Draw(0);Check(draws==before);
        noSchemeFont=false;InvalidateWallResources();invalidateOnScheme=true;Draw(0);Check(draws==before);
        Draw(0);Check(draws==before+2 && last==L"Level 8");
        Check(engineLocalizeRequests==0);
        Check(pushes==pops && pops==flushes && draws==flushes+quads && fontReads==1);
        before=draws;Wall(); ShutdownWallStatus(); Draw(0); Check(draws == before && unregistrations==1);
        Check(!WallUpgradeHudVisible());
        std::cout << "wall status production consumer checks: " << checks << '\n';
        return 0;
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
