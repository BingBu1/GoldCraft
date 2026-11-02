// SPDX-License-Identifier: GPL-3.0-or-later
// Execute actual compiled plugin/stock cmd_access. Engine/menu/service natives
// below are explicit controlled boundaries, not a real-server provider test.
#include "amx.h"
#include <algorithm>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr int AdminCfg=1<<7;
int checks{},failures{};
void Check(bool ok,const char* text){++checks;if(!ok){++failures;std::cerr<<"FAIL "<<text<<'\n';}}
cell* Addr(AMX* amx,cell value){cell* p{};if(amx_GetAddr(amx,value,&p))throw std::runtime_error("bad AMX address");return p;}
std::string Read(AMX* amx,cell value){std::string s;for(auto* p=Addr(amx,value);*p;++p)s+=static_cast<char>(*p);return s;}
cell Write(AMX* amx,cell value,const std::string& text,int max){auto* p=Addr(amx,value);auto count=std::min(text.size(),std::size_t(std::max(0,max)));for(std::size_t i=0;i<count;++i)p[i]=static_cast<unsigned char>(text[i]);p[count]=0;return static_cast<cell>(count);}
struct Menu{std::string title,handler;std::vector<std::pair<std::string,std::string>> items;bool destroyed{};};
struct Context{
    int mode{2},flags{AdminCfg},active{2},pending{4},maximum{6},map{98},nextMenu{1},shown{-1},requests{},writes{},destroys{};
    bool connected{true},available{true},replayPending{};
    int joining{},replayRequests{},replays{}; // Matched ReAPI JoinState: JOINED = 0.
    std::vector<int> replayMaps;
    std::function<void()> onDestroy;
    std::vector<std::string> args{"amx_scenario_difficulty"};
    std::map<int,Menu> menus;
    std::map<int,std::string> cvars{{1,"0"},{2,"zs_deadend"}};
    std::vector<std::pair<int,int>> submissions;
};
Context* context{};
cell AMX_NATIVE_CALL Reject(AMX* amx,cell*){amx_RaiseError(amx,AMX_ERR_NATIVE);return 0;}
cell AMX_NATIVE_CALL One(AMX*,cell*){return 1;}
cell AMX_NATIVE_CALL Zero(AMX*,cell*){return 0;}
cell AMX_NATIVE_CALL Flags(AMX*,cell*){return context->flags;}
cell AMX_NATIVE_CALL Connected(AMX*,cell*){return context->connected;}
cell AMX_NATIVE_CALL Mode(AMX*,cell*){return context->mode;}
cell AMX_NATIVE_CALL Member(AMX*,cell*){return context->joining;}
cell AMX_NATIVE_CALL Argc(AMX*,cell*){return static_cast<cell>(context->args.size());}
cell AMX_NATIVE_CALL Argv(AMX* amx,cell* p){return Write(amx,p[2],context->args.at(p[1]),p[3]);}
cell AMX_NATIVE_CALL Length(AMX* amx,cell* p){return static_cast<cell>(Read(amx,p[1]).size());}
cell AMX_NATIVE_CALL Copy(AMX* amx,cell* p){return Write(amx,p[1],Read(amx,p[3]),p[2]);}
cell AMX_NATIVE_CALL Equal(AMX* amx,cell* p){auto a=Read(amx,p[1]),b=Read(amx,p[2]);return p[3]>0?a.substr(0,p[3])==b.substr(0,p[3]):a==b;}
cell AMX_NATIVE_CALL NumToStr(AMX* amx,cell* p){return Write(amx,p[2],std::to_string(p[1]),p[3]);}
cell AMX_NATIVE_CALL StrToNum(AMX* amx,cell* p){return std::atoi(Read(amx,p[1]).c_str());}
cell AMX_NATIVE_CALL Format(AMX* amx,cell* p){
    const auto fmt=Read(amx,p[3]);std::string out;int arg=4;
    for(std::size_t i=0;i<fmt.size();++i){if(fmt[i]!='%'){out+=fmt[i];continue;}if(++i==fmt.size())throw std::runtime_error("bad format");
        if(fmt[i]=='s')out+=Read(amx,p[arg++]);else if(fmt[i]=='d')out+=std::to_string(*Addr(amx,p[arg++]));else if(fmt[i]=='%')out+='%';else throw std::runtime_error("unexpected tested format");}
    return Write(amx,p[1],out,p[2]);
}
cell AMX_NATIVE_CALL Pointer(AMX* amx,cell* p){auto s=Read(amx,p[1]);if(s=="csol_scenario_finish_action")return 1;if(s=="csol_scenario_nextmap")return 2;return 0;}
cell AMX_NATIVE_CALL GetCvar(AMX* amx,cell* p){return Write(amx,p[2],context->cvars.at(p[1]),p[3]);}
cell AMX_NATIVE_CALL SetCvar(AMX* amx,cell* p){context->cvars.at(p[1])=Read(amx,p[2]);return 1;}
cell AMX_NATIVE_CALL MapName(AMX* amx,cell* p){return Write(amx,p[1],"zs_deadend",p[2]);}
cell AMX_NATIVE_CALL MenuCreate(AMX* amx,cell* p){auto& c=*context;const int id=c.nextMenu++;c.menus[id]={Read(amx,p[1]),Read(amx,p[2]),{},false};return id;}
cell AMX_NATIVE_CALL MenuAdd(AMX* amx,cell* p){auto& menu=context->menus.at(p[1]);Check(!menu.destroyed,"add item to live menu");menu.items.emplace_back(Read(amx,p[2]),Read(amx,p[3]));return 1;}
cell AMX_NATIVE_CALL MenuDisplay(AMX*,cell* p){Check(!context->menus.at(p[2]).destroyed,"display live menu");context->shown=p[2];return 1;}
cell AMX_NATIVE_CALL MenuDestroy(AMX*,cell* p){auto& menu=context->menus.at(p[1]);Check(!menu.destroyed,"destroy menu exactly once");menu.destroyed=true;++context->destroys;auto callback=context->onDestroy;if(callback)callback();return 1;}
cell AMX_NATIVE_CALL MenuInfo(AMX* amx,cell* p){auto& menu=context->menus.at(p[1]);Check(!menu.destroyed,"read item before menu destruction");if(p[2]<0||p[2]>=static_cast<int>(menu.items.size()))return 0;*Addr(amx,p[3])=0;Write(amx,p[4],menu.items[p[2]].second,p[5]);*Addr(amx,p[8])=-1;return 1;}
cell AMX_NATIVE_CALL Info(AMX* amx,cell* p){auto& c=*context;if(!c.available)return 0;*Addr(amx,p[1])=c.active;*Addr(amx,p[2])=c.pending;*Addr(amx,p[3])=c.maximum;*Addr(amx,p[4])=c.map;return 1;}
cell AMX_NATIVE_CALL Request(AMX*,cell* p){auto& c=*context;++c.requests;c.submissions.emplace_back(p[1],p[2]);if(!c.available||p[1]!=c.map||p[2]<1||p[2]>c.maximum)return 0;c.pending=p[2];++c.writes;return 1;}
cell AMX_NATIVE_CALL Replay(AMX*,cell* p){auto& c=*context;++c.replayRequests;c.replayMaps.push_back(p[1]);if(!c.available||c.replayPending||p[1]!=c.map||c.pending<1||c.pending>c.maximum)return 0;c.replayPending=true;++c.replays;return 1;}
const AMX_NATIVE_INFO natives[]{
    {"get_user_flags",Flags},{"is_dedicated_server",One},{"console_print",One},{"client_print",One},
    {"read_argc",Argc},{"read_argv",Argv},{"register_plugin",One},{"register_concmd",One},{"register_clcmd",One},{"get_cvar_pointer",Pointer},
    {"csol_scenario_admin_api_version",One},{"csol_game_mode_api_version",One},{"csol_game_mode_current",Mode},
    {"set_fail_state",Reject},{"strlen",Length},{"copy",Copy},{"equal",Equal},{"formatex",Format},
    {"get_pcvar_string",GetCvar},{"set_pcvar_string",SetCvar},{"get_mapname",MapName},{"is_map_valid",One},
    {"menu_create",MenuCreate},{"menu_additem",MenuAdd},{"menu_display",MenuDisplay},{"menu_destroy",MenuDestroy},
    {"menu_setprop",One},{"menu_item_getinfo",MenuInfo},{"is_user_connected",Connected},{"get_user_userid",One},
    {"csol_scenario_difficulty_info",Info},{"csol_scenario_difficulty_request",Request},
    {"csol_scenario_replay_api_version",One},{"csol_scenario_replay",Replay},{"get_member",Member},{"get_member_s",Member},
    {"num_to_str",NumToStr},{"str_to_num",StrToNum},{"log_amx",One},{nullptr,nullptr}};
