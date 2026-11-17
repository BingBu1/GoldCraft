// SPDX-License-Identifier: GPL-3.0-or-later
#include "validation.hpp"
#include "../../native/csol/client/resources.cpp"
void LoadFixture(model_t* model,void* bytes,void (*original)(model_t*,void*))
{csol::client::loadStudio=original;csol::client::LoadStudio(model,bytes);}

namespace csol::client {
namespace {
bool fixtureMatchedReturn{};
unsigned fixtureMode{};
#ifndef CSOL_TEST_OLD_STUDIO_SOURCE
int __cdecl FixtureRead(void*,int size,int,void*)
{return fixtureMode==4?size-1:fixtureMode==5?-1:fixtureMode==6?0:size;}
#endif
__declspec(naked) void NativeStudioCaller(model_t*,void*,int)
{
    __asm {
        push ebp
        mov ebp, esp
        sub esp, 110h
        mov eax, [ebp+16]
        mov [ebp-110h], eax
        xor eax, eax
        cmp byte ptr [fixtureMatchedReturn], 0
        je ordinary
        lea eax, returned
    ordinary:
#ifndef CSOL_TEST_OLD_STUDIO_SOURCE
        mov dword ptr [nativeStudioReturn], eax
        cmp dword ptr [fixtureMode], 3
        je nativeFixtureLoadStep
        lea eax, readReturned
        mov dword ptr [nativeStudioReadReturn], eax
        push 0
        push 1
        push dword ptr [ebp+16]
        push dword ptr [ebp+12]
        call StudioReadEntry
    readReturned:
        add esp, 16
    nativeFixtureLoadStep:
#endif
        push dword ptr [ebp+12]
        push dword ptr [ebp+8]
#ifdef CSOL_TEST_OLD_STUDIO_SOURCE
        call LoadStudio
#else
        call LoadStudioEntry
#endif
    returned:
        add esp, 8
        mov esp, ebp
        pop ebp
        ret
    }
}
}
}
__declspec(noinline) void NativeLoadFixture(model_t* model,void* bytes,int size,unsigned mode,void (*original)(model_t*,void*))
{
    using namespace csol::client;
    loadStudio=original;
    const auto savedMatch=fixtureMatchedReturn;
    const auto savedMode=fixtureMode;fixtureMode=mode;
    fixtureMatchedReturn=mode!=1;
#ifndef CSOL_TEST_OLD_STUDIO_SOURCE
    const auto* savedReturn=nativeStudioReturn;
    const auto savedThread=nativeStudioThread;
    const auto* savedReadReturn=nativeStudioReadReturn;const auto savedRead=studioReadReceipt;
    const auto savedReader=studioRead;studioRead=FixtureRead;
    nativeStudioThread=GetCurrentThreadId()+(mode==2);
    const auto restore=[&]{fixtureMode=savedMode;fixtureMatchedReturn=savedMatch;nativeStudioReturn=savedReturn;
        nativeStudioThread=savedThread;nativeStudioReadReturn=savedReadReturn;studioReadReceipt=savedRead;studioRead=savedReader;};
#else
    const auto restore=[&]{fixtureMode=savedMode;fixtureMatchedReturn=savedMatch;};
#endif
    try{NativeStudioCaller(model,bytes,size);}catch(...){restore();throw;}
    restore();
}
