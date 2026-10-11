// Run the matched copied engine's real Key_Event without DllMain or game input.
// Only the command buffer/ClientDLL callbacks are adapted. The production
// host_ui release observer is installed through the real public-entry address.
#include <metahook.h>
#include <bcrypt.h>
#include "../../native/client/host_ui.hpp"
#include "goldcraft/host_key_events.hpp"
#include <array>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
unsigned char* engine;
std::array<bool,256> held{};
std::vector<int> edges;
std::vector<std::string> commands;
unsigned client_releases=0;
void* trampoline=nullptr;
std::array<unsigned char,9> original_bytes{};
void jump(void* address,const void* destination){
    DWORD old,ignored;assert(VirtualProtect(address,5,PAGE_EXECUTE_READWRITE,&old));
    auto p=static_cast<unsigned char*>(address);p[0]=0xe9;
    auto relative=std::uint32_t(reinterpret_cast<std::uintptr_t>(destination)-reinterpret_cast<std::uintptr_t>(p+5));
    std::memcpy(p+1,&relative,4);assert(VirtualProtect(address,5,old,&ignored));
    assert(FlushInstructionCache(GetCurrentProcess(),address,5));
}
hook_t* hook(void* address,void* replacement,void** original){
    assert(address==engine+0x1d7500&&!trampoline);
    constexpr unsigned char prologue[]{0x55,0x8b,0xec,0x81,0xec,0x08,0x04,0,0};
    assert(!std::memcmp(address,prologue,sizeof(prologue)));
    std::memcpy(original_bytes.data(),address,original_bytes.size());
    trampoline=VirtualAlloc(nullptr,32,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);assert(trampoline);
    std::memcpy(trampoline,address,original_bytes.size());
    jump(static_cast<unsigned char*>(trampoline)+9,engine+0x1d7509);
    *original=trampoline;jump(address,replacement);return reinterpret_cast<hook_t*>(trampoline);
}
BOOL unhook(hook_t* value){
    assert(value==reinterpret_cast<hook_t*>(trampoline));DWORD old,ignored;
    assert(VirtualProtect(engine+0x1d7500,9,PAGE_EXECUTE_READWRITE,&old));
    std::memcpy(engine+0x1d7500,original_bytes.data(),9);
    assert(VirtualProtect(engine+0x1d7500,9,old,&ignored));
    assert(FlushInstructionCache(GetCurrentProcess(),engine+0x1d7500,9));
    assert(VirtualFree(trampoline,0,MEM_RELEASE));trampoline=nullptr;return TRUE;
}
void forward(int down,int key){
    int action=goldcraft::host_keys::action(held,key,down!=0,key>=241&&key<=245);
    if(action>=0)edges.push_back(action);
}
void release(int key){if(key>=0&&key<256&&held[key])forward(0,key);}
bool base_handler(int down,int key,const char* binding){
    auto route=goldcraft::host_keys::gameplay_route(binding,true,true);
    if(route!=goldcraft::host_keys::Route::native)forward(down,key);
    return route==goldcraft::host_keys::Route::consume;
}
int client(int down,int key,const char* binding){
    if(!down)client_releases++;
    return base_handler(down,key,binding)?0:1;
}
void queued(const char* command){commands.emplace_back(command);}
int format(char* target,std::size_t size,const char* pattern,...){
    va_list args;va_start(args,pattern);int result=std::vsnprintf(target,size,pattern,args);va_end(args);return result;
}
CreateInterfaceFn factory(){return nullptr;}
void bind(int key,const char* text){
    auto keys=reinterpret_cast<int*>(engine+0x12461e0);
    keys[key]=keys[key+256]=keys[key+512]=0;keys[key+768]=key;
    reinterpret_cast<const char**>(keys)[key+1536]=text;
}
void clear(){held.fill(false);edges.clear();commands.clear();client_releases=0;}
}

int main(int argc,char** argv){
    assert(argc==2);std::ifstream file(argv[1],std::ios::binary);
    std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(file),{}};
    std::array<unsigned char,32> hash{};
    assert(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,bytes.data(),ULONG(bytes.size()),hash.data(),ULONG(hash.size()))==0);
    constexpr unsigned char expected[]{0x9b,0xa9,0xa2,0xdb,0x5e,0x07,0x59,0x8f,0xd5,0x9a,0xfa,0x35,0x50,0x7a,0x98,0xc8,
        0x61,0x62,0xe4,0xe1,0x5b,0x38,0x35,0x17,0x7b,0x78,0xc1,0x18,0x42,0xcd,0x22,0x95};
    assert(hash==std::to_array(expected));
    auto module=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);assert(module);
    engine=reinterpret_cast<unsigned char*>(module);
    jump(engine+0x1971b0,reinterpret_cast<void*>(client));
    jump(engine+0x192040,reinterpret_cast<void*>(format));
    jump(engine+0x1b4ca0,reinterpret_cast<void*>(queued));
    *reinterpret_cast<int*>(engine+0x4b97f4)=0;
    auto key_event=reinterpret_cast<void(*)(int,int)>(engine+0x1d7500);
    bind(241,"+attack");key_event(241,1);key_event(241,0);
    assert(held[241]&&edges==std::vector<int>{1}&&client_releases==0);
    const auto original_commands=commands;
    assert(original_commands==std::vector<std::string>({"+attack 241\n","-attack 241\n"}));
    clear();metahook_api_t api{};api.InlineHook=hook;api.UnHook=unhook;api.GetEngineFactory=factory;
    goldcraft::host_ui::install(&api,base_handler,key_event,release);
    assert(goldcraft::host_ui::state().release_hooked);
    for(int cycle=0;cycle<32;cycle++){
        clear();bind(241,"+attack");key_event(241,1);key_event(241,1);key_event(241,0);
        assert(!held[241]&&edges==std::vector<int>({1,0})&&client_releases==0);
        assert(commands==original_commands);
    }
    for(auto command:{"+attack2","+reload","+speed","+strafe","+duck"}){
        clear();bind(114,command);key_event(114,1);key_event(114,0);
        assert(!held[114]&&edges==std::vector<int>({1,0})&&commands.size()==2&&client_releases==0);
    }
    clear();bind(101,"+use");key_event(101,1);key_event(101,0);
    assert(edges.empty()&&commands==std::vector<std::string>({"+use 101\n","-use 101\n"}));
    clear();bind(243,"");key_event(243,1);key_event(243,0);
    assert(edges==std::vector<int>({1,0})&&!held[243]&&client_releases==1&&commands.empty());
    clear();bind(114,"impulse 100");key_event(114,1);key_event(114,1);key_event(114,0);
    assert(edges==std::vector<int>({1,2,0})&&!held[114]&&commands.empty());
    clear();bind(241,"+attack");key_event(241,1);held.fill(false);key_event(241,0);
    assert(edges==std::vector<int>{1}&&commands==original_commands);
    goldcraft::host_ui::shutdown();assert(!goldcraft::host_ui::state().release_hooked&&!trampoline);
    clear();bind(241,"+attack");key_event(241,1);key_event(241,0);assert(held[241]&&client_releases==0);
    FreeLibrary(module);
    std::cout<<"PASS actual GoldSrc Key_Event: old stuck hold reproduced; production release hook fixes 32 click cycles and 5 native bindings; native command bytes preserved; middle/repeat/reset/shutdown pass\n";
}