std::vector<unsigned char> bytecode;
struct VM{
    Context state;std::vector<unsigned char> memory=bytecode;AMX amx{};
    VM(){context=&state;const auto* h=reinterpret_cast<const AMX_HEADER*>(memory.data());memory.resize(std::max(memory.size(),std::size_t(h->stp)));
        if(amx_Init(&amx,memory.data()))throw std::runtime_error("AMX init");
        amx_Register(&amx,natives,-1);
        int count{};amx_NumNatives(&amx,&count);for(int i=0;i<count;++i){char name[128]{};amx_GetNative(&amx,i,name);AMX_NATIVE_INFO row[2]{{name,Reject},{nullptr,nullptr}};amx_Register(&amx,row,-1);}
        if(amx_Register(&amx,nullptr,0))throw std::runtime_error("AMX natives");Call("plugin_init");}
    ~VM(){Call("plugin_end");amx_Cleanup(&amx);context=nullptr;}
    cell Call(const char* name,std::initializer_list<cell> args={}){context=&state;int index{};if(amx_FindPublic(&amx,name,&index))throw std::runtime_error(std::string("missing public ")+name);for(auto it=std::rbegin(args);it!=std::rend(args);++it)amx_Push(&amx,*it);cell result{};int error=amx_Exec(&amx,&result,index);if(error)throw std::runtime_error(std::string(name)+" AMX error "+std::to_string(error));return result;}
    void Command(const std::vector<std::string>& args,int player=1){state.args=args;Call("CommandDifficulty",{player,AdminCfg,1});}
    int Open(){Command({"amx_scenario_difficulty"});return state.shown;}
    int OpenFinish(){state.args={"amx_scenario_menu"};Call("CommandMenu",{1,AdminCfg,1});return state.shown;}
    void ReplayCommand(int player=1){state.args={"amx_scenario_replay"};Call("CommandReplay",{player,AdminCfg,1});}
    void Choose(int menu,int item){Call("DifficultyMenuHandler",{1,menu,item});}
};
void Cases(){
    {VM v;v.state.flags=0;v.Command({"amx_scenario_difficulty","4"});Check(v.state.requests==0&&v.state.pending==4,"ordinary client command denied by actual cmd_access");
        v.Command({"amx_scenario_difficulty","3"},0);Check(v.state.writes==1&&v.state.pending==3&&v.state.active==2,"dedicated console can change pending without active mutation");}
    {VM v;for(const auto& args:std::vector<std::vector<std::string>>{{"cmd","0"},{"cmd","7"},{"cmd","8"},{"cmd","-1"},{"cmd","2x"},{"cmd","2","3"}})v.Command(args);
        Check(v.state.writes==0&&v.state.pending==4&&v.state.requests==1&&v.state.submissions[0]==std::pair<int,int>{98,7},"command rejects malformed/zero input and unavailable seventh grade");}
    {VM v;int menu=v.Open();const auto& m=v.state.menus.at(menu);Check(m.items.size()==6&&m.items.front().first=="入门"&&m.items.back().first=="炼狱","map cap gives exactly six original Chinese grades");
        Check(m.title.find("当前：普通")!=m.title.npos&&m.title.find("待用：噩梦")!=m.title.npos,"current and pending labels remain separate");
        v.Choose(menu,2);Check(v.state.pending==3&&v.state.active==2&&v.state.writes==1&&v.state.shown!=menu,"menu submission updates pending only and replaces old menu");
        const int current=v.state.shown,destroys=v.state.destroys;v.Choose(menu,0);Check(v.state.writes==1&&v.state.shown==current&&v.state.destroys==destroys&&!v.state.menus.at(current).destroyed,"old menu callback neither writes nor destroys the new menu");}
    for(int variant=0;variant<3;++variant){VM v;int menu=v.Open();if(variant==0)v.state.flags=0;else if(variant==1)v.state.connected=false;else v.state.mode=1;v.Choose(menu,0);Check(v.state.requests==0&&v.state.menus.at(menu).destroyed,"submission rechecks permissions connection and scenario mode");}
    {VM v;int menu=v.Open();v.state.map=99;v.Choose(menu,0);Check(v.state.requests==1&&v.state.writes==0&&v.state.submissions[0].first==98,"stale menu retains captured map identity and provider denies it");}
    {VM v;int menu=v.Open();v.Choose(menu,-3);int count=v.state.destroys;v.Choose(menu,-3);Check(v.state.requests==0&&v.state.destroys==count,"exit and repeated cancel do not write or double-destroy");}
    {VM v;int menu=v.Open();v.Call("client_disconnected",{1});int count=v.state.destroys;v.Choose(menu,0);Check(v.state.destroys==count&&v.state.requests==0,"disconnect removes ownership before late callback");}
    {VM v;v.state.pending=0;int menu=v.Open();Check(v.state.menus.at(menu).title.find("待用设置无效")!=std::string::npos,"invalid pending is visible without changing current");}
    {VM v;v.state.available=false;v.Open();Check(v.state.shown==-1&&v.state.requests==0,"unavailable service creates no menu");}
}
void ReplayCases(){
    {VM v;v.state.flags=0;v.ReplayCommand();Check(v.state.replayRequests==0,"ordinary client cannot request replay through actual cmd_access");
        v.ReplayCommand(0);Check(v.state.replays==1&&v.state.replayMaps==std::vector<int>{98}&&v.state.active==2,"dedicated console queues exact current map without mutating active difficulty");
        v.ReplayCommand(0);Check(v.state.replayRequests==2&&v.state.replays==1,"duplicate command reaches provider refusal without claiming another replay");}
    for(int variant=0;variant<2;++variant){VM v;if(variant==0)v.state.available=false;else v.state.mode=1;v.ReplayCommand();
        Check(v.state.replayRequests==0,"unready provider and non-scenario mode cannot queue replay");}
    {VM v;int menu=v.OpenFinish();Check(v.state.menus.at(menu).items.size()==7&&v.state.menus.at(menu).items[5].first.find("立即重玩本图")!=std::string::npos,"actual management menu exposes same-map replay");
        v.Call("FinishMenuHandler",{1,menu,5});Check(v.state.replays==1&&v.state.replayMaps==std::vector<int>{98}&&v.state.menus.at(menu).destroyed,"menu queues captured map once and releases ownership");
        int destroys=v.state.destroys;v.Call("FinishMenuHandler",{1,menu,5});Check(v.state.replayRequests==1&&v.state.destroys==destroys,"duplicate old replay callback is ignored without another destroy");}
    for(int variant=0;variant<3;++variant){VM v;int menu=v.OpenFinish();if(variant==0)v.state.flags=0;else if(variant==1)v.state.connected=false;else v.state.mode=1;
        v.Call("FinishMenuHandler",{1,menu,5});Check(v.state.replayRequests==0&&v.state.menus.at(menu).destroyed,"replay menu rechecks permission connection and mode before provider");}
    {VM v;int menu=v.OpenFinish();v.state.map=99;v.Call("FinishMenuHandler",{1,menu,5});
        Check(v.state.replayRequests==1&&v.state.replays==0&&v.state.replayMaps==std::vector<int>{98},"old management menu retains map identity for provider rejection");}
    {VM v;int menu=v.OpenFinish();bool once=true;v.state.onDestroy=[&]{if(once){once=false;v.state.map=99;v.OpenFinish();}};
        v.Call("FinishMenuHandler",{1,menu,5});v.state.onDestroy={};
        Check(v.state.replayMaps==std::vector<int>{98}&&v.state.replays==0&&v.state.shown!=menu&&
            !v.state.menus.at(v.state.shown).destroyed,"destroy callback cannot retarget old replay to replacement menu map");}
    {VM v;int old=v.OpenFinish(),fresh=v.OpenFinish();int destroys=v.state.destroys;v.Call("FinishMenuHandler",{1,old,5});
        Check(v.state.replayRequests==0&&v.state.shown==fresh&&v.state.destroys==destroys&&!v.state.menus.at(fresh).destroyed,"old management callback cannot consume replacement menu");}
    {VM v;int menu=v.OpenFinish();v.Call("client_disconnected",{1});int destroys=v.state.destroys;v.Call("FinishMenuHandler",{1,menu,5});
        Check(v.state.replayRequests==0&&v.state.destroys==destroys,"disconnect retires replay menu before late callback");}
    {VM v;int menu=v.OpenFinish();v.Call("FinishMenuHandler",{1,menu,2});
        Check(v.state.cvars.at(1)=="2"&&v.state.replayRequests==0,"finish policy selection saves action2 without immediate replay");}
    {VM v;v.state.args={"amx_scenario_finish","2"};v.Call("CommandFinish",{1,AdminCfg,1});
        Check(v.state.cvars.at(1)=="2"&&v.state.replayRequests==0,"console finish policy accepts same-map replay separately");}
    for(int variant=0;variant<4;++variant){VM v;if(variant==0)v.state.flags=0;else if(variant==1)v.state.connected=false;else if(variant==2)v.state.mode=1;else v.state.joining=1;
        Check(v.Call("CommandChooseTeam",{1})==0&&v.state.shown==-1,"M preserves native selection for denied or not-yet-joined player");}
    {VM v;Check(v.Call("CommandChooseTeam",{1})==1&&v.state.shown>0,"joined scenario administrator opens management with native M command");}
}
void VoteCases(){
    {VM v;int menu=v.OpenFinish();Check(v.state.menus.at(menu).items[6].first.find("投票")!=std::string::npos,"seventh management item selects map voting");
        v.Call("FinishMenuHandler",{1,menu,6});Check(v.state.cvars.at(1)=="3"&&v.state.replayRequests==0,"vote policy is saved without immediate replay");
        Check(v.state.menus.at(v.state.shown).title.find("投票选择下一张地图")!=std::string::npos,"reopened menu displays actual vote policy");}
    {VM v;v.state.args={"amx_scenario_finish","3"};v.Call("CommandFinish",{1,AdminCfg,1});
        Check(v.state.cvars.at(1)=="3"&&v.state.replayRequests==0,"console accepts vote action without restart");}
    for(int variant=0;variant<2;++variant){VM v;if(variant==0)v.state.flags=0;else v.state.mode=1;
        v.state.args={"amx_scenario_finish","3"};const auto original=v.state.cvars.at(1);v.Call("CommandFinish",{1,AdminCfg,1});
        Check(v.state.cvars.at(1)==original,"vote command rechecks administrator and scenario mode");}
    for(const char* invalid:{"3x","03","4","-1","3 "}){VM v;const auto original=v.state.cvars.at(1);v.state.args={"amx_scenario_finish",invalid};
        v.Call("CommandFinish",{1,AdminCfg,1});Check(v.state.cvars.at(1)==original,"malformed vote policy cannot overwrite configured action");}
    for(int variant=0;variant<3;++variant){VM v;int menu=v.OpenFinish();const auto original=v.state.cvars.at(1);
        if(variant==0)v.state.flags=0;else if(variant==1)v.state.connected=false;else v.state.mode=1;
        v.Call("FinishMenuHandler",{1,menu,6});Check(v.state.cvars.at(1)==original&&v.state.menus.at(menu).destroyed,"vote menu rechecks permissions connection and mode");}
    {VM v;int old=v.OpenFinish(),fresh=v.OpenFinish();const auto original=v.state.cvars.at(1);v.Call("FinishMenuHandler",{1,old,6});
        Check(v.state.cvars.at(1)==original&&v.state.shown==fresh&&!v.state.menus.at(fresh).destroyed,"old vote callback cannot consume or retarget replacement menu");}
}

}
int main(int argc,char** argv){static_assert(sizeof(void*)==4&&sizeof(cell)==4);if(argc!=2)return 2;
    try{std::ifstream f(argv[1],std::ios::binary);bytecode.assign(std::istreambuf_iterator<char>(f),{});if(bytecode.size()<sizeof(AMX_HEADER))throw std::runtime_error("missing bytecode");Cases();ReplayCases();VoteCases();}
    catch(const std::exception& e){++failures;std::cerr<<"EXCEPTION "<<e.what()<<'\n';}
    std::cout<<"scenario-admin pawn "<<checks<<" checks "<<failures<<" failures\n";return failures?1:0;
}
