// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../runtime.hpp"
#include "scenario_music_program.hpp"
#include <atomic>
#include <cstring>
#include <intrin.h>
#include <mutex>

namespace csol::client
{
namespace
{
using Register=int(__cdecl*)(const char*);
using Action=void(__thiscall*)(void*,int,int);
using Enqueue=bool(__thiscall*)(void*,void*,int,int);
using Open=void*(__stdcall*)(void*,const char*,int);
using Start=void(__stdcall*)(void*);
using Status=int(__stdcall*)(void*);
using Close=void(__stdcall*)(void*);
Register registerTrack{};
Action originalPlay{};
Enqueue originalEnqueue{};
Open originalOpen{};
Start originalStart{};
Status originalStatus{};
Close originalClose{};
void* playEntry{};
void* workerReturn{};
void** queueOwner{};
std::array<hook_t*,6> hooks{};
DWORD mainThread{};
bool ready{};
std::uint32_t nextToken{};
std::atomic<std::uint32_t> currentToken{},completed{};
thread_local std::uint32_t submitToken{},playToken{};
struct Queued {int track{},loop{};std::uint32_t token{};};
std::array<Queued,8> queued{};
std::size_t queuedCount{};
std::mutex metadata;
void* observedStream{};
std::uint32_t observedToken{};
bool observedStarted{};
struct Pending {std::uint32_t token{};int intro{},loop{};bool introQueued{},tailReady{},tailQueued{};} pending;
bool Main(){return mainThread&&mainThread==GetCurrentThreadId();}

bool __fastcall EnqueueHook(void* self,void*,void* callback,int a,int b)
{
    if(callback!=playEntry)return originalEnqueue(self,callback,a,b);
    std::lock_guard lock(metadata);
    // Original pending/copy buffers contain at most one Play each because
    // they deduplicate by callback. Eight entries leave a bounded margin.
    if(queuedCount==queued.size())return false;
    const bool accepted=originalEnqueue(self,callback,a,b);
    if(accepted)queued[queuedCount++]={a,b,submitToken};
    return accepted;
}
void ExecutePlay(void* self,int track,int loop,bool fromQueue)
{
    std::uint32_t token{};
    if(fromQueue)
    {
        std::lock_guard lock(metadata);
        if(queuedCount&&queued[0].track==track&&queued[0].loop==loop)
        {
            token=queued[0].token;
            for(std::size_t i=1;i<queuedCount;++i)queued[i-1]=queued[i];
            --queuedCount;
        }
        else
        {
            queuedCount=0;
            // Lost correlation cannot give an older stream completion rights.
            currentToken.store(0,std::memory_order_release);
        }
    }
    if(token&&currentToken.load(std::memory_order_acquire)!=token)return;
    const auto previous=playToken;playToken=loop?0:token;
    originalPlay(self,track,loop);
    playToken=previous;
}
void __fastcall PlayHook(void* self,void*,int track,int loop)
{ExecutePlay(self,track,loop,_ReturnAddress()==workerReturn);}
void* __stdcall OpenHook(void* driver,const char* path,int memory)
{
    auto* stream=originalOpen(driver,path,memory);
    std::lock_guard lock(metadata);
    observedStream=stream;observedStarted=false;
    observedToken=stream?playToken:0;
    return stream;
}
void __stdcall StartHook(void* stream)
{
    originalStart(stream);
    std::lock_guard lock(metadata);
    if(stream==observedStream&&observedToken&&observedToken==playToken)observedStarted=true;
}
int __stdcall StatusHook(void* stream)
{
    const auto status=originalStatus(stream);
    if(status!=2)return status; // Matching Miles: 4 playing,8 paused,-1 invalid.
    std::lock_guard lock(metadata);
    if(stream==observedStream&&observedStarted&&observedToken&&
        currentToken.load(std::memory_order_acquire)==observedToken)
    {
        completed.store(observedToken,std::memory_order_release);
        observedToken=0;observedStarted=false;
    }
    return status;
}
void __stdcall CloseHook(void* stream)
{
    {
        std::lock_guard lock(metadata);
        if(stream==observedStream){observedStream=nullptr;observedToken=0;observedStarted=false;}
    }
    originalClose(stream);
}
bool Queue(int track,int loop,std::uint32_t token)
{
    if(!queueOwner||!*queueOwner)return false;
    const auto previous=submitToken;submitToken=token;
    const bool accepted=EnqueueHook(*queueOwner,nullptr,playEntry,track,loop);
    submitToken=previous;
    return accepted;
}
}
bool InitializeScenarioMusicProgram()
{
    if(mainThread&&!Main())return false;
    mainThread=GetCurrentThreadId();
    if(ready)return true;
    if(!api||!api->IATHook||!api->InlineHook||!api->ResolveGameSymbol||!api->UnHook)return false;
    try
    {
        registerTrack=reinterpret_cast<Register>(Resolve("CSOL_Music_RegisterTrack",MH_GAMESYMBOL_KIND_FUNCTION));
        playEntry=Resolve("CSOL_Music_Play",MH_GAMESYMBOL_KIND_FUNCTION);
        auto* enqueue=Resolve("CSOL_Music_Enqueue",MH_GAMESYMBOL_KIND_FUNCTION);
        workerReturn=static_cast<std::byte*>(Resolve("CSOL_Music_Worker",MH_GAMESYMBOL_KIND_FUNCTION))+0x98;
        queueOwner=reinterpret_cast<void**>(Resolve("CSOL_Music_Queue",MH_GAMESYMBOL_KIND_GLOBAL));
        std::uint32_t queueEvent{};
        if(!queueOwner||!*queueOwner)throw std::runtime_error("native music queue is absent");
        std::memcpy(&queueEvent,static_cast<const std::byte*>(*queueOwner)+4,sizeof(queueEvent));
        if(queueEvent)throw std::runtime_error("native music observer requires cold engine initialization");
        hooks[0]=api->InlineHook(enqueue,reinterpret_cast<void*>(EnqueueHook),reinterpret_cast<void**>(&originalEnqueue));
        hooks[1]=api->InlineHook(playEntry,reinterpret_cast<void*>(PlayHook),reinterpret_cast<void**>(&originalPlay));
        auto module=reinterpret_cast<HMODULE>(api->GetEngineBase());
        hooks[2]=api->IATHook(module,"mss32.dll","_AIL_open_stream@12",reinterpret_cast<void*>(OpenHook),reinterpret_cast<void**>(&originalOpen));
        hooks[3]=api->IATHook(module,"mss32.dll","_AIL_start_stream@4",reinterpret_cast<void*>(StartHook),reinterpret_cast<void**>(&originalStart));
        hooks[4]=api->IATHook(module,"mss32.dll","_AIL_stream_status@4",reinterpret_cast<void*>(StatusHook),reinterpret_cast<void**>(&originalStatus));
        hooks[5]=api->IATHook(module,"mss32.dll","_AIL_close_stream@4",reinterpret_cast<void*>(CloseHook),reinterpret_cast<void**>(&originalClose));
        for(auto* hook:hooks)if(!hook)throw std::runtime_error("native music program hook is unavailable");
        ready=true;return true;
    }
    catch(const std::exception& error)
    {
        if(engine.Con_DPrintf)engine.Con_DPrintf("CSOLSupport: night music sequence: %s\n",error.what());
        ShutdownScenarioMusicProgram();return false;
    }
}
bool StartScenarioMusicProgram(const music::Packet& packet)
{
    if(!Main()||!ready||packet.action!=music::Action::PlayThenLoop||!music::Command(packet)||nextToken==UINT32_MAX)return false;
    CancelScenarioMusicProgram();
    const int intro=registerTrack(packet.path.c_str()),loop=registerTrack(packet.nextPath.c_str());
    if(intro<=1||loop<=1)return false;
    pending={++nextToken,intro,loop};currentToken.store(pending.token,std::memory_order_release);
    pending.introQueued=Queue(intro,0,pending.token);
    return true; // A full native queue is retried once per main frame.
}
void CancelScenarioMusicProgram()
{
    currentToken.store(0,std::memory_order_release);completed.store(0,std::memory_order_release);
    if(Main())pending={};
}
void ScenarioMusicProgramFrame()
{
    if(!Main()||!ready||!pending.token)return;
    if(currentToken.load(std::memory_order_acquire)!=pending.token){pending={};return;}
    if(!pending.introQueued){pending.introQueued=Queue(pending.intro,0,pending.token);return;}
    const auto finished=completed.exchange(0,std::memory_order_acq_rel);
    if(finished==pending.token)pending.tailReady=true;
    if(pending.tailReady&&!pending.tailQueued)pending.tailQueued=Queue(pending.loop,1,pending.token);
}
void ShutdownScenarioMusicProgram()
{
    if(!Main())return;
    CancelScenarioMusicProgram();ready=false;
    // Only native callback addresses were enqueued. They remain valid after
    // restoring the entrypoints; no queued pointer names this plugin's code.
    if(api)for(auto it=hooks.rbegin();it!=hooks.rend();++it)if(*it){api->UnHook(*it);*it=nullptr;}
    std::lock_guard lock(metadata);queuedCount=0;observedStream=nullptr;observedToken=0;observedStarted=false;mainThread=0;
}
}
