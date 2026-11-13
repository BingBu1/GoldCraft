// SPDX-License-Identifier: GPL-3.0-or-later
// Reuse the established AMX host/menu primitives; execute the actual freshly
// compiled .sma. Settings/file/auth providers remain controlled native edges.
#define main legacy_scenario_admin_fixture_main
#include "../csol-scenario-admin/pawn.cpp"
#undef main
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace custom_menu_test
{
struct Field { std::string key, comment; int type; float value, minimum, maximum; int nextMap{}, available{1}; };
struct State : Context
{
    int userid{71}, revision{5}, status{2}, busy{}, edits{}, editIndex{-1}, expectedUserid{}, expectedRevision{};
    bool settingsAvailable{true}, immediateSaved{};
    float editValue{};
    std::string error;
    std::vector<Field> fields{{"kick", "踢击开关，可反复修改。",0,1,0,1},
        {"midday_seconds","正午持续秒数。",1,180,1,86400,1},
        {"dusk_speed_multiplier","黄昏移速自定义倍率。",2,1.05f,.001f,1000,1},
        {"sprint_maximum","未知原体力，不提供猜测默认。",2,0,.001f,1000000,1,0}};
    std::vector<std::string> notices, commands;
    struct Dispatch {int player;std::string command,arg1,arg2;bool menuDestroyed;};
    std::vector<Dispatch> engineCommands;
    std::map<int,std::string> tasks;
    void Finish(bool success)
    {
        busy=0;status=success?2:-1;
        if(success){++revision;fields.at(editIndex).value=editValue;fields.at(editIndex).available=1;}
        else error="controlled disk failure";
    }
};
State* state{};
cell AMX_NATIVE_CALL Userid(AMX*,cell*){return state->userid;}
cell AMX_NATIVE_CALL Notice(AMX* amx,cell* p)
{
    auto text=Read(amx,p[3]);int argument=4;
    for(auto at=text.find("%s");at!=text.npos&&argument<=p[0]/cell(sizeof(cell));at=text.find("%s",at))
    {const auto value=Read(amx,p[argument++]);text.replace(at,2,value);at+=value.size();}
    state->notices.push_back(std::move(text));return 1;
}
cell AMX_NATIVE_CALL Command(AMX* amx,cell* p){state->commands.push_back(Read(amx,p[2]));return 1;}
cell AMX_NATIVE_CALL EngineCommand(AMX* amx,cell* p)
{
    state->engineCommands.push_back({p[1],Read(amx,p[2]),Read(amx,p[3]),Read(amx,p[4]),
        state->menus.at(state->shown).destroyed});return 1;
}
cell AMX_NATIVE_CALL Task(AMX* amx,cell* p){state->tasks[p[3]]=Read(amx,p[2]);return 1;}
cell AMX_NATIVE_CALL RemoveTask(AMX*,cell* p){return static_cast<cell>(state->tasks.erase(p[1]));}
cell AMX_NATIVE_CALL Float(AMX*,cell* p){return std::bit_cast<cell>(static_cast<float>(p[1]));}
cell AMX_NATIVE_CALL FloatRound(AMX*,cell* p){return static_cast<cell>(std::lround(std::bit_cast<float>(p[1])));}
cell AMX_NATIVE_CALL FloatString(AMX* amx,cell* p){return std::bit_cast<cell>(std::strtof(Read(amx,p[1]).c_str(),nullptr));}
cell AMX_NATIVE_CALL FloatCmp(AMX*,cell* p){const float a=std::bit_cast<float>(p[1]),b=std::bit_cast<float>(p[2]);return a<b?-1:a>b?1:0;}
cell AMX_NATIVE_CALL FloatAdd(AMX*,cell* p){return std::bit_cast<cell>(std::bit_cast<float>(p[1])+std::bit_cast<float>(p[2]));}
cell AMX_NATIVE_CALL FloatSub(AMX*,cell* p){return std::bit_cast<cell>(std::bit_cast<float>(p[1])-std::bit_cast<float>(p[2]));}
cell AMX_NATIVE_CALL FloatMul(AMX*,cell* p){return std::bit_cast<cell>(std::bit_cast<float>(p[1])*std::bit_cast<float>(p[2]));}
cell AMX_NATIVE_CALL FloatDiv(AMX*,cell* p){return std::bit_cast<cell>(std::bit_cast<float>(p[1])/std::bit_cast<float>(p[2]));}
cell AMX_NATIVE_CALL Args(AMX* amx,cell* p)
{std::string value;for(std::size_t i=1;i<state->args.size();++i){if(i>1)value+=' ';value+=state->args[i];}return Write(amx,p[1],value,p[2]);}
cell AMX_NATIVE_CALL Quotes(AMX* amx,cell* p)
{auto text=Read(amx,p[1]);if(text.size()>=2&&text.front()=='"'&&text.back()=='"')text=text.substr(1,text.size()-2);return Write(amx,p[1],text,int(text.size()));}
cell AMX_NATIVE_CALL Trim(AMX* amx,cell* p)
{auto text=Read(amx,p[1]);auto first=text.find_first_not_of(" \t\r\n"),last=text.find_last_not_of(" \t\r\n");text=first==text.npos?std::string{}:text.substr(first,last-first+1);return Write(amx,p[1],text,int(text.size()));}
cell AMX_NATIVE_CALL Format(AMX* amx,cell* p)
{
    const auto format=Read(amx,p[3]);std::string output;int argument=4;
    for(std::size_t i=0;i<format.size();++i)
    {
        if(format[i]!='%'){output+=format[i];continue;}
        const auto start=i++;
        if(i==format.size())throw std::runtime_error("bad format");
        if(format[i]=='%'){output+='%';continue;}
        while(i<format.size()&&(format[i]=='.'||(format[i]>='0'&&format[i]<='9')))++i;
        if(i==format.size())throw std::runtime_error("bad format specifier");
        const auto token=format.substr(start,i-start+1);char buffer[256]{};
        if(format[i]=='s')output+=Read(amx,p[argument++]);
        else if(format[i]=='d'){std::snprintf(buffer,sizeof(buffer),token.c_str(),int(*Addr(amx,p[argument++])));output+=buffer;}
        else if(format[i]=='f'){std::snprintf(buffer,sizeof(buffer),token.c_str(),double(std::bit_cast<float>(*Addr(amx,p[argument++]))));output+=buffer;}
        else throw std::runtime_error("unexpected tested format");
    }
    return Write(amx,p[1],output,p[2]);
}
cell AMX_NATIVE_CALL Info(AMX* amx,cell* p)
{
    if(!state->settingsAvailable)return 0;
    *Addr(amx,p[1])=state->revision;*Addr(amx,p[2])=static_cast<cell>(state->fields.size());
    *Addr(amx,p[3])=state->busy;*Addr(amx,p[4])=state->status;Write(amx,p[5],state->error,p[6]);return 1;
}
cell AMX_NATIVE_CALL Entry(AMX* amx,cell* p)
{
    if(p[1]<0||p[1]>=static_cast<int>(state->fields.size()))return 0;const auto& f=state->fields[p[1]];
    Write(amx,p[2],f.key,p[3]);Write(amx,p[4],f.comment,p[5]);*Addr(amx,p[6])=f.type;
    *Addr(amx,p[7])=std::bit_cast<cell>(f.value);*Addr(amx,p[8])=std::bit_cast<cell>(f.minimum);
    *Addr(amx,p[9])=std::bit_cast<cell>(f.maximum);*Addr(amx,p[10])=f.nextMap;*Addr(amx,p[11])=f.available;return 1;
}
cell AMX_NATIVE_CALL Edit(AMX*,cell* p)
{
    ++state->edits;state->expectedUserid=p[2];state->expectedRevision=p[3];state->editIndex=p[4];
    state->editValue=std::bit_cast<float>(p[5]);
    if(!state->connected||!(state->flags&AdminCfg)||p[2]!=state->userid)return -4;
    if(p[3]!=state->revision)return -2;if(state->busy)return -3;
    state->busy=1;state->status=1;
    if(state->immediateSaved){state->Finish(true);return 2;}
    return 1;
}
const AMX_NATIVE_INFO overrides[]{
    {"get_user_userid",Userid},{"client_print",Notice},{"client_cmd",Command},{"engclient_cmd",EngineCommand},{"formatex",Format},
    {"set_task",Task},{"remove_task",RemoveTask},{"float",Float},{"floatround",FloatRound},{"str_to_float",FloatString},
    {"floatcmp",FloatCmp},{"floatadd",FloatAdd},{"floatsub",FloatSub},{"floatmul",FloatMul},{"floatdiv",FloatDiv},
    {"read_args",Args},{"remove_quotes",Quotes},{"trim",Trim},
    {"csol_custom_api_version",One},{"csol_custom_info",Info},{"csol_custom_read",Entry},{"csol_custom_edit",Edit},
    {nullptr,nullptr}};
struct VM
{
    State s;std::vector<unsigned char> memory=bytecode;AMX amx{};
    VM()
    {
        context=&s;state=&s;const auto* header=reinterpret_cast<const AMX_HEADER*>(memory.data());
        memory.resize(std::max(memory.size(),std::size_t(header->stp)));
        if(amx_Init(&amx,memory.data()))throw std::runtime_error("AMX init");
        std::map<std::string,AMX_NATIVE> entries;
        for(auto* p=natives;p->name;++p)entries[p->name]=p->func;
        for(auto* p=overrides;p->name;++p)entries[p->name]=p->func;
        std::vector<AMX_NATIVE_INFO> registration;
        for(const auto& [name,function]:entries)registration.push_back({name.c_str(),function});
        registration.push_back({nullptr,nullptr});amx_Register(&amx,registration.data(),-1);
        int count{};amx_NumNatives(&amx,&count);
        for(int i=0;i<count;++i){char name[128]{};amx_GetNative(&amx,i,name);AMX_NATIVE_INFO row[2]{{name,Reject},{nullptr,nullptr}};amx_Register(&amx,row,-1);}
        if(amx_Register(&amx,nullptr,0))throw std::runtime_error("AMX natives");
        Call("plugin_init");
    }
    ~VM(){Call("plugin_end");amx_Cleanup(&amx);state=nullptr;context=nullptr;}
    cell Call(const char* name,std::initializer_list<cell> arguments={})
    {
        context=&s;state=&s;int index{};if(amx_FindPublic(&amx,name,&index))throw std::runtime_error(std::string("missing public ")+name);
        for(auto it=std::rbegin(arguments);it!=std::rend(arguments);++it)amx_Push(&amx,*it);
        cell result{};const int error=amx_Exec(&amx,&result,index);
        if(error)throw std::runtime_error(std::string(name)+" AMX error "+std::to_string(error));return result;
    }
    int Management(){s.args={"amx_scenario_menu"};Call("CommandMenu",{1,AdminCfg,1});return s.shown;}
    int Settings(){int menu=Management();Call("FinishMenuHandler",{1,menu,8});return s.shown;}
    void Choose(int index){int menu=Settings();Call("CustomSettingsHandler",{1,menu,index});}
    void Prompt(int index){Choose(index);const int menu=s.shown;Call("CustomEntryHandler",{1,menu,0});}
    void Input(std::string value){s.args={"csol_custom_value",std::move(value)};Call("CommandCustomValue",{1});}
    void Poll(){Call("PollCustomSave",{27401});}
    bool NoticeContains(std::string_view text)const{return std::any_of(s.notices.begin(),s.notices.end(),[&](const auto& notice){return notice.find(text)!=notice.npos;});}
};
void RootCases()
{
    for(bool admin:{false,true})
    {
        VM v;if(!admin)v.s.flags=0;
        Check(v.Call("CommandChooseTeam",{1})==1,"joined Scenario M is handled by genuine root homepage");
        const int root=v.s.shown;const auto& menu=v.s.menus.at(root);
        Check(menu.handler=="PlayerMenuHandler"&&menu.items.size()==(admin?2u:1u),"ordinary root hides administrator branch");
        Check(menu.items[0]==std::pair<std::string,std::string>{"购买系统","0"},"root item1 is real purchase system");
        if(admin)Check(menu.items[1]==std::pair<std::string,std::string>{"管理员菜单","2"},"root item2 preserves explicit admin data identity");
        Check(std::none_of(menu.items.begin(),menu.items.end(),[](const auto& item)
            {return item.first.find("盲盒")!=std::string::npos||item.first.find("熟练度")!=std::string::npos||item.first.find("就绪")!=std::string::npos;}),
            "root contains no unavailable feature or implementation-status labels");
        v.Call("PlayerMenuHandler",{1,root,0});
        Check(v.s.engineCommands.size()==1,"purchase selection dispatches exactly one engine command");
        const auto& dispatch=v.s.engineCommands.at(0);
        Check(dispatch.player==1&&dispatch.command=="buy"&&dispatch.arg1.empty()&&dispatch.arg2.empty()&&dispatch.menuDestroyed,
            "real engclient_cmd signature receives fixed buy after closing AMXX root");
        Check(v.s.commands.empty()&&v.s.edits==0&&v.s.requests==0&&v.s.replays==0&&v.s.votes==0,
            "purchase root cannot mutate saved settings or submit administrative actions");
        v.Call("PlayerMenuHandler",{1,root,0});Check(v.s.engineCommands.size()==1,"late duplicate root selection cannot buy again");
    }
    {VM v;v.Call("CommandChooseTeam",{1});const int root=v.s.shown;v.Call("PlayerMenuHandler",{1,root,1});
        const auto& menu=v.s.menus.at(v.s.shown);
        Check(menu.handler=="FinishMenuHandler"&&menu.items.size()==9,"admin second item opens all8 original actions plus settings");
        Check(menu.items[5].first.find("立即重玩")!=std::string::npos&&menu.items[7].first.find("立即发起选图投票")!=std::string::npos&&
            menu.items[8].first.find("持久玩法设置")!=std::string::npos,"admin legacy replay/vote and persistent settings stay in place");}
    for(int variant=0;variant<5;++variant)
    {VM v;v.Call("CommandChooseTeam",{1});const int root=v.s.shown;
        if(variant==0)v.s.userid++;else if(variant==1)v.s.mode=1;else if(variant==2)v.s.connected=false;else if(variant==3)v.s.joining=1;
        else v.s.onDestroy=[&]{v.s.userid++;};
        v.Call("PlayerMenuHandler",{1,root,0});v.s.onDestroy={};
        Check(v.s.engineCommands.empty(),"stale userid/mode/connection/join state and synchronous menu retirement block purchase");}
    {VM v;v.Call("CommandChooseTeam",{1});const int root=v.s.shown;v.s.flags=0;v.Call("PlayerMenuHandler",{1,root,1});
        Check(v.s.shown==root&&v.s.menus.at(root).destroyed&&v.s.engineCommands.empty(),"admin item rechecks revoked ADMIN_CFG at submission");}
    {VM v;v.s.flags=0;v.Call("CommandChooseTeam",{1});const int root=v.s.shown;v.Call("PlayerMenuHandler",{1,root,1});
        Check(v.s.shown==root&&v.s.menus.at(root).destroyed&&v.s.engineCommands.empty(),"ordinary player forged absent admin index cannot enter management");}
    for(int variant=0;variant<3;++variant)
    {VM v;if(variant==0)v.s.joining=1;else if(variant==1)v.s.mode=1;else v.s.connected=false;
        Check(v.Call("CommandChooseTeam",{1})==0&&v.s.shown==-1,"initial selection other modes and disconnect preserve native M");}
    {VM v;v.Call("CommandChooseTeam",{1});const int old=v.s.shown;v.Call("CommandChooseTeam",{1});const int fresh=v.s.shown;
        const auto destroys=v.s.destroys;v.Call("PlayerMenuHandler",{1,old,0});
        Check(v.s.engineCommands.empty()&&v.s.shown==fresh&&v.s.destroys==destroys,"old root handler cannot close newer root or dispatch a command");}
}
void Cases()
{
    {VM v;v.s.flags=0;Check(v.Call("CommandChooseTeam",{1})==1,"joined ordinary Scenario player gets M homepage");
        const auto& menu=v.s.menus.at(v.s.shown);Check(menu.items.size()==1&&menu.handler=="PlayerMenuHandler","ordinary homepage has player actions and no server management");
        v.Call("PlayerMenuHandler",{1,v.s.shown,0});Check(v.s.edits==0&&v.s.engineCommands.size()==1&&v.s.engineCommands[0].command=="buy","root purchase dispatches the real fixed native buy command without settings edits");}
    for(int mode=0;mode!=3;++mode){VM v;if(mode==0)v.s.joining=1;else if(mode==1)v.s.mode=1;else v.s.connected=false;
        Check(v.Call("CommandChooseTeam",{1})==0&&v.s.shown==-1,"initial selection other mode and disconnect retain native M");}
    {VM v;v.Call("CommandChooseTeam",{1});const int menu=v.s.shown;
        Check(v.s.menus.at(menu).items.size()==2,"administrator homepage adds management branch");
        v.Call("PlayerMenuHandler",{1,menu,1});const auto& management=v.s.menus.at(v.s.shown);
        Check(management.items.size()==9&&management.items[5].first.find("立即重玩")!=std::string::npos&&management.items[7].first.find("立即发起选图投票")!=std::string::npos,
            "management preserves all eight legacy actions and adds persistent settings");}
    {VM v;const int menu=v.Management();v.Call("FinishMenuHandler",{1,menu,5});Check(v.s.replays==1,"legacy same-map replay still reaches actual plugin submission");}
    {VM v;const int menu=v.Management();v.Call("FinishMenuHandler",{1,menu,7});Check(v.s.votes==1,"legacy immediate vote still reaches actual plugin submission");}
    {VM v;const int menu=v.Settings();Check(v.s.menus.at(menu).items.size()==4,"settings enumerate actual native catalog");
        v.Call("CustomSettingsHandler",{1,menu,0});Check(v.s.edits==1&&v.s.busy&&v.s.fields[0].value==1&&v.s.editValue==0,
            "boolean toggle queues typed false without changing saved value prematurely");
        Check(v.s.expectedUserid==71&&v.s.expectedRevision==5&&v.NoticeContains("已排队")&&!v.NoticeContains("已永久保存"),"queued feedback carries exact identity and never claims saved");
        v.Poll();Check(!v.NoticeContains("已永久保存"),"busy poll remains queued");v.s.Finish(true);v.Poll();
        Check(v.NoticeContains("已永久保存")&&v.s.revision==6&&v.s.fields[0].value==0,"matching committed revision/value finally reports permanent saved");
        v.Choose(0);v.s.Finish(true);v.Poll();Check(v.s.fields[0].value==1&&v.s.edits==2,"boolean can be re-enabled after saved disable");}
    {VM v;v.Choose(0);v.s.Finish(false);v.Poll();Check(!v.NoticeContains("已永久保存")&&v.s.revision==5&&v.s.fields[0].value==1,
        "disk failure retains old state and cannot claim permanent success");}
    {VM v;v.s.immediateSaved=true;v.Choose(0);Check(!v.NoticeContains("已永久保存"),"direct saved return still waits for matching revision verification");
        v.Poll();Check(v.NoticeContains("已永久保存"),"matching direct saved result is supported without false unsubmitted feedback");}
    for(int variant=0;variant!=4;++variant){VM v;int menu=v.Settings();if(variant==0)v.s.flags=0;else if(variant==1)v.s.userid=72;else if(variant==2)v.s.mode=1;else v.s.connected=false;
        v.Call("CustomSettingsHandler",{1,menu,0});Check(v.s.edits==0,"bool selection rechecks admin userid mode and connection");}
    {VM v;int menu=v.Settings();++v.s.revision;v.Call("CustomSettingsHandler",{1,menu,0});Check(v.s.edits==0,"stale settings revision cannot mutate and refreshes");}
    {VM v;int old=v.Settings(),fresh=v.Settings();int destroyed=v.s.destroys;v.Call("CustomSettingsHandler",{1,old,0});
        Check(v.s.edits==0&&v.s.shown==fresh&&v.s.destroys==destroyed,"late old menu cannot destroy or edit current settings menu");}
    {VM v;v.Prompt(1);Check(v.s.commands==std::vector<std::string>{"messagemode csol_custom_value"},"numeric prompt executes only the fixed safe message-mode command");
        v.Input("240");Check(v.s.edits==1&&v.s.editIndex==1&&v.s.editValue==240&&v.s.expectedRevision==5,"strict integer enters typed native with captured revision");}
    {VM v;v.Prompt(2);v.Input("1.125");Check(v.s.edits==1&&v.s.editValue==1.125f,"ordinary decimal enters real-valued typed native");}
    {VM v;v.s.fields[1].maximum=1000000000;v.Prompt(1);v.Input("16777217");Check(v.s.edits==0,"integer not exactly representable by the Float native ABI is rejected without rounding");}
    for(const auto& text:{"1;quit","NaN","Infinity","1e2","1.5","-1","1 2","999999999999999999999","0","86401"})
    {VM v;v.Prompt(1);v.Input(text);Check(v.s.edits==0,"integer prompt rejects malformed fractional injection overflow and range errors");}
    for(int variant=0;variant!=5;++variant){VM v;v.Prompt(1);if(variant==0)v.s.flags=0;else if(variant==1)++v.s.userid;else if(variant==2)++v.s.revision;else if(variant==3)v.s.fields[1].key="different_identity";else v.s.busy=1;
        v.Input("240");Check(v.s.edits==0,"text submission rechecks admin connection revision key and writer busy");}
    {VM v;v.Prompt(1);v.Call("client_disconnected",{1});++v.s.userid;v.Input("240");Check(v.s.edits==0,"disconnected old prompt cannot submit into reused slot");}
    {VM v;v.Input("240");Check(v.s.edits==0,"unsolicited csol_custom_value command lacks a captured prompt");}
    {VM v;v.Choose(0);v.s.Finish(true);v.s.fields[0].value=1;v.Poll();Check(!v.NoticeContains("已永久保存"),"another value at a completed revision cannot be misreported as this save");}
    {VM v;v.Choose(0);++v.s.userid;v.s.Finish(true);v.Poll();Check(!v.NoticeContains("已永久保存"),"queued status is never delivered to a new occupant");}
}
}
int main(int argc,char** argv)
{
    try
    {
        if(argc<2||argc>3)throw std::runtime_error("provide extracted current administrator .amx [root|all]");
        std::ifstream file(argv[1],std::ios::binary);bytecode.assign(std::istreambuf_iterator<char>(file),{});
        if(bytecode.empty())throw std::runtime_error("empty bytecode");
        if(argc==3&&std::string_view(argv[2])=="root")custom_menu_test::RootCases();
        else custom_menu_test::Cases();
    }
    catch(const std::exception& error){++failures;std::cerr<<"ERROR "<<error.what()<<'\n';}
    std::cout<<"checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
