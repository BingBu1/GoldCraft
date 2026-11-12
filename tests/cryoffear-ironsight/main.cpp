// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/client/runtime.hpp"
#include "../../native/csol/client/weapons/guest_cof_ironsight.hpp"
#include <cstdio>
#include <cstring>
#include <thread>
#include <cstdint>

namespace csol::client { metahook_api_t* api{}; }
namespace
{
using namespace csol::client;
unsigned checks{}, failures{}, hooks{}, removes{};
bool resolve{true};
unsigned char* site{};
void* replacement{};
std::uint32_t inputBits{}, outputBits{}, beforeFlags{}, afterFlags{};
void Check(bool value,int line) { ++checks; if(!value){++failures;std::printf("FAIL %d\n",line);} }
#define CHECK(x) Check((x),__LINE__)
void* Client() { return reinterpret_cast<void*>(0x10000000); }
mh_gamesymbol_status_t Resolve(void* module,const char* name,mh_gamesymbol_kind_t kind,void** out)
{
    CHECK(module==Client());
    CHECK(!std::strcmp(name,"GoldCraftCof_ViewmodelBobLoad"));
    CHECK(kind==MH_GAMESYMBOL_KIND_PATCH);
    if(!resolve) return MH_GAMESYMBOL_SYMBOL_NOT_FOUND;
    *out=site; return MH_GAMESYMBOL_OK;
}
__declspec(naked) void Execute()
{
    __asm {
        pushfd
        pushad
        sub esp, 44
        mov eax, dword ptr [inputBits]
        mov dword ptr [esp+40], eax
        pushfd
        pop eax
        mov dword ptr [beforeFlags], eax
        call dword ptr [replacement]
        pushfd
        pop eax
        mov dword ptr [afterFlags], eax
        movss dword ptr [outputBits], xmm6
        add esp, 44
        popad
        popfd
        ret
    }
}
void Sample(std::uint32_t expected)
{
    Execute(); CHECK(outputBits==expected); CHECK(beforeFlags==afterFlags);
}
}
namespace csol::client
{
hook_t* Hook(void* address,void* next,void** original)
{
    CHECK(address==site); ++hooks; replacement=next; *original=site;
    return reinterpret_cast<hook_t*>(1);
}
void RemoveHook(hook_t* value){CHECK(value==reinterpret_cast<hook_t*>(1));++removes;}
}
int main()
{
    using namespace csol::client;
    CHECK(!InitializeGuestCofIronsight());
    metahook_api_t functions{};
    functions.GetClientBase=Client;
    functions.ResolveGameSymbol=Resolve;
    api=&functions;
    site=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    CHECK(site);
    const unsigned char original[]{0xf3,0x0f,0x10,0x74,0x24,0x2c,0xc3};
    std::memcpy(site,original,sizeof(original));
    resolve=false; CHECK(!InitializeGuestCofIronsight()); resolve=true;
    site[0]=0; CHECK(!InitializeGuestCofIronsight()); site[0]=original[0];
    CHECK(hooks==0);
    CHECK(InitializeGuestCofIronsight()); CHECK(InitializeGuestCofIronsight()); CHECK(hooks==1);
    ref_params_t ref{}; ref.viewentity=4; ref.playernum=3;
    const std::uint32_t samples[]{0,0x80000000,0x3f800000,0xc0e00000,0x7fc12345,0x7f800000};
    for(auto value:samples)
    {
        inputBits=value;
        Sample(value); // Outside the downstream refdef scope.
        SetGuestCofIronsight(false,true);
        {GuestCofIronsightViewScope scope(ref);Sample(value);}
        SetGuestCofIronsight(true,false);
        {GuestCofIronsightViewScope scope(ref);Sample(value);}
        SetGuestCofIronsight(true,true);
        {GuestCofIronsightViewScope scope(ref);Sample(0);}
        Sample(value);
        for(auto field:{&ref.spectator,&ref.paused,&ref.intermission})
        {
            *field=1; {GuestCofIronsightViewScope scope(ref);Sample(value);} *field=0;
        }
        ref.viewentity=5; {GuestCofIronsightViewScope scope(ref);Sample(value);} ref.viewentity=4;
        {
            GuestCofIronsightViewScope outer(ref);Sample(0);
            ref.spectator=1; {GuestCofIronsightViewScope inner(ref);Sample(value);} ref.spectator=0;
            Sample(0);
        }
        {
            GuestCofIronsightViewScope scope(ref);ResetGuestCofIronsight();Sample(value);
        }
        {GuestCofIronsightViewScope scope(ref);Sample(value);}
    }
    inputBits=0x3f800000;
    SetGuestCofIronsight(true,true);
    std::thread worker([]{CHECK(!InitializeGuestCofIronsight());SetGuestCofIronsight(false,false);ResetGuestCofIronsight();ShutdownGuestCofIronsight();});
    worker.join();
    {GuestCofIronsightViewScope scope(ref);Sample(0);}
    {
        GuestCofIronsightViewScope scope(ref);SetGuestCofIronsight(false,false);Sample(inputBits);
    }
    {GuestCofIronsightViewScope scope(ref);Sample(inputBits);}
    SetGuestCofIronsight(true,true);
    {GuestCofIronsightViewScope scope(ref);ShutdownGuestCofIronsight();}
    CHECK(removes==1); ShutdownGuestCofIronsight(); CHECK(removes==1);
    CHECK(InitializeGuestCofIronsight());
    {GuestCofIronsightViewScope scope(ref);Sample(inputBits);}
    ShutdownGuestCofIronsight(); CHECK(removes==2);
    VirtualFree(site,0,MEM_RELEASE);
    std::printf("%u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
