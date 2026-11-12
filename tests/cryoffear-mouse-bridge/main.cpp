// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/client/runtime.hpp"
#include "../../native/csol/client/resource_refresh.hpp"
#include "../../native/csol/client/weapons/guest_cof_mouse.hpp"
#include "../../native/csol/client/weapons/guest_cof.hpp"
#include "../../native/csol/client/weapons/guest_cof_effects.hpp"
#include "../../native/guest/cof/client/mouse_input.hpp"
#include "../../native/guest/cof/presentation_protocol.hpp"
#include "../../native/include/goldcraft/connection_api.h"
#include <array>
#include <bit>
#include <cstdio>
#include <cstring>
#include <thread>

namespace csol::client {
metahook_api_t* api{};cl_enginefunc_t engine{};ref_params_t view{};cl_exportfuncs_t downstream{};
void SynchronizeSession(){} // One controlled session; real-session cases are in the FOV bridge fixture.
void SetGuestCofEffectsWeapon(guest::cof::client::Weapon) noexcept{} // Rendering is outside this input fixture.
void ResetGuestCofCrosshair() noexcept{} // Crosshair integration has its own actual message/SDK test.
void GuestCofCrosshairPresentation(const guest::cof::PresentationState&) noexcept{}
}
namespace
{
using namespace csol::client;
using namespace guest::cof::client;
unsigned checks{}, failures{}, hooks{}, removes{}, queries{}, nativeCalls{}, resumes{}, generationCalls{};
unsigned char* image{};
void* replacement{};
constexpr std::size_t ImageBytes=4096;
std::uint64_t generation{5};
bool resolve{true},revoke{},shutdownInside{},replaceFrame{},queryReentry{};
bool retireOnInstall{};
bool deferTrampoline{};void** deferredTrampoline{};
cvar_t filter{},sensitivity{},yaw{};
cvar_t defaultFov{},zoomSpeed{},zoomRatio{},multiplier{};
float clockTime=1;
pfnUserMsgHook message{};
std::int32_t *previous{}, *accumulated{};
float* deltas{};
std::array<std::int32_t,2> raw{};
std::array<std::uint32_t,2> output{};
cvar_t* routedYaw{};
std::uint32_t beforeFlags{},afterFlags{},beforeStack{},afterStack{};
std::uint32_t xmm4Seed=0x7fc12345,xmm6Seed=0xc0600000,xmm4Seen{},xmm6Seen{};
struct Site {const char* name;unsigned offset;};
constexpr Site sites[]{
    {"Stage",0},{"Resume",64},{"HistoryReference",128},{"DeltasReference",192},
    {"AccumReference",256},{"YawReference",320}};
void Check(bool okay,int line){++checks;if(!okay){++failures;std::printf("FAIL %d\n",line);}}
#define CHECK(...) Check((__VA_ARGS__),__LINE__)
void* Client(){return image;}
DWORD Size(){return ImageBytes;}
std::uint64_t Generation()
{
    __asm {
        xorps xmm4, xmm4
        xorps xmm6, xmm6
    }
    ++generationCalls;
    if(queryReentry)return generation;
    queryReentry=true;
    if(revoke){revoke=false;ResetGuestCofMouse();}
    if(shutdownInside){shutdownInside=false;ShutdownGuestCofMouse();}
    if(replaceFrame){replaceFrame=false;SetGuestCofMouseFrame({3,4,6},90,90,0);}
    queryReentry=false;
    return generation;
}
mh_gamesymbol_status_t Resolve(void* module,const char* name,mh_gamesymbol_kind_t kind,void** out)
{
    CHECK(module==image&&kind==MH_GAMESYMBOL_KIND_PATCH);
    if(!std::strcmp(name,"GoldCraftCof_ViewmodelBobLoad"))return MH_GAMESYMBOL_SYMBOL_NOT_FOUND;
    constexpr const char* prefix="GoldCraftCof_Mouse";
    CHECK(!std::strncmp(name,prefix,std::strlen(prefix)));
    if(!resolve)return MH_GAMESYMBOL_SYMBOL_NOT_FOUND;
    for(const auto& site:sites)if(!std::strcmp(name+std::strlen(prefix),site.name))
    {*out=image+site.offset;return MH_GAMESYMBOL_OK;}
    CHECK(false);return MH_GAMESYMBOL_SYMBOL_NOT_FOUND;
}
cvar_t* Setting(const char* name)
{
    ++queries;
    if(!std::strcmp(name,"m_filter"))return &filter;
    if(!std::strcmp(name,"sensitivity"))return &sensitivity;
    if(!std::strcmp(name,"default_fov"))return &defaultFov;
    if(!std::strcmp(name,"cl_zoomspeed"))return &zoomSpeed;
    if(!std::strcmp(name,"zoom_sensitivity_ratio"))return &zoomRatio;
    if(!std::strcmp(name,"cl_fovmultiplier"))return &multiplier;
    CHECK(!std::strcmp(name,"m_yaw"));return &yaw;
}
pfnUserMsgHook HookMessage(const char* name,pfnUserMsgHook callback)
{CHECK(!std::strcmp(name,guest::cof::StateMessage));auto old=message;if(old)message=callback;return old;}
int RegisterMessage(const char* name,pfnUserMsgHook callback)
{CHECK(!std::strcmp(name,guest::cof::StateMessage));message=callback;return 1;}
float Time(){return clockTime;}
int NativeFov(client_data_s* data,float){data->fov=90;return 37;}
__declspec(naked) void Native()
{
    __asm {
        inc dword ptr [nativeCalls]
        ret
    }
}
__declspec(naked) void Resume()
{
    __asm {
        inc dword ptr [resumes]
        movss dword ptr [output], xmm1
        movss dword ptr [output+4], xmm2
        mov dword ptr [routedYaw], edx
        ret
    }
}
__declspec(naked) void Execute()
{
    __asm {
        pushfd
        pushad
        mov esi, dword ptr [raw]
        mov edi, dword ptr [raw+4]
        movss xmm4, dword ptr [xmm4Seed]
        movss xmm6, dword ptr [xmm6Seed]
        pushfd
        pop eax
        mov dword ptr [beforeFlags], eax
        mov dword ptr [beforeStack], esp
        call dword ptr [replacement]
        movss dword ptr [xmm4Seen], xmm4
        movss dword ptr [xmm6Seen], xmm6
        mov dword ptr [afterStack], esp
        pushfd
        pop eax
        mov dword ptr [afterFlags], eax
        popad
        popfd
        ret
    }
}
void Pointer(unsigned offset,const void* pointer){std::memcpy(image+offset,&pointer,4);}
void Fill()
{
    std::memset(image,0,ImageBytes);
    const unsigned char stage[]{0xa1,0,0,0,0};std::memcpy(image,stage,sizeof(stage));Pointer(1,image+600);
    const unsigned char next[]{0x8b,0x0d,0,0,0,0,0xb8,0,0,0,0,0xff,0xe0};
    std::memcpy(image+64,next,sizeof(next));Pointer(64+2,image+620);Pointer(64+7,reinterpret_cast<void*>(Resume));
    const unsigned char old[]{0x89,0x35,0,0,0,0,0x89,0x3d,0,0,0,0};
    std::memcpy(image+128,old,sizeof(old));Pointer(130,image+700);Pointer(136,image+704);
    const unsigned char output[]{0xf3,0x0f,0x11,0x05,0,0,0,0,0xf3,0x0f,0x11,0x15,0,0,0,0};
    std::memcpy(image+192,output,sizeof(output));Pointer(196,image+720);Pointer(204,image+724);
    const unsigned char clear[]{0xc7,0x05,0,0,0,0,0,0,0,0,0xc7,0x05,0,0,0,0,0,0,0,0};
    std::memcpy(image+256,clear,sizeof(clear));Pointer(258,image+740);Pointer(268,image+744);
    image[320]=0x8b;image[321]=0x15;Pointer(322,image+608);
    Pointer(600,&filter);Pointer(608,&yaw);
    previous=reinterpret_cast<std::int32_t*>(image+700);deltas=reinterpret_cast<float*>(image+720);
    accumulated=reinterpret_cast<std::int32_t*>(image+740);
}
void Sample(const MouseInput& input)
{
    const MouseHistory history{previous[0],previous[1]};
    const auto expected=ScaleMouse(history,input);
    filter.value=input.filter;sensitivity.value=input.sensitivity;
    raw={input.x,input.y};accumulated[0]=31;accumulated[1]=-19;
    CHECK(SetGuestCofMouseFrame({3,4,5},input.wholeFov,input.defaultFov,input.hudSensitivity));
    const auto native=nativeCalls,count=resumes;
    Execute();
    CHECK(resumes==count+1&&nativeCalls==native&&routedYaw==&yaw);
    CHECK(output[0]==std::bit_cast<std::uint32_t>(float(expected.x))&&output[1]==std::bit_cast<std::uint32_t>(float(expected.y)));
    CHECK(previous[0]==input.x&&previous[1]==input.y&&!accumulated[0]&&!accumulated[1]);
    CHECK(beforeStack==afterStack);
    CHECK(xmm4Seen==xmm4Seed&&xmm6Seen==xmm6Seed);
}
}
namespace csol::client
{
hook_t* Hook(void* address,void* next,void** original)
{
    CHECK(address==image);++hooks;replacement=next;
    if(deferTrampoline){deferredTrampoline=original;*original=nullptr;}
    else *original=reinterpret_cast<void*>(Native);
    if(retireOnInstall){ShutdownGuestCofMouse();GuestCofMouseFrame();}
    return reinterpret_cast<hook_t*>(1);
}
void RemoveHook(hook_t* pointer){CHECK(pointer==reinterpret_cast<hook_t*>(1));++removes;deferredTrampoline=nullptr;}
}
int main()
{
    CHECK(!InitializeGuestCofMouse(nullptr));
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,ImageBytes,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    CHECK(image);Fill();
    metahook_api_t table{};api=&table;table.GetClientBase=Client;table.GetClientSize=Size;table.ResolveGameSymbol=Resolve;
    engine.pfnGetCvarPointer=Setting;
    GoldCraftConnectionAPI provider{sizeof(provider),1,Generation};
    resolve=false;CHECK(!InitializeGuestCofMouse(&provider));resolve=true;
    image[0]=0;CHECK(!InitializeGuestCofMouse(&provider));image[0]=0xa1;
    Pointer(136,image+708);CHECK(!InitializeGuestCofMouse(&provider));Pointer(136,image+704);
    image[262]=1;CHECK(!InitializeGuestCofMouse(&provider));image[262]=0;
    CHECK(!hooks);retireOnInstall=true;
    CHECK(!InitializeGuestCofMouse(&provider)&&!GuestCofMouseReady()&&hooks==1&&removes==1);
    retireOnInstall=false;
    CHECK(InitializeGuestCofMouse(&provider)&&GuestCofMouseReady()&&hooks==2);
    CHECK(InitializeGuestCofMouse(&provider)&&hooks==2);
    view.viewentity=1;view.playernum=0;view.health=100;
    Execute();CHECK(nativeCalls==1&&!resumes);
    const auto coldQueries=queries;
    for(auto filter:{0.0F,1.0F})for(auto hud:{0.0F,.375F,-.75F})for(auto fov:{50,90})
        for(auto x:{-16777217,-101,-3,0,3,101,16777217})
            Sample({x,-x,filter,hud,2.5F,fov,90});
    CHECK(queries==coldQueries);
    for(auto* gate:{&view.spectator,&view.intermission,&view.paused})
    {*gate=1;const auto native=nativeCalls;Execute();CHECK(nativeCalls==native+1);*gate=0;}
    view.viewentity=2;const auto native=nativeCalls;Execute();CHECK(nativeCalls==native+1);view.viewentity=1;
#if CSOL_RESOURCE_REFRESH
    resource_refresh_detail::readsAllowed=false;Execute();CHECK(nativeCalls==native+2);resource_refresh_detail::readsAllowed=true;
#endif
    const auto old=resumes;revoke=true;Execute();CHECK(resumes==old);CHECK(GuestCofMouseReady());
    Sample({3,5,0,.5F,2.5F,50,90});
    replaceFrame=true;const auto cancelled=resumes;Execute();CHECK(resumes==cancelled);
    Sample({3,5,0,.5F,2.5F,50,90});
    ++generation;const auto count=resumes;Execute();CHECK(resumes==count);
    CHECK(SetGuestCofMouseFrame({3,4,6},50,90,.5F));
    bool foreignReady=true;
    const auto foreignNative=nativeCalls,foreignResumes=resumes;
    std::thread other([&]{foreignReady=GuestCofMouseReady();ResetGuestCofMouse();ShutdownGuestCofMouse();Execute();});other.join();
    CHECK(!foreignReady&&GuestCofMouseReady());
    CHECK(nativeCalls==foreignNative+1&&resumes==foreignResumes);
    Sample({3,5,0,.5F,2.5F,50,90});
    shutdownInside=true;const auto retired=removes;Execute();CHECK(removes==retired&&!GuestCofMouseReady());
    GuestCofMouseFrame();CHECK(removes==retired+1);
    CHECK(InitializeGuestCofMouse(&provider));ShutdownGuestCofMouse();CHECK(removes==retired+2);
    // Actual accepted wire -> downstream HUD -> original FOV helper -> mouse
    // frame -> executing native detour. No replacement sensitivity setter.
    table.HookUserMsg=HookMessage;engine.pfnHookUserMsg=RegisterMessage;engine.GetClientTime=Time;
    downstream.HUD_UpdateClientData=NativeFov;
    defaultFov.value=90;sensitivity.value=2.5F;zoomSpeed.value=100;zoomRatio.value=1.2F;multiplier.value=1;
    InitializeGuestCof(&provider);CHECK(GuestCofMouseReady()&&message);
    guest::cof::PresentationState state{{21,22,23},1,3,true,50,2800};
    guest::cof::StatePacket packet{};CHECK(guest::cof::EncodeState(state,packet));
    CHECK(message(guest::cof::StateMessage,int(packet.size()),packet.data())==1);
    client_data_t data{};CHECK(GuestCofUpdateClientData(&data,clockTime)==37&&data.fov==90);
    clockTime=1.2F;CHECK(GuestCofUpdateClientData(&data,clockTime)==37&&data.fov==50);
    raw={5,-5};filter.value=0;const auto routed=resumes;Execute();
    CHECK(resumes==routed+1&&output[0]==std::bit_cast<std::uint32_t>(4.0F)&&output[1]==std::bit_cast<std::uint32_t>(-4.0F));
    state.revision=2;state.originalWeapon=0;state.aimEnabled=false;state.fovMode=0;
    CHECK(guest::cof::EncodeState(state,packet));message(guest::cof::StateMessage,int(packet.size()),packet.data());
    CHECK(GuestCofUpdateClientData(&data,clockTime)==37&&data.fov==90);
    const auto nativeAgain=nativeCalls;Execute();CHECK(nativeCalls==nativeAgain+1&&resumes==routed+1);
    ShutdownGuestCof();CHECK(!GuestCofMouseReady());
    deferTrampoline=true;const auto removedBefore=removes;
    CHECK(InitializeGuestCofMouse(&provider)&&!GuestCofMouseReady()&&deferredTrampoline);
    CHECK(!SetGuestCofMouseFrame({3,4,6},50,90,.5F)&&removes==removedBefore);
    *deferredTrampoline=reinterpret_cast<void*>(Native);deferredTrampoline=nullptr;
    GuestCofMouseFrame();CHECK(GuestCofMouseReady());
    ShutdownGuestCofMouse();CHECK(!GuestCofMouseReady()&&removes==removedBefore+1);
    CHECK(InitializeGuestCofMouse(&provider)&&deferredTrampoline&&!GuestCofMouseReady());
    ShutdownGuestCofMouse();CHECK(!deferredTrampoline&&!GuestCofMouseReady());
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("actual mouse stage SDK/thunk checks=%u failures=%u; native device/runtime integration separate\n",checks,failures);
    return failures?1:0;
}
