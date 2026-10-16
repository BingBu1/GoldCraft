// Compile the actual x86 ABI adapters and hook transaction, not a copy.
#include "steam_async.cpp"
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>
using namespace goldcraft::steam_async;
namespace {
struct Object {void** vtable;};
struct Saved {void** slot;void* original;};
std::array<Saved,10> saved{};
unsigned count{},fail_at=99;
std::atomic<unsigned> native_calls{};
void* lazy_user{},*lazy_friends{},*lazy_utils{};
void* __cdecl lazy_context(void*){
    *user_context=lazy_user;*friends_context=lazy_friends;*utils_context=lazy_utils;
    return user_context;
}
hook_t* fake_hook(void* object,int,int slot,void* replacement,void** original){
    if(count==fail_at)return nullptr;
    auto& h=saved[count++];h.slot=&(*static_cast<void***>(object))[slot];h.original=*h.slot;
    *original=*h.slot;*h.slot=replacement;return reinterpret_cast<hook_t*>(&h);
}
// Match MetaHook's actual bool-returning UnHook signature below via decltype.
int fake_unhook(hook_t* hook){auto& h=*reinterpret_cast<Saved*>(hook);*h.slot=h.original;--count;return 1;}
void __fastcall original_start(void*,void*){++native_calls;}
void __fastcall original_stop(void*,void*){++native_calls;}
VoiceResult __fastcall original_available(void*,void*,unsigned*,unsigned*,unsigned){++native_calls;return VoiceResult::no_data;}
VoiceResult __fastcall original_voice(void*,void*,bool,void*,unsigned,unsigned*,bool,void*,unsigned,unsigned*,unsigned){++native_calls;return VoiceResult::no_data;}
int __fastcall original_friend(void*,void*,SteamId){++native_calls;return 17;}
bool __fastcall original_size(void*,void*,int,unsigned*,unsigned*){++native_calls;return false;}
bool __fastcall original_rgba(void*,void*,int,std::uint8_t*,int){++native_calls;return false;}
}
int main(){
    metahook_api_t fake{};fake.VFTHook=fake_hook;fake.UnHook=reinterpret_cast<decltype(fake.UnHook)>(fake_unhook);
    std::array<void*,40> users{},friends{},utils{};
    users[7]=reinterpret_cast<void*>(original_start);users[8]=reinterpret_cast<void*>(original_stop);
    users[9]=reinterpret_cast<void*>(original_available);users[10]=reinterpret_cast<void*>(original_voice);
    friends[5]=friends[34]=friends[35]=friends[36]=reinterpret_cast<void*>(original_friend);
    utils[5]=reinterpret_cast<void*>(original_size);utils[6]=reinterpret_cast<void*>(original_rgba);
    Object u{users.data()},f{friends.data()},v{utils.data()};
    auto setup=[&]{
        state=std::make_shared<WorkerState>();api=&fake;game_thread=GetCurrentThreadId();
        state->avatars.friends=&f;state->avatars.utils=&v;state->voice.user=&u;
        state->avatars_bound=true;state->voice_bound=true;
    };
    setup();frame();assert(avatar_hooks&&voice_hooks&&count==10);
    auto retained=state;
    // Authentication and voice decode slots were never part of the patch.
    assert(users[0]==nullptr&&users[11]==nullptr&&users[13]==nullptr);
    assert(invoke<int>(friends[35],&f,SteamId{42})==0);
    assert(invoke<int>(friends[5],&f,SteamId{42})==0);
    unsigned n=9,r=9;
    invoke<void>(users[7],&u);
    assert(invoke<VoiceResult>(users[9],&u,&n,&r,0u)==VoiceResult::no_data&&n==0&&r==0);
    unsigned char data[8]{};
    assert(invoke<VoiceResult>(users[10],&u,true,static_cast<void*>(data),8u,&n,false,
        static_cast<void*>(nullptr),0u,&r,0u)==VoiceResult::no_data);
    invoke<void>(users[8],&u);
    assert(invoke<VoiceResult>(users[9],&u,&n,&r,0u)==VoiceResult::no_data);
    assert(native_calls==0);
    std::thread other([&]{assert(invoke<VoiceResult>(users[9],&u,&n,&r,0u)==VoiceResult::no_data);});other.join();
    assert(native_calls==1);
    assert(!invoke<bool>(utils[5],&v,image_base+5,&n,&r));assert(native_calls==1);
    assert(!invoke<bool>(utils[5],&v,42,&n,&r));assert(native_calls==2);
    shutdown();assert(count==0&&retained->hooks_released&&retained->state.stopping);
    assert(users[9]==reinterpret_cast<void*>(original_available));
    // Lazy Steam context initialization must install before the first query,
    // not require a second TAB press. No extra Steam initializer is invented.
    setup();state->avatars_bound=false;state->voice_bound=false;
    // Helpers are deliberately still starting; that must not expose the
    // original blocking query on the first lazy context initialization.
    assert(!state->state.counters.avatar_ready&&!state->state.counters.voice_ready);
    void* user_slot=nullptr,*friends_slot=nullptr,*utils_slot=nullptr;
    user_context=&user_slot;friends_context=&friends_slot;utils_context=&utils_slot;
    lazy_user=&u;lazy_friends=&f;lazy_utils=&v;engine_context_original=lazy_context;
    assert(engine_context(nullptr)==user_context);assert(avatar_hooks&&voice_hooks);
    const auto before=native_calls.load();assert(invoke<int>(friends[35],&f,SteamId{42})==0);
    assert(native_calls==before);
    // A different module keeps the real image-handle namespace and API.
    client_begin=1;client_end=2;
    assert(invoke<int>(friends[35],&f,SteamId{42})==17);assert(native_calls==before+1);
    client_begin=client_end=0;shutdown();user_context=friends_context=utils_context=nullptr;
    // Every possible partial installation must roll back every installed slot.
    for(fail_at=0;fail_at<10;++fail_at){setup();auto held=state;frame();assert(!state&&count==0&&held->hooks_released);}
    std::cout<<"Steam async x86 adapter: ABI, main/worker routing, lazy first query, caller isolation, untouched auth/decode, complete rollback passed\n";
}
