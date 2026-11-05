// SPDX-License-Identifier: GPL-3.0-or-later
// Actual SDK adapter invokes original, identity-checked x86 machine code.
#include "../../native/csol/client/runtime.hpp"
#include "../../native/csol/client/shared_random.hpp"
#include "native_image.hpp"
#include <bit>
#include <cstdio>
#include <limits>

namespace reference {
#include "original_shared_spread.inc"
}
namespace fixture {
using namespace native_random_test;
#include "native_spans.inc"
}
namespace csol::client {metahook_api_t* api{};}
namespace {
int checks{},failures{},resolves{};void* module{};void* selected{};
int __cdecl UnusedCRT(){return 16384;}
using CRT=int(__cdecl*)();CRT crtSlot=UnusedCRT;
mh_gamesymbol_status_t status=MH_GAMESYMBOL_OK;bool requestValid=true;
void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL %s\n",label);}}
using Function=float(__cdecl*)(unsigned,float,float);
std::uint32_t Bits(float value){return std::bit_cast<std::uint32_t>(value);}
float Reference(unsigned seed,float low,float high){
#pragma clang fp contract(off)
    unsigned state=reference::RandomSeeds[(seed+Bits(low)+Bits(high))&255];
    const auto next=[&](){state*=69069;state+=reference::RandomSeeds[state&255]+1;return state;};
    next();next();const auto range=static_cast<unsigned>(static_cast<std::int64_t>(high-low));
    if(!range)return low;
    const float fraction=float(next()&65535)/65536.f;
    return low+fraction*float(range);
}
__declspec(noinline) float Call(Function fn,unsigned seed,float low,float high){
    std::uintptr_t before{},after{};
    __asm mov before,esp
    const float value=fn(seed,low,high);
    __asm mov after,esp
    Check(before==after,"original cdecl leaves argument cleanup to caller");
    return value;
}
bool ThrowsLogic(){try{(void)csol::client::SharedSpread(3);return false;}catch(const std::logic_error&){return true;}}
bool InitializeFails(){try{csol::client::InitializeSharedRandom();return false;}catch(const std::runtime_error&){return true;}}
}

int main(int argc,char** argv){
    try{
        if(argc!=2)throw std::runtime_error("expected verified native PE path");
        fixture::Image image(argv[1],fixture::spans);module=image.Base();
        auto native=image.Address<Function>(fixture::functionRva);selected=reinterpret_cast<void*>(native);
        auto* capability=image.Address<unsigned*>(fixture::capabilityRva);
        Check(*capability<6,"file CPU capability default uses supported SSE conversion path");
        static metahook_api_t sdk{};csol::client::api=&sdk;
        sdk.GetClientBase=[](){return module;};
        sdk.ResolveGameSymbol=[](void* base,const char* name,mh_gamesymbol_kind_t kind,void** address){
            ++resolves;
            if(!std::strcmp(name,"CSOL_ClientCRTRand")){
                requestValid=requestValid&&base==module&&kind==MH_GAMESYMBOL_KIND_GLOBAL;
                *address=&crtSlot;return MH_GAMESYMBOL_OK;
            }
            requestValid=requestValid&&base==module&&!std::strcmp(name,"UTIL_SharedRandomFloat")&&kind==MH_GAMESYMBOL_KIND_FUNCTION;
            *address=selected;return status;};
        csol::client::ShutdownSharedRandom();Check(ThrowsLogic(),"use before initialize rejected");
        csol::client::InitializeSharedRandom();Check(requestValid&&resolves==2,"exact host module/function and CRT import resolved");
        for(unsigned capabilityLevel:{0u,1u,5u}){
            *capability=capabilityLevel;
            for(unsigned page:{0u,256u,65536u,0x7fffff00u,0xffffff00u})for(unsigned i=0;i<256;++i){
                const auto seed=page+i;
                const float original=Call(native,seed,-.5f,.5f),expected=reference::SharedSpread(seed);
                Check(Bits(original)==Bits(expected),"all low seeds and unsigned boundaries match old spread bits");
                Check(Bits(csol::client::SharedSpread(seed))==Bits(original),"adapter forwards exact native spread result");
            }
        }
        constexpr float ranges[][2]={{0,0},{-0.f,0.f},{3.25f,3.25f},{0,.999f},{.25f,1.25f},
            {-10,10},{0,4096},{0,16777216},{1,-1},{-3.75f,2.125f},{100000,100001},{-2147483520.f,0}};
        for(const auto& range:ranges)for(unsigned seed=0;seed<256;++seed){
            const float expected=Reference(seed,range[0],range[1]);
            const float actual=Call(native,seed,range[0],range[1]);
            if(Bits(actual)!=Bits(expected))std::printf("range mismatch seed=%u low=%a high=%a native=%08x oracle=%08x\n",seed,range[0],range[1],Bits(actual),Bits(expected));
            Check(Bits(actual)==Bits(expected),"native unsigned range and float-bit seeding match independent oracle");
            Check(Bits(csol::client::SharedRandomFloat(seed,range[0],range[1]))==Bits(expected),"adapter preserves non-spread arguments and result");
        }
        const int before=resolves;Check(Bits(csol::client::SharedSpread(123))==Bits(reference::SharedSpread(123)),"native state reseeded on each call");
        Check(resolves==before,"prediction calls do not repeat resolver or allocate a replacement table");
        for(const auto error:{MH_GAMESYMBOL_SYMBOL_NOT_FOUND,MH_GAMESYMBOL_KIND_MISMATCH}){
            status=error;Check(InitializeFails(),"failed resolver rejects initialization");Check(ThrowsLogic(),"failed reinitialize clears old callable pointer");
            status=MH_GAMESYMBOL_OK;csol::client::InitializeSharedRandom();
            Check(Bits(csol::client::SharedSpread(5))==Bits(reference::SharedSpread(5)),"successful reinitialize restores native entry");
        }
        selected=nullptr;Check(InitializeFails()&&ThrowsLogic(),"success status with null pointer never retains old function");
        selected=reinterpret_cast<void*>(native);csol::client::InitializeSharedRandom();
        csol::client::ShutdownSharedRandom();Check(ThrowsLogic(),"shutdown removes callable host pointer");
        csol::client::ShutdownSharedRandom();Check(ThrowsLogic(),"shutdown is repeatable");
        std::printf("native shared-random adapter: %d checks, %d failures; no DllMain/imports/UI\n",checks,failures);
        return failures?1:0;
    }catch(const std::exception& error){std::printf("ERROR %s\n",error.what());return 2;}
}
