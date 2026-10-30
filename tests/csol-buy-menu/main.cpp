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
void* extension=reinterpret_cast<void*>(1);
struct Cvar {const char* name;const char* string;};
std::string cvarValues[2];
Cvar cv[2]{{"t",""},{"ct",""}};
Cvar* favoriteCvars[2]{&cv[0],&cv[1]};
std::vector<std::string> commands,clientCommands;
bool focused=true,consoleVisible{};
double clockTime{};
int activated{},deactivated{},cleared{},hidden{},syncs{},captureCleared{},cursorLocked{};
struct Engine {
    double(*GetClientTime)()=[](){return clockTime;};
    int(*pfnServerCmd)(char*)=[](char* s){commands.emplace_back(s);return 0;};
    int(*pfnClientCmd)(char*)=[](char* s){clientCommands.emplace_back(s);return 0;};
    int(*Con_IsVisible)()=[](){return int(consoleVisible);};
    void(*SetViewAngles)(float*)=[](float*){};
    void(*GetViewAngles)(float*)=[](float* values){std::fill_n(values,3,0);};
    void(*Cvar_Set)(const char*,const char*)=[](const char* name,const char* value){int i=!std::strcmp(name,"ct");cvarValues[i]=value;cv[i].string=cvarValues[i].c_str();};
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
bool Load(){return loaded;}
void(*oldBuy)(){};
int(*oldShowMenu)(const char*,int,void*){};
void ClearNativeWeaponSelection(){}
void SynchronizeSession(){++syncs;}
void Layout(){}
enum class VGUI2Extension_Result {NONE,SUPERCEDE_SKIP_PLUGINS};
struct VGUI2Extension_CallbackContext {bool IsPost{};void* pPluginReturnValue{};VGUI2Extension_Result Result{};};
struct BaseOverlay {virtual void Key_Event(int&,int&,const char*&,VGUI2Extension_CallbackContext*)=0;};
#include "actual_controller.inc"
struct TestOverlay : BaseOverlay {
#include "actual_key.inc"
};

unsigned checks{};
void Check(bool ok,const char* what){++checks;if(!ok)throw std::runtime_error(what);}
void Reset(){
    visible=true;loaded=capability=true;waiting=false;catalogReady=true;mouseHeld=swallowMouse=restoreMouse=rightHeld=rebuying=false;
    category=-1;page=0;hover=-1;rowsCategory=-2;team=2;balance=32000;request=7;clockTime=requestedAt=0;
    commands.clear();clientCommands.clear();lastPurchases.clear();rebuyQueue.clear();offers.clear();visibleRows.clear();textures.clear();query.clear();status.clear();favorites={};search={};
    focused=true;consoleVisible=false;activated=deactivated=cleared=hidden=captureCleared=cursorLocked=0;inputValue={};inputValue.x=inputValue.y=-100;
    for(const auto& item:Catalog())offers.emplace(item.id,Packet{Kind::Item,request,item.id,item.id>=2000?0:100,32000,Result::Available,2});
}
void Deliver(Kind kind,int id=0,Result result=Result::Available,int req=-1,int money=32000){
    auto bytes=Encode({kind,req<0?request:std::uint16_t(req),std::uint16_t(id),100,money,result,2});Receive("CSOBuy",int(bytes.size()),bytes.data());
}
void Key(int key,const char* binding=nullptr){TestOverlay overlay;int down=1,result{};VGUI2Extension_CallbackContext context{false,&result};overlay.Key_Event(down,key,binding,&context);}
void JsonTests(const std::string& original){
    auto p=ParsePresentation(original);Check(p.width==606&&p.favorites.size()==8,"modern dimensions and eight slots");
    const auto resources=Resources(p);Check(std::find(resources.begin(),resources.end(),p.keyboard.path)!=resources.end(),"keyboard texture advertised");
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
void FavoriteTests(){
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
int main(int argc,char**argv){try{if(argc!=2)throw std::runtime_error("require exported menu JSON");std::ifstream f(argv[1],std::ios::binary);std::ostringstream text;text<<f.rdbuf();presentation=ParsePresentation(text.str());JsonTests(text.str());FavoriteTests();ControllerTests();std::cout<<"buy menu: "<<checks<<" checks passed\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL "<<checks<<": "<<e.what()<<'\n';return 1;}}
