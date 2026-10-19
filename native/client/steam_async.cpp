#include <metahook.h>
#include "steam_async.hpp"
#include "steam_async_state.hpp"
#include "steam_async_transport.hpp"
#include <Windows.h>
#include <array>
#include <memory>
#include <ostream>
#include <intrin.h>

namespace goldcraft::steam_async {
namespace {
// This deliberately describes only the verified public ABI, not private
// steamclient layouts. SDK1.61: SteamClient021, SteamUser023,
// SteamFriends017, SteamUtils010. Authentication slots are never replaced.
template<class R,class... A> R invoke(void* fn,void* object,A... args){
    return reinterpret_cast<R(__thiscall*)(void*,A...)>(fn)(object,args...);
}
struct Binding {
    void* user{};void* friends{};void* utils{};
    std::array<void*,4> voice{};
    std::array<void*,4> friend_calls{};
    std::array<void*,2> image{};
};
struct WorkerState {
    State state;
    Binding avatars,voice;
    std::atomic<bool> avatars_bound{},voice_bound{};
    HelperStatus avatar_status,voice_status;
    std::atomic<bool> hooks_released{};
    HANDLE cancel=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    std::wstring executable,steam;
    ~WorkerState(){if(cancel)CloseHandle(cancel);}
};

metahook_api_t* api{};
std::shared_ptr<WorkerState> state;
DWORD game_thread{};
std::uintptr_t client_begin{},client_end{};
void** user_context{},**friends_context{},**utils_context{};
std::array<hook_t*,12> hooks{};
unsigned hook_count{};
Binding originals;
bool avatar_hooks{},voice_hooks{};
void* (__cdecl* engine_context_original)(void*){};
void* (__cdecl* client_context_original)(void*){};
unsigned failures{};
std::atomic<unsigned> avatar_hits{},voice_hits{};
std::atomic<unsigned> running_workers{};
bool game(){return GetCurrentThreadId()==game_thread&&state&&!state->state.stopping.load();}
bool client_call(void* caller){
    const auto address=reinterpret_cast<std::uintptr_t>(caller);
    return game()&&(!client_begin||(address>=client_begin&&address<client_end));
}
void* __cdecl engine_context(void* context){
    auto* result=engine_context_original(context);
    if(game()&&(!avatar_hooks||!voice_hooks))frame();
    return result;
}
void* __cdecl client_context(void* context){
    auto* result=client_context_original(context);
    if(game()&&(!avatar_hooks||!voice_hooks))frame();
    return result;
}

int __fastcall relationship(void* self,void*,SteamId id){
    if(!client_call(_ReturnAddress()))return invoke<int>(originals.friend_calls[0],self,id);
    ++avatar_hits;return state->state.relationship(id);
}
template<unsigned Size> int __fastcall avatar(void* self,void*,SteamId id){
    if(!client_call(_ReturnAddress()))return invoke<int>(originals.friend_calls[Size+1],self,id);
    ++avatar_hits;return state->state.avatar(id,Size);
}
bool __fastcall image_size(void* self,void*,int handle,unsigned* width,unsigned* height){
    if(client_call(_ReturnAddress())&&handle>=image_base)return state->state.image_size(handle,width,height);
    return invoke<bool>(originals.image[0],self,handle,width,height);
}
bool __fastcall image_rgba(void* self,void*,int handle,std::uint8_t* data,int bytes){
    if(client_call(_ReturnAddress())&&handle>=image_base)return state->state.image_rgba(handle,data,bytes);
    return invoke<bool>(originals.image[1],self,handle,data,bytes);
}
void __fastcall start_voice(void* self,void*){
    if(!game()){invoke<void>(originals.voice[0],self);return;}
    ++voice_hits;state->state.start_voice();
}
void __fastcall stop_voice(void* self,void*){
    if(!game()){invoke<void>(originals.voice[1],self);return;}
    ++voice_hits;state->state.stop_voice();
}
VoiceResult __fastcall available(void* self,void*,unsigned* compressed,unsigned* raw,unsigned rate){
    if(!game())return invoke<VoiceResult>(originals.voice[2],self,compressed,raw,rate);
    ++voice_hits;return state->state.available(compressed,raw,rate);
}
VoiceResult __fastcall voice(void* self,void*,bool compressed,void* data,unsigned capacity,unsigned* written,
    bool pcm,void* raw,unsigned raw_capacity,unsigned* raw_written,unsigned rate){
    if(!game())return invoke<VoiceResult>(originals.voice[3],self,compressed,data,capacity,written,pcm,raw,raw_capacity,raw_written,rate);
    ++voice_hits;return state->state.voice(compressed,data,capacity,written,pcm,raw,raw_capacity,raw_written,rate);
}

struct Work {std::shared_ptr<WorkerState> state;HMODULE pin{};bool voice{};};
DWORD WINAPI worker(void* opaque){
    HMODULE pin;
    {std::unique_ptr<Work> work(static_cast<Work*>(opaque));pin=work->pin;
        auto& s=*work->state;
        ProcessBackend backend(s.executable,s.steam,s.cancel,work->voice?&s.voice_status:&s.avatar_status);
        try {
            if(work->voice)s.state.run_voice(backend);
            else s.state.run_avatars(backend);
        }catch(...){work->state->state.stopping.store(true);}
    }
    // The worker owns the plugin reference while Steam is blocked. Releasing it
    // and ending the thread are atomic; no return into an unloaded DLL.
    --running_workers;FreeLibraryAndExitThread(pin,0);
}
bool start_worker(bool voice_worker){
    HMODULE pin{};
    if(!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<const char*>(&worker),&pin))return false;
    auto work=std::make_unique<Work>();work->state=state;work->pin=pin;work->voice=voice_worker;
    ++running_workers;
    const auto thread=CreateThread(nullptr,0,worker,work.get(),0,nullptr);
    if(!thread){--running_workers;FreeLibrary(pin);return false;}
    work.release();CloseHandle(thread);return true;
}
bool hook(void* object,int slot,void* replacement,void** original){
    auto h=api->VFTHook(object,0,slot,replacement,original);
    if(!h)return false;
    hooks[hook_count++]=h;return true;
}
void rollback(unsigned first){while(hook_count>first)api->UnHook(hooks[--hook_count]);}
}
void install(metahook_api_s* value){
    if(state)return;
    if(running_workers.load()){++failures;return;}
    api=value;game_thread=GetCurrentThreadId();
    auto resolve=[&](void* module,const char* name)->void**{
        void* context=nullptr;
        if(api->ResolveGameSymbol(module,name,MH_GAMESYMBOL_KIND_GLOBAL,&context)!=MH_GAMESYMBOL_OK||!context)return nullptr;
        // Verified SteamContext descriptor: init callback, counter, interface.
        return static_cast<void**>(context)+2;
    };
    user_context=resolve(api->GetEngineBase(),"GoldCraftSteam_UserContext");
    friends_context=resolve(api->GetClientBase(),"GoldCraftSteam_FriendsContext");
    utils_context=resolve(api->GetClientBase(),"GoldCraftSteam_UtilsContext");
    if(!user_context||!friends_context||!utils_context){++failures;return;}
    client_begin=reinterpret_cast<std::uintptr_t>(api->GetClientBase());
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(client_begin);
    const auto* pe=reinterpret_cast<const IMAGE_NT_HEADERS*>(client_begin+dos->e_lfanew);
    client_end=client_begin+pe->OptionalHeader.SizeOfImage;
    state=std::make_shared<WorkerState>();
    wchar_t own_path[32768]{},steam_path[32768]{};HMODULE own{};
    const auto steam_module=GetModuleHandleW(L"steamclient.dll");
    if(!state->cancel||!steam_module||!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<const wchar_t*>(&install),&own)||!GetModuleFileNameW(own,own_path,32768)||
        !GetModuleFileNameW(steam_module,steam_path,32768)){++failures;shutdown();return;}
    const std::wstring path=own_path;
    state->executable=path.substr(0,path.find_last_of(L"/\\")+1)+L"GoldCraftSteamWorker.exe";
    state->steam=steam_path;
    // Catch lazy interface initialization before its first avatar/voice call,
    // including the first TAB press. Keep Steam's real initialization intact.
    const auto engine_hook=api->IATHook(static_cast<HMODULE>(api->GetEngineBase()),"steam_api.dll","SteamInternal_ContextInit",
        reinterpret_cast<void*>(engine_context),reinterpret_cast<void**>(&engine_context_original));
    if(!engine_hook){++failures;shutdown();return;}hooks[hook_count++]=engine_hook;
    const auto client_hook=api->IATHook(static_cast<HMODULE>(api->GetClientBase()),"steam_api.dll","SteamInternal_ContextInit",
        reinterpret_cast<void*>(client_context),reinterpret_cast<void**>(&client_context_original));
    if(!client_hook){++failures;shutdown();return;}hooks[hook_count++]=client_hook;
    if(!start_worker(false)||!start_worker(true)){++failures;shutdown();}
}
void frame(){
    if(!state)return;
    if(state->state.stopping.load()){shutdown();return;}
    if(user_context&&friends_context&&utils_context){
        // Install as soon as the original contexts exist. Waiting for helper
        // readiness here would let the first query block during helper startup.
        if(*user_context){state->voice.user=*user_context;state->voice_bound=true;}
        if(*friends_context&&*utils_context){
            state->avatars.friends=*friends_context;state->avatars.utils=*utils_context;state->avatars_bound=true;
        }
    }
    if(!avatar_hooks&&state->avatars_bound.load(std::memory_order_acquire)){
        const auto begin=hook_count;auto& b=state->avatars;
        // Image consumers are installed before producing synthetic handles.
        bool ok=hook(b.utils,5,reinterpret_cast<void*>(image_size),&originals.image[0])&&
            hook(b.utils,6,reinterpret_cast<void*>(image_rgba),&originals.image[1])&&
            hook(b.friends,5,reinterpret_cast<void*>(relationship),&originals.friend_calls[0])&&
            hook(b.friends,34,reinterpret_cast<void*>(avatar<0>),&originals.friend_calls[1])&&
            hook(b.friends,35,reinterpret_cast<void*>(avatar<1>),&originals.friend_calls[2])&&
            hook(b.friends,36,reinterpret_cast<void*>(avatar<2>),&originals.friend_calls[3]);
        if(!ok){rollback(begin);++failures;shutdown();return;}else avatar_hooks=true;
    }
    if(!voice_hooks&&state->voice_bound.load(std::memory_order_acquire)){
        const auto begin=hook_count;auto* user=state->voice.user;
        bool ok=hook(user,7,reinterpret_cast<void*>(start_voice),&originals.voice[0])&&
            hook(user,8,reinterpret_cast<void*>(stop_voice),&originals.voice[1])&&
            hook(user,9,reinterpret_cast<void*>(available),&originals.voice[2])&&
            hook(user,10,reinterpret_cast<void*>(voice),&originals.voice[3]);
        if(!ok){rollback(begin);++failures;shutdown();return;}else voice_hooks=true;
    }
}
void shutdown(){
    if(!state)return;
    state->state.stop_voice();state->state.stopping.store(true);SetEvent(state->cancel);
    rollback(0);avatar_hooks=voice_hooks=false;state->hooks_released.store(true);state.reset();
    user_context=friends_context=utils_context=nullptr;
    client_begin=client_end=0;
}
void diagnostics(std::ostream& out){
    out<<"{\"avatarsHooked\":"<<(avatar_hooks?"true":"false")<<",\"voiceHooked\":"<<(voice_hooks?"true":"false")
       <<",\"avatarCalls\":"<<avatar_hits.load()<<",\"voiceCalls\":"<<voice_hits.load()<<",\"installFailures\":"<<failures
       <<",\"workers\":"<<running_workers.load();
    if(state){auto& c=state->state.counters;
        out<<",\"avatarsCompleted\":"<<c.avatar_completed.load()<<",\"voicePackets\":"<<c.voice_packets.load()
           <<",\"voiceDropped\":"<<c.dropped.load()<<",\"lockMisses\":"<<c.lock_misses.load()
           <<",\"voiceResult\":"<<c.last_voice_result.load()<<",\"avatarReady\":"<<c.avatar_ready.load()
           <<",\"voiceReady\":"<<c.voice_ready.load()<<",\"avatarInitResult\":"<<state->avatar_status.initialization.load()
           <<",\"voiceInitResult\":"<<state->voice_status.initialization.load();
        auto helper=[&](const char* name,HelperStatus& s){const auto stamp=s.sample_tick_ms.load();const auto now=GetTickCount64();
            out<<",\""<<name<<"\":{\"appId\":"<<s.app_id.load()
            <<",\"loggedOn\":"<<s.logged_on.load()<<",\"samples\":"<<s.samples.load()<<",\"failures\":"<<s.failures.load()
            <<",\"sampleAgeMs\":"<<(stamp?static_cast<long long>(now>=stamp?now-stamp:0):-1)<<'}';};
        helper("avatarHelper",state->avatar_status);helper("voiceHelper",state->voice_status);}
    out<<'}';
}
}
