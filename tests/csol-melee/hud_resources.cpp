// SPDX-License-Identifier: GPL-3.0-or-later
// Links complete production inventory, pickup and death-notice translation
// units. Only engine callbacks are controlled; no rendered-pixel claim.
#include "../../native/csol/client/runtime.hpp"
#include "../../native/csol/client/weapon_inventory.hpp"
#include "../../native/csol/client/pickup_history.hpp"
#include "../../native/csol/client/death_notices.hpp"
#include "../../native/csol/weapon_resources.hpp"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <source_location>

namespace fixture {
unsigned checks{},failures{},hookCalls{},stringDraws{},fills{};
int width=1280,height=720,currentSprite{};
float now=10;
std::map<std::string,pfnUserMsgHook> messages;
std::map<std::string,xcommand_t> commands;
std::map<std::string,std::vector<client_sprite_t>> lists;
std::map<std::string,int> handles;
std::vector<std::string> sent;
struct Draw {int handle{},x{},y{};wrect_t rect{};std::array<int,3> rgb{};};
std::vector<Draw> draws;
std::array<int,3> rgb{};
cvar_t option{};
void Check(bool ok,const char* label,std::source_location at=std::source_location::current()) {
    ++checks;if(!ok){++failures;std::cerr<<"FAIL "<<at.line()<<' '<<label<<'\n';}
}
bool Same(wrect_t a,wrect_t b){return a.left==b.left&&a.right==b.right&&a.top==b.top&&a.bottom==b.bottom;}
void NativeCommand(){}
int NativeMessage(const char*,int,void*){return 1;}
client_sprite_t* GetList(char* path,int* count) {
    if(!path||!count)return nullptr;
    const auto found=lists.find(path);
    if(found==lists.end()){*count=0;return nullptr;}
    *count=int(found->second.size());return found->second.data();
}
client_sprite_t Sprite(const char* name,const char* atlas,int res,wrect_t rect) {
    client_sprite_t row{};std::strcpy(row.szName,name);std::strcpy(row.szSprite,atlas);row.iRes=res;row.rc=rect;return row;
}
void Send(const char* name,const std::vector<std::byte>& bytes) {
    messages.at(name)(name,int(bytes.size()),const_cast<std::byte*>(bytes.data()));
}
void PrepareKnife(const std::filesystem::path& common,const char* name) {
    std::ifstream source(common/("sprites/knife_"+std::string(name)+".txt"));
    std::string atlas,symbol;int x{},y{},w{},h{};source>>atlas>>x>>y>>w>>h>>symbol;
    Check(bool(source)&&symbol=="d_"+std::string(name),"original knife descriptor parsed");
    const std::string path="sprites/weapon_"+std::string(name)+".txt";
    Check(csol::WeaponResourceSource(path)=="sprites/knife_"+std::string(name)+".txt","root source remaps generated descriptor");
    std::istringstream generated(std::string(csol::GeneratedWeaponResource(path)));
    int count{};generated>>count;Check(count==2,"exactly two selection roles, no fabricated ammo");
    auto& rows=lists[path];rows.clear();
    for(auto* role:{"weapon","weapon_s"}) {
        std::string actualRole,actualAtlas;int res{},ax{},ay{},aw{},ah{};
        generated>>actualRole>>res>>actualAtlas>>ax>>ay>>aw>>ah;
        Check(generated&&actualRole==role&&res==640&&actualAtlas=="csol/"+atlas&&
            x==ax&&y==ay&&w==aw&&h==ah,"generated role preserves original atlas and rectangle");
        rows.push_back(Sprite(actualRole.c_str(),actualAtlas.c_str(),res,{ax,ax+aw,ay,ay+ah}));
    }
    std::string tail;Check(!(generated>>tail),"no unexpected generated descriptor row");
}
}
namespace csol::client {
cl_enginefunc_t engine{};cl_exportfuncs_t downstream{};ref_params_t view{};metahook_api_t* api{};
void SynchronizeSession(){}
void ReceiveExtendedWeaponDefinition(const WeaponDefinition&){}
void ReceiveExtendedWeaponState(const WeaponCurrent&){}
void ClearNativeWeaponSelection(){}
bool TryScenarioHealingSlot(int){return false;}
bool TryActivateScenarioAbility(){return false;}
hook_t* Hook(void* address,void* replacement,void** original) {
    ++fixture::hookCalls;*original=address;engine.pfnSPR_GetList=reinterpret_cast<decltype(engine.pfnSPR_GetList)>(replacement);return nullptr;
}
}
int wmain(int argc,wchar_t**argv) {
    using namespace csol;using namespace csol::client;using namespace fixture;
    if(argc!=2)return 2;
    const std::filesystem::path common(argv[1]);
    PrepareKnife(common,"dragonsword");PrepareKnife(common,"skullaxe");
    auto& native=lists["sprites/hud.txt"];
    native.push_back(Sprite("d_skullaxe","existing/skullaxe",640,{1,3,2,4}));
    for(int res:{320,640})for(int bucket=1;bucket<=5;++bucket)
        native.push_back(Sprite(("bucket"+std::to_string(bucket)).c_str(),"native/buckets",res,{0,20,0,20}));
    engine.pfnSPR_GetList=GetList;InitializeDeathNotices();InitializeDeathNotices();
    Check(hookCalls==1,"death hook installed only once");
    char hud[]="sprites/hud.txt";int count{};auto* extended=engine.pfnSPR_GetList(hud,&count);
    Check(extended&&count==int(native.size())+13,"seven death icons at both resolutions preserve existing duplicate");
    for(auto* name:{"dragonsword","skullaxe"})for(int res:{320,640}) {
        const std::string symbol="d_"+std::string(name);const client_sprite_t* found=nullptr;unsigned matches{};
        for(int i=0;i<count;++i)if(extended[i].iRes==res&&extended[i].szName==symbol){found=extended+i;++matches;}
        Check(matches==1&&found,"one death icon per symbol and resolution");if(!found)continue;
        if(symbol=="d_skullaxe"&&res==640)Check(Same(found->rc,{1,3,2,4})&&std::string(found->szSprite)=="existing/skullaxe","preexisting death icon preserved");
        else {
            std::ifstream file(common/"sprites/hud.txt",std::ios::binary);std::string line;bool originalFound=false;
            while(std::getline(file,line)) {std::istringstream row(line);std::string token,atlas;int r{},x{},y{},w{},h{};
                if(row>>token>>r>>atlas>>x>>y>>w>>h;token==symbol&&r==640) {
                    originalFound=true;Check(Same(found->rc,{x,x+w,y,y+h})&&std::string(found->szSprite)=="csol/"+atlas,"death adapter matches original hud.txt coordinates");break;
                }}
            Check(originalFound,"original death oracle found");
        }
    }
    auto* again=engine.pfnSPR_GetList(hud,&count);Check(again==extended,"death list address stable across resets");
    char unrelated[]="sprites/weapon_skullaxe.txt";int unrelatedCount{};
    Check(engine.pfnSPR_GetList(unrelated,&unrelatedCount)==lists[unrelated].data()&&unrelatedCount==2,"non-hud sprite list forwarded untouched");
    Check(engine.pfnSPR_GetList(nullptr,&count)==nullptr&&engine.pfnSPR_GetList(hud,nullptr)==nullptr,"invalid engine query preserved");
    metahook_api_t hooks{};api=&hooks;
    hooks.HookCmd=[](const char* name,xcommand_t fn)->xcommand_t{commands[name]=fn;return NativeCommand;};
    hooks.HookUserMsg=[](const char* name,pfnUserMsgHook fn)->pfnUserMsgHook{messages[name]=fn;return NativeMessage;};
    engine.pfnHookUserMsg=[](const char* name,pfnUserMsgHook fn)->int{messages[name]=fn;return 1;};
    engine.pfnAddCommand=[](const char* name,xcommand_t fn)->int{commands[name]=fn;return 1;};
    engine.pfnRegisterVariable=[](const char*,const char*,int)->cvar_t*{return &option;};
    engine.Cvar_SetValue=[](const char*,float){};
    engine.GetClientTime=[]()->float{return now;};
    engine.pfnGetCvarFloat=[](const char* name)->float{return !std::strcmp(name,"hud_fastswitch")?0.f:5.f;};
    engine.pfnServerCmd=[](const char* name)->int{sent.emplace_back(name);return 1;};
    engine.pfnGetScreenInfo=[](SCREENINFO* screen)->int{screen->iWidth=width;screen->iHeight=height;return 1;};
    engine.pfnSPR_Load=[](const char* path)->int{auto [it,added]=handles.try_emplace(path,int(handles.size())+1);return it->second;};
    engine.pfnSPR_Set=[](int h,int r,int g,int b){currentSprite=h;rgb={r,g,b};};
    engine.pfnSPR_DrawAdditive=[](int,int x,int y,const wrect_t* rect){draws.push_back({currentSprite,x,y,*rect,rgb});};
    engine.pfnDrawString=[](int,int,const char*,int,int,int)->int{++stringDraws;return 1;};
    engine.pfnDrawStringReverse=[](int,int,const char*,int,int,int)->int{++stringDraws;return 1;};
    engine.pfnFillRGBA=[](int,int,int,int,int,int,int,int){++fills;};
    InitializeWeaponInventory();InitializePickupHistory();
    for(int screenWidth:{1280,480}) {
        width=screenWidth;view.health=100;ResetWeaponInventory(true);ResetPickupHistory();WeaponInventoryVideoInit();PickupHistoryVideoInit();
        constexpr int sword=std::uint16_t(139),axe=std::uint16_t(115); // Fixture IDs are extended protocol keys only.
        for(auto [id,name]:{std::pair{sword,"dragonsword"},std::pair{axe,"skullaxe"}})
            Send(WeaponDefinitionMessage,EncodeWeaponDefinition({id,2,id,-1,-1,-1,-1,-1,0,"weapon_"+std::string(name)}));
        WeaponOwnership own{};own[sword]=own[axe]=true;
        for(const auto& packet:EncodeWeaponInventory(own,1))Send(WeaponInventoryMessage,packet);
        Check(WeaponHasAmmo(sword)&&WeaponHasAmmo(axe),"ammo-free knives remain selectable");
        for(int select=0;select<2;++select) {
            draws.clear();fills=stringDraws=0;commands.at("slot3")();DrawWeaponSelection(0,0);
            for(auto* name:{"skullaxe","dragonsword"}) {
                const auto& row=lists["sprites/weapon_"+std::string(name)+".txt"][0];const int handle=handles.at("sprites/"+std::string(row.szSprite)+".spr");
                const auto found=std::find_if(draws.begin(),draws.end(),[&](const Draw& d){return d.handle==handle;});
                Check(found!=draws.end()&&Same(found->rect,row.rc),"actual selection consumer draws original knife card at both resolutions");
            }
            Check(!stringDraws&&!fills,"loaded cards avoid text fallback and fake ammunition bars");
        }
        commands.at("cancelselect")();
        for(auto [id,name]:{std::pair{sword,"dragonsword"},std::pair{axe,"skullaxe"}}) {
            ResetPickupHistory();draws.clear();fills=stringDraws=0;
            std::vector<std::byte> msg{std::byte(WeaponProtocolVersion),std::byte(id),std::byte(id>>8)};Send(WeaponPickupMessage,msg);
            DrawPickupHistory(0,0);const auto& row=lists["sprites/weapon_"+std::string(name)+".txt"][0];
            Check(draws.size()==1&&Same(draws[0].rect,row.rc),"actual weapon pickup message draws original knife card");
            Check(!draws.empty()&&draws[0].x==width-170&&draws[0].rgb==std::array<int,3>{255,160,0},"ammo-free knife pickup aligns to right edge and keeps selectable tint");
            Check(!stringDraws&&!fills,"weapon pickup invents no ammo digits or bars");
            draws.clear();DrawPickupHistory(1,0);Check(draws.empty(),"hidden pickup is suppressed");
        }
        now+=6;draws.clear();DrawPickupHistory(0,0);Check(draws.empty(),"expired knife pickup is retired");
    }
    Check(engine.pfnSPR_GetList(hud,&count)==extended,"inventory video resets retain stable death sprite list");
    std::cout<<"melee HUD resources "<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}
