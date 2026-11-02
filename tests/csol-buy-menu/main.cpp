// SPDX-License-Identifier: GPL-3.0-or-later
// Production controller functions are extracted unchanged by the runner.
// Engine/VGUI calls are controlled boundaries; this is not a live UI test.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../../native/csol/buy_menu.hpp"
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <algorithm>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
using namespace csol;
using namespace csol::buy;
namespace vgui { enum { MOUSE_LEFT, MOUSE_RIGHT }; }
struct Search {
    bool visible{},keyboard{},focus{}; std::wstring text;
    Search()=default;Search(void*,const char*){}
    void SetParent(unsigned){}void SetMaximumCharCount(int){}void SetCatchEnterKey(bool){}void SetZPos(int){}
    void SetVisible(bool v){visible=v; if(!v)focus=false;}
    void SetKeyBoardInputEnabled(bool v){keyboard=v; if(!v)focus=false;}
    void SetText(const wchar_t* value){text=value;}
    void RequestFocus(){focus=keyboard;}
    bool HasFocus()const{return focus;}
    void GetText(wchar_t* value,int bytes){wcsncpy_s(value,bytes/sizeof(wchar_t),text.c_str(),_TRUNCATE);}
} search;
namespace vgui {using TextEntry=Search;}
Search* searchEntry=&search;
bool g_pVGuiInput2=true,g_pVGuiSurface2=true,g_pVGui=true,g_pVGuiPanel=true;
struct Extension;
Extension* extension=reinterpret_cast<Extension*>(1);
struct Cvar {const char* name;const char* string;};
using Command=void(*)();
using xcommand_t=Command;
using MessageHook=int(*)(const char*,int,void*);
using Factory=void*(*)(const char*,int*);
using HINTERFACEMODULE=HMODULE;
constexpr int FCVAR_ARCHIVE=1;
std::unordered_map<std::string,Command> commandHooks;
std::unordered_map<std::string,MessageHook> messageHooks;
std::vector<std::string> registeredCommands,registeredCvars;
int originalRebuys{},originalBuys{},originalCancels{},overlayRegistrations{};
struct View {int health=100;bool spectator{};} view;
std::string cvarValues[2];
Cvar cv[2]{{"t",""},{"ct",""}};
Cvar* favoriteCvars[2]{&cv[0],&cv[1]};
std::vector<std::string> commands,clientCommands;
std::unordered_map<std::string,std::string> resourceFiles;
std::string lastReadResource;
unsigned resourceErrors{};
std::vector<std::byte> ReadResource(const char* path){
    lastReadResource=path;auto it=resourceFiles.find(path);
    if(it==resourceFiles.end())throw std::runtime_error("missing resource");
    const auto* data=reinterpret_cast<const std::byte*>(it->second.data());
    return {data,data+it->second.size()};
}
bool focused=true,consoleVisible{};
double clockTime{};
int activated{},deactivated{},cleared{},hidden{},syncs{},captureCleared{},cursorLocked{};
struct Engine {
    void Con_Printf(const char*,...){++resourceErrors;}
    double(*GetClientTime)()=[](){return clockTime;};
    int(*pfnServerCmd)(char*)=[](char* s){commands.emplace_back(s);return 0;};
    int(*pfnClientCmd)(char*)=[](char* s){clientCommands.emplace_back(s);return 0;};
    int(*Con_IsVisible)()=[](){return int(consoleVisible);};
    void(*SetViewAngles)(float*)=[](float*){};
    void(*GetViewAngles)(float*)=[](float* values){std::fill_n(values,3,0);};
    void(*Cvar_Set)(const char*,const char*)=[](const char* name,const char* value){int i=!std::strcmp(name,"ct");cvarValues[i]=value;cv[i].string=cvarValues[i].c_str();};
    Cvar*(*pfnRegisterVariable)(const char*,const char*,int)=[](const char* name,const char*,int flags)->Cvar*{
        if(flags!=FCVAR_ARCHIVE)throw std::runtime_error("favorite archive flag");registeredCvars.emplace_back(name);
        return &cv[std::string_view(name).ends_with("_ct")?1:0];};
    int(*pfnAddCommand)(const char*,Command)=[](const char* name,Command handler){registeredCommands.emplace_back(name);commandHooks[name]=handler;return 1;};
    int(*pfnHookUserMsg)(const char*,MessageHook)=[](const char* name,MessageHook handler){messageHooks[name]=handler;return 1;};
} engine;
struct Downstream {
    void(*IN_ClearStates)()=[](){++cleared;};
    void(*IN_ActivateMouse)()=[](){++activated;};
    void(*IN_DeactivateMouse)()=[](){++deactivated;};
} downstream;
struct Input {
    bool down[2]{};int x=-100,y=-100;
    void SetMouseCapture(int){++captureCleared;}
    void SetCursorOveride(int){}
    bool IsMouseDown(int code){return down[code];}
    void GetCursorPos(int& a,int& b){a=x;b=y;}
} inputValue,*input=&inputValue;
struct Surface {
    unsigned GetEmbeddedPanel(){return 1;}
    void SetCursor(int){} void LockCursor(){++cursorLocked;} void UnlockCursor(){}
    void DeleteTextureByID(int){}
} surfaceValue,*surface=&surfaceValue;
struct ClientUI {void HideAllVGUIMenu(){++hidden;}} clientValue,*clientUI=&clientValue;
struct Extension {void RegisterBaseUICallbacks(void*){++overlayRegistrations;}} extensionValue;
namespace vgui {using ISurface=Surface;using IInput=Input;}
using IClientVGUI=ClientUI;
using IVGUI2Extension=Extension;
constexpr char VGUI_SURFACE_INTERFACE_VERSION[]="fixture-surface";
constexpr char CLIENTVGUI_INTERFACE_VERSION[]="fixture-client-ui";
constexpr char VGUI_INPUT_INTERFACE_VERSION[]="fixture-input";
constexpr char VGUI2_EXTENSION_INTERFACE_VERSION[]="fixture-extension";
void* SurfaceFactory(const char*,int*){return &surfaceValue;}
void* ClientFactory(const char*,int*){return &clientValue;}
void* InputFactory(const char*,int*){return &inputValue;}
void* ExtensionFactory(const char*,int*){return &extensionValue;}
HMODULE FixtureModule(const char*){return reinterpret_cast<HMODULE>(1);}
Factory Sys_GetFactory(HMODULE module){return module==reinterpret_cast<HMODULE>(1)?InputFactory:ExtensionFactory;}
struct HookAPI {
    struct CommandRecord {Command function{};};
    CommandRecord*(*FindCmd)(const char*)=[](const char* name)->CommandRecord* {
        static CommandRecord result;const auto found=commandHooks.find(name);
        if(found==commandHooks.end())return nullptr;result.function=found->second;return &result;};
    Command HookCmd(const char* name,Command handler){const auto found=commandHooks.find(name);if(found==commandHooks.end())return nullptr;const auto old=found->second;found->second=handler;return old;}
    MessageHook HookUserMsg(const char* name,MessageHook handler){const auto old=messageHooks[name];messageHooks[name]=handler;return old;}
    Factory GetEngineFactory(){return SurfaceFactory;}Factory GetClientFactory(){return ClientFactory;}
    HMODULE GetPluginModuleHandleByBaseFileName(const char*){return reinterpret_cast<HMODULE>(2);}
} apiValue,*api=&apiValue;
Presentation presentation;
bool capability{},loaded{},visible{},waiting{},catalogReady{},mouseHeld{},swallowMouse{},restoreMouse{};
bool rightHeld{},rebuying{};
int team=2;
Favorites favorites{};
std::vector<int> lastPurchases,rebuyQueue;
std::wstring query;
std::uint16_t request{};
int category=-1,page{},hover=-1,balance{},fontHeight{},left{},top{};
float scale=1,angles[3]{};
double requestedAt{};
std::wstring status;
std::unordered_map<int,Packet> offers;
std::unordered_map<std::string,int> textures;
std::vector<const Item*> visibleRows;
int rowsCategory=-2;
unsigned rejected{},purchases{},draws{},restores{};
constexpr char favoriteKeys[]="sdfgzxcv";
constexpr int IN_ATTACK=1,IN_ATTACK2=2048;
struct usercmd_t {int buttons{};float forwardmove{},sidemove{},upmove{},viewangles[3]{};};
bool Focused(){return focused;}
void(*oldBuy)(){},(*oldCancel)(){},(*oldRebuy)(){};
MessageHook oldVGUI{};
int(*oldShowMenu)(const char*,int,void*){};
void ClearNativeWeaponSelection(){}
void SynchronizeSession(){++syncs;}
void Layout(){}
enum class VGUI2Extension_Result {NONE,SUPERCEDE_SKIP_PLUGINS};
struct VGUI2Extension_CallbackContext {bool IsPost{};void* pPluginReturnValue{};VGUI2Extension_Result Result{};};
struct BaseOverlay {virtual void Key_Event(int&,int&,const char*&,VGUI2Extension_CallbackContext*)=0;};
#include "actual_command_chain.inc"
using csol::client::InstallChainedCommand;
#include "actual_controller.inc"
struct TestOverlay : BaseOverlay {
#include "actual_key.inc"
} overlay;
#define GetModuleHandleA FixtureModule
#include "actual_initialize.inc"
#undef GetModuleHandleA

