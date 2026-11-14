// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/client/scenario/core/scenario_music_program.cpp"
#include <iostream>
#include <algorithm>
#include <thread>
#include <vector>

namespace fixture
{
int checks{},opens{},starts{},closes{},statuses{},plays{},removed{};
int streamResult=4;
std::uintptr_t streamSerial=100;
DWORD playedOn{};
bool allowHooks=true,queueFull{},skipStart{};
struct Task{void* callback{};int a{},b{};};
std::vector<Task> tasks;
std::array<std::uint32_t,2> queueStorage{};
void* queue=queueStorage.data();
std::vector<std::pair<int,int>> actions;
void Check(bool value,const char* why)
{++checks;if(!value)throw std::runtime_error(why);}
bool __fastcall Enqueue(void*,void*,void* callback,int a,int b)
{
    if(queueFull||std::any_of(tasks.begin(),tasks.end(),[&](const auto& t){return t.callback==callback;}))return false;
    tasks.push_back({callback,a,b});return true;
}
int __cdecl Register(const char* path)
{return std::string_view(path)=="bgm/Play_BGM_Rex.mp3"?5:std::string_view(path)=="bgm/BGM_Rex_loop.mp3"?6:0;}
void* __stdcall Open(void*,const char*,int){++opens;return reinterpret_cast<void*>(streamSerial);}
void __stdcall Start(void*){++starts;}
int __stdcall Status(void*){++statuses;return streamResult;}
void __stdcall Close(void*){++closes;}
void __fastcall Play(void*,void*,int track,int loop)
{
    using namespace csol::client;
    ++plays;playedOn=GetCurrentThreadId();actions.emplace_back(track,loop);
    auto* stream=OpenHook(nullptr,"explicit native stream fixture",0);
    if(!skipStart)StartHook(stream);
}
void Execute()
{
    Check(!tasks.empty(),"native task exists");
    auto task=tasks.front();tasks.erase(tasks.begin());
    std::thread audio([&]{csol::client::ExecutePlay(reinterpret_cast<void*>(2),task.a,task.b,true);});
    audio.join();
}
void State(int status)
{
    streamResult=status;
    std::thread audio([]{csol::client::StatusHook(reinterpret_cast<void*>(streamSerial));});audio.join();
}
}
namespace csol::client
{
cl_enginefunc_t engine{};metahook_api_t* api{};
void* Resolve(const char* name,mh_gamesymbol_kind_t)
{
    if(std::string_view(name)=="CSOL_Music_RegisterTrack")return reinterpret_cast<void*>(fixture::Register);
    if(std::string_view(name)=="CSOL_Music_Play")return reinterpret_cast<void*>(fixture::Play);
    if(std::string_view(name)=="CSOL_Music_Enqueue")return reinterpret_cast<void*>(fixture::Enqueue);
    if(std::string_view(name)=="CSOL_Music_Worker")return reinterpret_cast<void*>(3);
    if(std::string_view(name)=="CSOL_Music_Queue")return &fixture::queue;
    throw std::runtime_error("unknown matched sequence fixture symbol");
}
}
int main()
{
    using namespace csol::client;using namespace fixture;
    try
    {
        tasks.reserve(32);actions.reserve(32);static metahook_api_t host{};api=&host;
        host.ResolveGameSymbol=[](void*,const char*,mh_gamesymbol_kind_t,void**)->mh_gamesymbol_status_t{return MH_GAMESYMBOL_OK;};
        host.InlineHook=[](void* target,void*,void** original)->hook_t*{
            *original=target;return allowHooks?reinterpret_cast<hook_t*>(1):nullptr;};
        host.IATHook=[](HMODULE,const char*,const char* name,void*,void** original)->hook_t*{
            const std::string_view symbol=name;
            if(symbol=="_AIL_open_stream@12")*original=reinterpret_cast<void*>(fixture::Open);
            else if(symbol=="_AIL_start_stream@4")*original=reinterpret_cast<void*>(fixture::Start);
            else if(symbol=="_AIL_stream_status@4")*original=reinterpret_cast<void*>(fixture::Status);
            else if(symbol=="_AIL_close_stream@4")*original=reinterpret_cast<void*>(fixture::Close);
            else throw std::runtime_error("wrong native import");
            return allowHooks?reinterpret_cast<hook_t*>(1):nullptr;};
        host.GetEngineBase=[]()->void*{return reinterpret_cast<void*>(4);};
        host.UnHook=[](hook_t*)->qboolean{++removed;return TRUE;};
        Check(InitializeScenarioMusicProgram(),"actual native sequence owner initializes matched hooks");
        const csol::music::Packet request{csol::music::Action::PlayThenLoop,11,
            "bgm/Play_BGM_Rex.mp3","bgm/BGM_Rex_loop.mp3"};
        auto encoded=csol::music::Encode(request);Check(!encoded.empty()&&encoded[0]==2,"two-path program uses v2");
        auto decoded=csol::music::Decode(std::as_bytes(std::span(encoded)));
        Check(decoded&&decoded->nextPath==request.nextPath,"actual protocol preserves tail");
        for(std::size_t size=0;size<encoded.size();++size)
            Check(!csol::music::Decode(std::as_bytes(std::span(encoded.data(),size))),"every truncated sequence rejected");
        for(const char* bad:{"../escape.mp3","bgm/a.mp3\nstop","bgm/a.ogg",""})
        {
            auto invalid=request;invalid.nextPath=bad;
            Check(csol::music::Encode(invalid).empty(),"tail command/path injection rejected");
        }
        Check(StartScenarioMusicProgram(request)&&tasks.size()==1,"intro submits one native callback");
        Check(tasks[0].callback==playEntry,"queued callback address is native, never DLL code");
        Execute();Check(actions.back()==std::pair{5,0},"intro uses one-shot native Play");
        Check(playedOn!=GetCurrentThreadId(),"native play/open/start run on controlled audio thread");
        for(int state:{4,8,-1,1}){State(state);ScenarioMusicProgramFrame();Check(tasks.empty(),"playing/paused/error never starts tail");}
        State(2);ScenarioMusicProgramFrame();Check(tasks.size()==1,"actual done status queues tail once");
        for(int i=0;i<100;++i)ScenarioMusicProgramFrame();
        Check(tasks.size()==1,"ordinary frames do not repeat tail submission");
        Execute();Check(actions.back()==std::pair{6,1},"tail starts native indefinite loop");
        State(2);ScenarioMusicProgramFrame();Check(tasks.empty(),"tail has no intro completion rights");

        Check(StartScenarioMusicProgram(request),"next request accepted");
        const auto beforeCancel=plays;CancelScenarioMusicProgram();Execute();
        Check(plays==beforeCancel,"cancelled pending native callback cannot start old intro");
        Check(StartScenarioMusicProgram(request),"new generation accepted");Execute();
        CancelScenarioMusicProgram();State(2);ScenarioMusicProgramFrame();Check(tasks.empty(),"old stream done cannot resurrect tail");
        Check(StartScenarioMusicProgram(request),"next intro accepted");Execute();
        CloseHook(reinterpret_cast<void*>(streamSerial));State(2);ScenarioMusicProgramFrame();
        Check(tasks.empty(),"closed/reused stream cannot signal old completion");
        CancelScenarioMusicProgram();skipStart=true;
        Check(StartScenarioMusicProgram(request),"unstarted stream test accepted");Execute();
        State(2);ScenarioMusicProgramFrame();Check(tasks.empty(),"not-started stream is never completed playback");skipStart=false;
        CancelScenarioMusicProgram();queueFull=true;
        Check(StartScenarioMusicProgram(request)&&tasks.empty(),"full native queue retains bounded pending intent");
        for(int i=0;i<100;++i)ScenarioMusicProgramFrame();Check(tasks.empty(),"full queue retry never invents successful playback");
        queueFull=false;ScenarioMusicProgramFrame();Check(tasks.size()==1,"main frame retries after queue becomes available");Execute();
        State(2);queueFull=true;ScenarioMusicProgramFrame();Check(tasks.empty(),"full tail queue retains completion");
        queueFull=false;ScenarioMusicProgramFrame();Check(tasks.size()==1,"tail retries without restarting intro");Execute();
        auto foreign=request;foreign.path="bgm/unknown.mp3";
        Check(!StartScenarioMusicProgram(foreign),"unregistered track rejected");
        Check(StartScenarioMusicProgram(request),"shutdown test has pending native task");
        ShutdownScenarioMusicProgram();Check(removed==6&&!ready,"shutdown restores all matched native hooks");
        Check(!StartScenarioMusicProgram(request),"uninstalled native capability cannot claim accepted sequence");
        Check(tasks[0].callback==reinterpret_cast<void*>(Play),"remaining queued address remains valid native entry");
        tasks.clear();
        queueStorage[1]=1;
        Check(!InitializeScenarioMusicProgram(),"active preexisting native queue cannot be silently observed late");
        queueStorage[1]=0;
        std::cout<<"PASS BGM actual program "<<checks<<" checks; native queue/audio/imports are controlled ABI boundaries\n";
    }
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
