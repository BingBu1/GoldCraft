// Process fixture: no Steam, authentication, microphone or game process.
#include "steam_async_wire.hpp"
#include <Windows.h>
#include <cstdlib>
#include <cwchar>
#include <initializer_list>
using namespace goldcraft::steam_async;
bool transfer(HANDLE pipe,void* data,unsigned size,bool write){
    auto* p=static_cast<unsigned char*>(data);
    while(size){DWORD n{};if(!(write?WriteFile(pipe,p,size,&n,nullptr):ReadFile(pipe,p,size,&n,nullptr))||!n)return false;p+=n;size-=n;}
    return true;
}
int wmain(int argc,wchar_t** argv){
    if(argc!=3)return 2;
    if(GetEnvironmentVariableW(L"GOLDCRAFT_STEAM_TEST_CHECK_ENV",nullptr,0)){
        // Unlike the production helper, this fixture does not scrub anything:
        // prove CreateProcess received a clean environment before entry.
        for(const auto* key:{L"SteamAppId",L"SteamGameId",L"SteamOverlayGameId",L"SteamEnv",L"SteamClientLaunch"})
            if(GetEnvironmentVariableW(key,nullptr,0))return 9;
    }
    auto pipe=reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(wcstoull(argv[1],nullptr,10)));
    if(!wcscmp(argv[2],L"startup-block"))Sleep(5000);
    wire::Reply reply;if(!transfer(pipe,&reply,sizeof(reply),true))return 3;
    bool recording=false;unsigned captured=0,tail_polls=0;
    for(;;){
        wire::Request command;if(!transfer(pipe,&command,sizeof(command),false))break;
        reply={};
        if(command.op==wire::Op::quit)return 0;
        if(command.op==wire::Op::avatar)Sleep(5000);
        if(command.op==wire::Op::start){recording=true;captured=tail_polls=0;}
        if(command.op==wire::Op::stop)recording=false;
        if(command.op==wire::Op::capture){
            if(recording){reply.result=captured++?3:0;reply.compressed=reply.result?0:3;}
            else {
                ++tail_polls;reply.result=tail_polls<=2?3:(tail_polls==3?0:2);
                reply.compressed=reply.result?0:3;
            }
        }
        if(!transfer(pipe,&reply,sizeof(reply),true))break;
        unsigned char sample[]{static_cast<unsigned char>(recording?1:99),2,3};
        if(reply.compressed&&!transfer(pipe,sample,3,true))break;
    }
    return 0;
}