unsigned checks{};
void Check(bool ok,const char* what){++checks;if(!ok)throw std::runtime_error(what);}
void Reset(){
    visible=true;loaded=capability=true;waiting=false;catalogReady=true;mouseHeld=swallowMouse=restoreMouse=rightHeld=rebuying=false;
    category=-1;page=0;hover=-1;rowsCategory=-2;team=2;balance=32000;request=7;clockTime=requestedAt=0;
    commands.clear();clientCommands.clear();lastPurchases.clear();rebuyQueue.clear();offers.clear();visibleRows.clear();textures.clear();query.clear();status.clear();favorites={};search={};
    focused=true;consoleVisible=false;activated=deactivated=cleared=hidden=captureCleared=cursorLocked=0;inputValue={};inputValue.x=inputValue.y=-100;
    view={};originalRebuys=originalBuys=originalCancels=0;
    for(const auto& item:Catalog())offers.emplace(item.id,Packet{Kind::Item,request,item.id,item.id>=2000?0:100,32000,Result::Available,2});
}
void Deliver(Kind kind,int id=0,Result result=Result::Available,int req=-1,int money=32000){
    auto bytes=Encode({kind,req<0?request:std::uint16_t(req),std::uint16_t(id),100,money,result,2});Receive("CSOBuy",int(bytes.size()),bytes.data());
}
void Key(int key,const char* binding=nullptr){TestOverlay overlay;int down=1,result{};VGUI2Extension_CallbackContext context{false,&result};overlay.Key_Event(down,key,binding,&context);}
void JsonTests(const std::string& original){
    auto p=ParsePresentation(original);Check(p.width==606&&p.favorites.size()==8,"modern dimensions and eight slots");
    Check(p.images.contains(197)&&p.images.at(197).path=="gfx/csol/buy/w197.tga","K3 original purchase image present");
    const auto resources=Resources(p);Check(std::find(resources.begin(),resources.end(),p.keyboard.path)!=resources.end(),"keyboard texture advertised");
    Check(std::find(resources.begin(),resources.end(),PresentationResource)!=resources.end(),"versioned menu advertised to downloads");
    Check(std::find(resources.begin(),resources.end(),"models/csol/buymenu.json")==resources.end(),"old cached menu is not current download");
    Check(std::find(resources.begin(),resources.end(),p.favoriteBackground.path)!=resources.end(),"favorite texture advertised");
    Check(std::adjacent_find(resources.begin(),resources.end())==resources.end(),"resource list unique sorted");
    rapidjson::Document d;d.Parse(original.c_str());
    auto serialize=[&](){rapidjson::StringBuffer s;rapidjson::Writer<rapidjson::StringBuffer>w(s);d.Accept(w);return std::string(s.GetString());};
    d["version"].SetInt(1);for(auto name:{"search","rebuy","teams","favorites","slotBackground","keyboard","favoriteBackground"})d.RemoveMember(name);
    auto legacy=ParsePresentation(serialize());Check(legacy.keyboard.path.empty()&&legacy.favorites[7].w==107,"v1 fallback has geometry without missing textures");
    d.Parse(original.c_str());d["favorites"][0][0].SetInt(600);bool caught=false;try{ParsePresentation(serialize());}catch(...){caught=true;}Check(caught,"out-of-panel favorite rejected");
    d.Parse(original.c_str());d["keyboard"]["path"].SetString("gfx/csol/buy/../outside.tga",d.GetAllocator());caught=false;try{ParsePresentation(serialize());}catch(...){caught=true;}Check(caught,"path traversal rejected");
    d.Parse(original.c_str());d["version"].SetInt(3);caught=false;try{ParsePresentation(serialize());}catch(...){caught=true;}Check(caught,"unknown schema rejected");
}
void CachedResourceTests(const std::string& original){
    rapidjson::Document old;old.Parse(original.c_str());auto& images=old["images"];
    for(auto it=images.Begin();it!=images.End();++it)if((*it)["id"].GetInt()==197){images.Erase(it);break;}
    rapidjson::StringBuffer text;rapidjson::Writer<rapidjson::StringBuffer> writer(text);old.Accept(writer);
    bool rejectedOld=false;try{ParsePresentation(text.GetString());}catch(...){rejectedOld=true;}
    Check(rejectedOld,"old downloaded menu reproduces missing K3 rejection");
    resourceFiles={{"models/csol/buymenu.json",text.GetString()},{PresentationResource,original}};
    loaded=false;presentation={};Check(Load()&&presentation.images.contains(197),"production loader uses new download despite old cached menu");
    Check(lastReadResource==PresentationResource,"loader and server resource paths agree");
    resourceFiles.erase(PresentationResource);loaded=false;const auto before=resourceErrors;
    Check(!Load()&&!loaded&&resourceErrors==before+1,"missing new download is reported without accepting incompatible cache");
    resourceFiles[PresentationResource]=original;Check(Load(),"later valid download recovers loader");
}
void FavoriteTests(){
    Check(Find(197)&&Find(197)->category==5&&Find(197)->extendedPrice==4750,"K3 original purchase category and price");
    const auto f=ParseFavorites("144,220,725,726,22,16,1001,1003");Check(f[0]==144&&f[7]==1003,"eight favorite IDs preserved");
    Check(ParseFavorites(SerializeFavorites(f))==f,"favorites round trip");
    Check(ParseFavorites("144,144,2001,65535,0,22")==Favorites{144,0,0,0,0,22,0,0},"duplicates ammo and unknown IDs rejected without shifting slots");
    for(auto text:{"144;quit","-1","999999999999999","144, a"})Check(ParseFavorites(text)==Favorites{},"invalid favorite text rejected");
    Check(InTeamList(*Find(28),1)&&!InTeamList(*Find(28),2),"AK47 T listing");
    Check(InTeamList(*Find(22),2)&&!InTeamList(*Find(22),1),"M4 CT listing");
    Check(InTeamList(*Find(13),2)&&!InTeamList(*Find(13),1)&&InTeamList(*Find(27),1),"SG550 CT and SG552 T listings");
    Check(InTeamList(*Find(16),1)&&InTeamList(*Find(16),2)&&InTeamList(*Find(17),1)&&InTeamList(*Find(17),2),"USP and Glock both lists");
    Check(InTeamList(*Find(144),1)&&InTeamList(*Find(144),2)&&!InTeamList(*Find(144),0),"special weapon both real teams");
}
void ControllerTests(){
    for(int row:{0,1,2,3,4,7,8}){Reset();Select(row);Check(category==row+1&&commands.empty(),"category number routes without purchasing");}
    for(int row:{5,6}){Reset();Select(row);Check(waiting&&commands.back()=="csol_buy 7 "+std::to_string(row==5?2001:2002)+"\n","ammo shortcut uses fill purchase endpoint");}
    Reset();Select(9);Check(!visible&&activated==1,"root zero closes and restores mouse");
    Reset();category=4;page=1;Select(9);Check(visible&&category==-1&&page==0,"submenu zero returns to root");
    Reset();query=L"";category=4;auto ct=Rows();Check(!ct.empty()&&std::none_of(ct.begin(),ct.end(),[](const Item* i){return i->id==28;}),"CT listing filters T rifle");
    Click(20);auto tr=Rows();Check(team==1&&std::any_of(tr.begin(),tr.end(),[](const Item* i){return i->id==28;}),"team click invalidates row cache");
    Reset();query=L"";category=5;auto machineguns=Rows();Check(std::any_of(machineguns.begin(),machineguns.end(),[](const Item* i){return i->id==197;}),"K3 visible in machinegun category");
    Purchase(*Find(197));Check(waiting&&commands.back()=="csol_buy 7 197\n","K3 uses authoritative purchase endpoint");
    Reset();query=L"";category=8;EditFavorite(0);Check(favorites[0]==144&&std::string(cv[1].string).starts_with("144,"),"right click saves first favorite");
    Click(30);Check(waiting&&commands.back()=="csol_buy 7 144\n","favorite uses authoritative purchase");
    EditFavorite(30);Check(favorites[0]==0,"right click favorite removes");
    Reset();category=1;query=L"";rowsCategory=-2;Key('s');Check(commands.empty(),"empty favorite shortcut does not buy");
    favorites[0]=220;Key('s');Check(waiting&&commands.back()=="csol_buy 7 220\n","favorite hotkey buys its item");
    Reset();query=L"m";rowsCategory=-2;Check(Rows().size()>9,"search fixture spans multiple pages");
    Click(11);const auto id=Rows()[9]->id;Select(0);Check(page==1&&commands.back()=="csol_buy 7 "+std::to_string(id)+"\n","second page starts at ninth offset");
    Reset();search.text=L"黑龙";BuyMenuFrame();Check(query==L"黑龙"&&Rows().size()==1&&Rows()[0]->id==144,"unicode search through native text boundary");
    Reset();Purchase(*Find(144));const auto count=commands.size();Purchase(*Find(220));Check(commands.size()==count,"inflight purchase suppresses double purchase");
    Deliver(Kind::Result,144,Result::Success,6);Check(waiting&&lastPurchases.empty(),"stale receipt ignored");
    Deliver(Kind::Result,144,Result::Failed);Check(visible&&!waiting&&lastPurchases.empty(),"failed purchase not added to rebuy");
    Purchase(*Find(144));Deliver(Kind::Result,144,Result::Success);Check(!visible&&lastPurchases==std::vector<int>{144},"successful receipt records purchase and closes");
    Deliver(Kind::Result,144,Result::Success);Check(lastPurchases.size()==1,"duplicate closed receipt ignored");
    Reset();RememberPurchase(144);RememberPurchase(22);RememberPurchase(220);RememberPurchase(1001);RememberPurchase(1002);RememberPurchase(2001);
    Check(lastPurchases==std::vector<int>({22,220,1002,2001}),"rebuy keeps latest weapon per slot and armor package");
    Click(41);Check(rebuying&&waiting&&commands.back()=="csol_buy 7 22\n","rebuy starts first item once");
    Deliver(Kind::Result,22,Result::Success);Check(visible&&rebuying&&!catalogReady&&request==8&&commands.back()=="csol_buy_menu 8\n","rebuy refreshes fresh request after success");
    ContinueRebuy();Check(commands.back()=="csol_buy_menu 8\n","rebuy waits for complete catalog");
    Deliver(Kind::Begin);Deliver(Kind::Item,220);Deliver(Kind::End);ContinueRebuy();Check(waiting&&commands.back()=="csol_buy 8 220\n","rebuy next item uses new catalog request");
    Reset();RememberPurchase(144);RememberPurchase(2001);RememberPurchase(726);Click(41);Check(commands.back()=="csol_buy 7 726\n"&&rebuyQueue==std::vector<int>{2001},"replacement weapon is rebought before its ammunition");
    Reset();RememberPurchase(144);RememberPurchase(220);Click(41);clockTime=6;BuyMenuFrame();Check(!rebuying&&rebuyQueue.empty(),"timeout cancels rebuy rather than reusing uncertain receipt ID");
    Reset();search.keyboard=true;search.focus=true;Key('b',"buy");Check(visible&&commands.empty(),"focused text bypasses buy hotkey in custom overlay");
    Key(27);Check(!visible&&!search.keyboard&&!search.visible&&activated==1,"escape drops text keyboard and restores mouse");
    Reset();Click(40);Check(search.keyboard&&search.focus,"search click requests native keyboard focus");Click(20);Check(!search.keyboard&&!search.focus,"outside click releases text focus");
    Reset();visible=false;search.keyboard=true;search.focus=true;inputValue.down[vgui::MOUSE_RIGHT]=true;OpenMenu(false);Check(visible&&!search.keyboard&&!search.focus&&rightHeld,"reopening does not steal number keys or misread held right click");
    Reset();consoleVisible=true;Close();Check(restoreMouse&&activated==0,"console blocks immediate mouse capture");consoleVisible=false;usercmd_t cmd{};BuyMenuCreateMove(&cmd,1);Check(!restoreMouse&&activated==1,"gameplay later restores capture");
    Reset();focused=false;Close();Check(restoreMouse&&activated==0,"background process does not steal capture");
    Reset();cmd={IN_ATTACK|IN_ATTACK2,100,100,100,{1,2,3}};BuyMenuCreateMove(&cmd,1);Check(cmd.buttons==0&&cmd.forwardmove==0&&cmd.sidemove==0&&cmd.upmove==0,"menu clears game movement and attack");
    Reset();RememberPurchase(144);rebuying=true;rebuyQueue={220};ResetBuyMenu();Check(!visible&&!capability&&lastPurchases.empty()&&rebuyQueue.empty()&&request==0,"session reset discards rebuy ownership and capabilities");
}
void OriginalRebuy(){++originalRebuys;}
void ProductIdentityTests()
{
    const Item dragon{1022,8,"dragonsword","青龙偃月刀","dragonsword",0,WeaponSlot::Melee};
    const Item axe{1023,8,"skullaxe","旋风 SKULL-9","skullaxe",0,WeaponSlot::Melee};
    for(const auto* item:{&dragon,&axe}){
        Check(IsWeapon(*item)&&!IsEquipment(item->id)&&!IsAmmunition(item->id),"high knife ID remains a weapon");
        Check(RebuySlot(*item)==3&&RebuyOrder(*item)==0,"knife has independent rebuy slot and precedes ammunition");
    }
    Check(RebuySlot(dragon)==RebuySlot(axe),"replacement knife removes previous knife from rebuy history");
    Check(RebuySlot(dragon)!=RebuySlot(*Find(144))&&RebuySlot(dragon)!=RebuySlot(*Find(220)),"knife history preserves both gun slots");
    Check(RebuySlot(*Find(1001))==RebuySlot(*Find(1002)),"vest and helmet keep one equipment history entry");
    for(int id:{1001,1002,1003,1004,1005})Check(IsEquipment(id)&&RebuyOrder(*Find(id))==1,"only five native equipment IDs select BuyItem");
    for(int id:{1000,1006,1022,1023,1999,2001,2002,65535})Check(!IsEquipment(id),"other IDs cannot index the equipment array");
    for(int id:{2001,2002})Check(IsAmmunition(id)&&RebuyOrder(*Find(id))==2,"ammo fills after weapons and equipment");
}
void OriginalBuy(){++originalBuys;}
void OriginalCancel(){++originalCancels;}
int OriginalMessage(const char*,int,void*){return 1;}
void InstallNativeCommands(bool nativeRebuy){
    // LoadClient receives the engine's original registry once. Independent
    // initialization cases must not reuse hooks from the previous fixture.
    csol::client::command_chain::installed.clear();
    commandHooks={{"buy",OriginalBuy},{"cancelselect",OriginalCancel}};
    if(nativeRebuy)commandHooks.emplace("rebuy",OriginalRebuy);
    messageHooks={{"VGUIMenu",OriginalMessage},{"ShowMenu",OriginalMessage}};
    oldBuy=oldCancel=oldRebuy=nullptr;oldVGUI=oldShowMenu=nullptr;
    registeredCommands.clear();registeredCvars.clear();InitializeBuyMenu();
}
void BoundRebuy(){
    // The engine resolves F2's "rebuy" binding before calling this command
    // registry. This executes the callback installed by real Initialize.
    const auto handler=commandHooks.find("rebuy");Check(handler!=commandHooks.end()&&handler->second,"rebuy binding registered");handler->second();
}
void RebuyHookTests(){
    Reset();InstallNativeCommands(true);
    Check(commandHooks.at("rebuy")==Rebuy&&oldRebuy==OriginalRebuy,"production initialization hooks native rebuy");
    Check(std::find(registeredCommands.begin(),registeredCommands.end(),"rebuy")==registeredCommands.end(),"existing native command is replaced rather than duplicated");
    Check(messageHooks.at("CSOBuy")==Receive,"production initialization registers authoritative buy message");
    Check(oldShowMenu==OriginalMessage&&oldVGUI==OriginalMessage,"initialization retains original message consumers");
    InitializeBuyMenu();
    Check(oldRebuy==OriginalRebuy&&oldBuy==OriginalBuy&&oldCancel==OriginalCancel,
        "reinitialization retains native command callbacks instead of self chaining");
    Check(oldShowMenu==OriginalMessage&&oldVGUI==OriginalMessage,
        "reinitialization retains native message callbacks instead of self chaining");
    Check(messageHooks.at("ShowMenu")("ShowMenu",0,nullptr)==1&&
        messageHooks.at("VGUIMenu")("VGUIMenu",0,nullptr)==1,
        "message fallbacks execute after repeated initialization");
    Reset();visible=false;capability=false;commandHooks.at("buy")();commandHooks.at("cancelselect")();
    Check(originalBuys==1&&originalCancels==1,"ordinary command fallbacks execute after repeated initialization");
    Reset();visible=false;capability=false;BoundRebuy();
    Check(originalRebuys==1&&commands.empty()&&!visible,"ordinary server uses original rebuy callback");
    Reset();visible=false;RememberPurchase(144);RememberPurchase(220);BoundRebuy();
    Check(visible&&rebuying&&!waiting&&!catalogReady&&commands==std::vector<std::string>{"csol_buy_menu 8\n"},"F2 command opens authoritative catalog before buying");
    BoundRebuy();Check(commands.size()==1,"repeated F2 does not open a second catalog or duplicate queue");
    Deliver(Kind::Begin);Deliver(Kind::Item,144);Deliver(Kind::Item,220);Deliver(Kind::End);BuyMenuFrame();
    Check(waiting&&commands.back()=="csol_buy 8 144\n","F2 waits for complete fresh catalog before first item");
    Deliver(Kind::Result,144,Result::Success);Deliver(Kind::Begin);Deliver(Kind::Item,220);Deliver(Kind::End);BuyMenuFrame();
    Check(waiting&&commands.back()=="csol_buy 9 220\n","F2 rebuy refreshes ownership for next item");
    Deliver(Kind::Result,220,Result::Success);Deliver(Kind::Begin);Deliver(Kind::End);BuyMenuFrame();
    Check(!visible&&!rebuying&&rebuyQueue.empty()&&activated==1,"completed F2 queue closes and restores mouse");
    for(int invalid=0;invalid<3;++invalid){
        Reset();visible=false;if(invalid!=2)RememberPurchase(144);if(invalid==0)view.health=0;if(invalid==1)view.spectator=true;
        BoundRebuy();Check(!visible&&!rebuying&&commands.empty(),"dead spectator or empty history cannot start rebuy");
    }
    for(bool repeating:{false,true})for(int denied:{0,144}){
        Reset();if(repeating){RememberPurchase(144);RememberPurchase(220);BoundRebuy();}else Purchase(*Find(144));
        const auto issued=commands.size();Deliver(Kind::Result,denied,Result::Unavailable);
        const bool shouldClose=repeating||denied==0;
        Check(visible!=shouldClose&&!waiting,"unavailable closes whole-menu or rebuy denial only");
        Check(commands.size()==issued,"unavailable does not refresh a rejected catalog");
        if(shouldClose){
            Check(!rebuying&&rebuyQueue.empty()&&activated==1,"denial clears rebuy queue and restores input");
            Deliver(Kind::Begin);Deliver(Kind::Item,220);Deliver(Kind::End);clockTime=20;BuyMenuFrame();
            Check(commands.size()==issued&&!visible,"late catalog and frame cannot restart denied rebuy loop");
        }
    }
    Reset();RememberPurchase(144);RememberPurchase(220);BoundRebuy();const auto issued=commands.size();
    Deliver(Kind::Result,0,Result::Unavailable,int(request)-1);
    Check(visible&&waiting&&rebuying&&commands.size()==issued,"stale denial cannot stop current transaction");
    InstallNativeCommands(false);
    Check(commandHooks.at("rebuy")==Rebuy&&std::count(registeredCommands.begin(),registeredCommands.end(),"rebuy")==1,"missing native command is registered once");
    Reset();visible=false;capability=false;BoundRebuy();
    Check(commands==std::vector<std::string>{"rebuy\n"}&&originalRebuys==0&&!visible,"no native callback falls back to original server command");
}
int main(int argc,char**argv){try{if(argc!=2)throw std::runtime_error("require exported menu JSON");std::ifstream f(argv[1],std::ios::binary);std::ostringstream text;text<<f.rdbuf();presentation=ParsePresentation(text.str());JsonTests(text.str());CachedResourceTests(text.str());FavoriteTests();ControllerTests();RebuyHookTests();ProductIdentityTests();std::cout<<"buy menu: "<<checks<<" checks passed\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL "<<checks<<": "<<e.what()<<'\n';return 1;}}
