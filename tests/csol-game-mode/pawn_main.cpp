// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
#include "amx.h"

namespace
{
int checks{}, failures{};
std::string currentCase;
void Check(bool value,const char* label)
{
    ++checks;
    if(!value){++failures;std::cerr<<"FAIL ["<<currentCase<<"] "<<label<<'\n';}
}
cell* get_amxaddr(AMX* amx,cell address)
{
    cell* result{};if(amx_GetAddr(amx,address,&result)!=AMX_ERR_NONE)throw std::runtime_error("invalid AMX address");
    return result;
}
char* get_amxstring(AMX* amx,cell address,int slot,int& length)
{
    static std::array<std::string,4> buffers;
    auto& buffer=buffers.at(slot);buffer.clear();
    for(auto* text=get_amxaddr(amx,address);*text;++text)buffer.push_back(static_cast<char>(*text));
    length=static_cast<int>(buffer.size());return buffer.data();
}
std::string Read(AMX* amx,cell address)
{int ignored{};return get_amxstring(amx,address,3,ignored);}
cell Write(AMX* amx,cell address,const std::string& value,int max)
{
    auto* destination=get_amxaddr(amx,address);
    const auto count=std::min(value.size(),static_cast<std::size_t>(std::max(0,max)));
    for(std::size_t i=0;i<count;++i)destination[i]=static_cast<unsigned char>(value[i]);
    destination[count]=0;return static_cast<cell>(count);
}
bool utf8isspace(const char* value)
{
    // Test rule tokens are ASCII; Chinese comments are retained byte-for-byte
    // but are not used for whitespace or case-fold edge claims.
    return std::isspace(static_cast<unsigned char>(*value))!=0;
}
#include "pawn_string_excerpt.inc"

struct Context
{
    int mode{2},apiVersion{1},confirms{},logs{},closed{};
    bool fileExists{true},confirmSuccess{true},failed{},eof{},fileOpen{};
    std::string policy,failPause,failure;
    std::size_t offset{};
    std::vector<std::string> plugins,stopped,order;
};
Context* context{};
cell AMX_NATIVE_CALL GetPlugin(AMX* amx,cell* p)
{
    const int index=static_cast<int>(p[1]);
    if(index<0 || index>=static_cast<int>(context->plugins.size()))return -1;
    Write(amx,p[2],context->plugins[index],p[3]);
    return index;
}
cell AMX_NATIVE_CALL Plugins(AMX*,cell*){return static_cast<cell>(context->plugins.size());}
cell AMX_NATIVE_CALL Pause(AMX* amx,cell* p)
{
    Check(Read(amx,p[1])=="ac","exact pause flags");
    const auto name=Read(amx,p[2]);context->order.push_back("pause:"+name);
    if(name==context->failPause)return 0;
    // Match actual CPlugin::findPlugin prefix semantics.
    const auto found=std::find_if(context->plugins.begin(),context->plugins.end(),[&](const auto& item){return item.starts_with(name);});
    if(found==context->plugins.end())return 0;
    context->stopped.push_back(*found);return 1;
}
cell AMX_NATIVE_CALL Current(AMX*,cell*){return context->mode;}
cell AMX_NATIVE_CALL Version(AMX*,cell*){return context->apiVersion;}
cell AMX_NATIVE_CALL Confirm(AMX*,cell* p)
{
    ++context->confirms;context->order.emplace_back("confirm");
    Check(p[1]==context->mode,"acknowledges locked mode");
    return context->confirmSuccess;
}
cell AMX_NATIVE_CALL Fail(AMX* amx,cell* p)
{
    context->failed=true;context->failure=Read(amx,p[1]);
    amx_RaiseError(amx,AMX_ERR_EXIT);return 0;
}
cell AMX_NATIVE_CALL LocalInfo(AMX* amx,cell* p)
{
    Check(Read(amx,p[1])=="amxx_configsdir","configuration root requested");
    return Write(amx,p[2],"configs",p[3]);
}
cell AMX_NATIVE_CALL Format(AMX* amx,cell* p)
{
    Check(Read(amx,p[3])=="%s/goldcraft/game_modes.ini","exact policy relative path");
    return Write(amx,p[1],Read(amx,p[4])+"/goldcraft/game_modes.ini",p[2]);
}
cell AMX_NATIVE_CALL Open(AMX* amx,cell* p)
{
    Check(Read(amx,p[1])=="configs/goldcraft/game_modes.ini","policy open path");
    Check(Read(amx,p[2])=="rt","policy text read only");
    context->fileOpen=context->fileExists;return context->fileExists?1:0;
}
cell AMX_NATIVE_CALL Close(AMX*,cell*){context->fileOpen=false;++context->closed;return 1;}
cell AMX_NATIVE_CALL Eof(AMX*,cell*){return context->eof;}
cell AMX_NATIVE_CALL Gets(AMX* amx,cell* p)
{
    auto& c=*context;
    if(c.offset==c.policy.size()){c.eof=true;return Write(amx,p[2],"",p[3]);}
    const auto newline=c.policy.find('\n',c.offset);
    const auto end=newline==std::string::npos?c.policy.size():newline+1;
    const auto line=c.policy.substr(c.offset,end-c.offset);c.offset=end;
    return Write(amx,p[2],line,p[3]);
}
cell AMX_NATIVE_CALL Length(AMX* amx,cell* p){return static_cast<cell>(Read(amx,p[1]).size());}
cell AMX_NATIVE_CALL Trim(AMX* amx,cell* p)
{
    const auto text=Read(amx,p[1]);auto first=text.find_first_not_of(" \t\r\n\f\v");
    const auto value=first==std::string::npos?std::string{}:text.substr(first,text.find_last_not_of(" \t\r\n\f\v")-first+1);
    Write(amx,p[1],value,static_cast<int>(text.size()));return static_cast<cell>(text.size()-value.size());
}
cell AMX_NATIVE_CALL FoldEqual(AMX* amx,cell* p)
{
    auto a=Read(amx,p[1]),b=Read(amx,p[2]);
    for(char& ch:a)ch=static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    for(char& ch:b)ch=static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return p[3]>0?std::strncmp(a.c_str(),b.c_str(),p[3])==0:a==b;
}
cell AMX_NATIVE_CALL Log(AMX*,cell*){++context->logs;return 1;}
cell AMX_NATIVE_CALL Registration(AMX*,cell*){return 0;}
const AMX_NATIVE_INFO natives[]{
    {"get_plugin",GetPlugin},{"get_pluginsnum",Plugins},{"pause",Pause},
    {"csol_game_mode_current",Current},{"csol_game_mode_api_version",Version},{"csol_game_mode_confirm",Confirm},
    {"set_fail_state",Fail},{"get_localinfo",LocalInfo},{"formatex",Format},
    {"fopen",Open},{"fclose",Close},{"feof",Eof},{"fgets",Gets},
    {"strlen",Length},{"trim",Trim},{"equali",FoldEqual},{"equal",equal},{"copy",copy},{"parse",parse},
    {"log_amx",Log},{"register_plugin",Registration},{nullptr,nullptr}};

const std::vector<std::string> KnownPlugins{
    "goldcraft_game_mode.amxx","zp50_core.amxx","zp50_objective_remover.amxx",
    "goldcraft_zp50.amxx","goldcraft_zp_rules.amxx","goldcraft_zombie_rules.amxx",
    "zombieplague.amxx","nademodes.amxx","cs_teams_api.amxx","amx_settings_api.amxx","shared.amxx"};
std::vector<unsigned char> bytecode;
std::string defaultPolicy;
void Run(const std::string& name,Context& scenario,const std::vector<std::string>& stopped,bool succeeds,int confirmations)
{
    currentCase=name;context=&scenario;
    auto memory=bytecode;const auto* header=reinterpret_cast<const AMX_HEADER*>(memory.data());
    const int total=header->stp;
    if(total<=0 || total>4*1024*1024)throw std::runtime_error("invalid image memory size");
    memory.resize(std::max(memory.size(),static_cast<std::size_t>(total)));
    AMX amx{};const int initialized=amx_Init(&amx,memory.data());
    if(initialized!=AMX_ERR_NONE)throw std::runtime_error("AMX init "+std::to_string(initialized));
    const int registered=amx_Register(&amx,natives,-1);
    if(registered!=AMX_ERR_NONE)
    {
        int count{};amx_NumNatives(&amx,&count);
        for(int i=0;i<count;++i){char native[128]{};amx_GetNative(&amx,i,native);std::cerr<<native<<'\n';}
        throw std::runtime_error("AMX register "+std::to_string(registered));
    }
    int entry{};Check(amx_FindPublic(&amx,"plugin_precache",&entry)==AMX_ERR_NONE,"compiled plugin_precache found");
    cell returned{};const int error=amx_Exec(&amx,&returned,entry);
    Check(error==(succeeds?AMX_ERR_NONE:AMX_ERR_EXIT),"expected AMX completion/error");
    Check(scenario.failed!=succeeds,"expected failure-state call");
    Check(scenario.confirms==confirmations,"acknowledgement call count");
    Check(scenario.logs==(succeeds?1:0),"no success log on failure");
    Check(scenario.stopped==stopped,"exact plugin stop sequence");
    Check(!scenario.fileOpen,"policy handle closed");
    if(confirmations)Check(!scenario.order.empty() && scenario.order.back()=="confirm","confirm occurs after all pause attempts");
    if(!succeeds)Check(!scenario.failure.empty(),"failure has diagnostic text");
    amx_Cleanup(&amx);
}
Context New(int mode,const std::string& policy)
{Context result;result.mode=mode;result.policy=policy;result.plugins=KnownPlugins;return result;}
std::vector<std::string> Required(int mode)
{
    std::vector<std::string> result;
    if(mode!=1)result.assign(KnownPlugins.begin()+1,KnownPlugins.begin()+7);
    if(mode==2)result.push_back("nademodes.amxx");
    return result;
}
void Cases()
{
    for(int mode=0;mode<3;++mode)
    {
        auto c=New(mode,defaultPolicy);Run("default mode "+std::to_string(mode),c,Required(mode),true,1);
        c=New(mode,"shared.amxx 7\n");Run("omitted required policies "+std::to_string(mode),c,Required(mode),true,1);
        c=New(mode,"zp50_* 7\ngoldcraft_zp* 7\ngoldcraft_zombie_* 7\nzombieplague* 7\nnademodes.amxx 7\n");
        Run("permissive policies cannot expand baseline "+std::to_string(mode),c,Required(mode),true,1);
        c=New(mode,"shared.amxx 0\n");auto stopped=Required(mode);stopped.push_back("shared.amxx");
        Run("policy can narrow shared plugin "+std::to_string(mode),c,stopped,true,1);
    }
    for(int mode:std::array<int,4>{-1,3,100,2147483647})
    {auto c=New(mode,defaultPolicy);Run("invalid locked mode "+std::to_string(mode),c,{},false,0);}
    {auto c=New(2,defaultPolicy);c.apiVersion=0;Run("missing API",c,{},false,0);}
    {auto c=New(2,defaultPolicy);c.plugins.insert(c.plugins.begin(),"admin.amxx");Run("gate not first",c,{},false,0);}
    {auto c=New(2,defaultPolicy);c.fileExists=false;Run("missing policy",c,{},false,0);}
    {auto c=New(2,defaultPolicy);c.failPause="goldcraft_zp50.amxx";Run("pause fails after partial work",c,{"zp50_core.amxx","zp50_objective_remover.amxx"},false,0);}
    {auto c=New(2,defaultPolicy);c.confirmSuccess=false;Run("native confirmation fails",c,Required(2),false,1);}
    const std::vector<std::string> invalid{
        ""," ; comment only\n# other\n","zp50_*\n","zp50_* 8\n","zp50_* -1\n","zp50_* 2 extra\n",
        "zp50_* 2 3 4\n","zp50_* 22\n","* 2\n","zp*50 2\n","zp/50 2\n","zp\\50 2\n",
        "\"\" 2\n",std::string(63,'a')+" 2\n",std::string(256,'a')+" 2\n"};
    for(std::size_t i=0;i<invalid.size();++i){auto c=New(2,invalid[i]);Run("invalid policy "+std::to_string(i),c,{},false,0);}
    {std::string policy;for(int i=0;i<129;++i)policy+="shared.amxx 7\n";auto c=New(2,policy);Run("129 rules rejected",c,{},false,0);}
    {std::string policy;for(int i=0;i<128;++i)policy+="shared.amxx 7\n";auto c=New(2,policy);Run("128 rules allowed",c,Required(2),true,1);}
    {auto c=New(1,"shared.amxx 0\nshared* 7\n");Run("first match denies",c,{"shared.amxx"},true,1);}
    {auto c=New(1,"shared.amxx 7\nshared* 0\n");Run("first match allows",c,{},true,1);}
    {auto c=New(0,"shared.amxx 7\n");for(auto& name:c.plugins)if(name!="goldcraft_game_mode.amxx")for(char& ch:name)ch=static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));std::vector<std::string> stopped(c.plugins.begin()+1,c.plugins.begin()+7);Run("baseline ASCII case folding",c,stopped,true,1);}
    {auto c=New(2,"shared.amxx 7\n");c.plugins.push_back("zp50x.amxx");c.plugins.push_back("goldcraft_zombiex.amxx");Run("near prefix shared plugins retained",c,Required(2),true,1);}
}
}
int main(int argc,char** argv)
{
    static_assert(sizeof(void*)==4 && sizeof(cell)==4);
    if(argc!=3)return 2;
    try
    {
        std::ifstream image(argv[1],std::ios::binary);bytecode.assign(std::istreambuf_iterator<char>(image),{});
        if(bytecode.size()<sizeof(AMX_HEADER))throw std::runtime_error("missing image");
        std::ifstream policy(argv[2],std::ios::binary);defaultPolicy.assign(std::istreambuf_iterator<char>(policy),{});
        if(defaultPolicy.empty())throw std::runtime_error("missing default policy");
        Cases();
    }
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
    std::cout<<"game_mode pawn checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
