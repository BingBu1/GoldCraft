// SPDX-License-Identifier: GPL-3.0-or-later
// Run this weapon's full predictor against the verified native random entry.
#define main OriginalGunChecks
#include "client.cpp"
#undef main
#include "../csol-shared-random/native_image.hpp"
#include <filesystem>
namespace fixture {
#include "native_spans.inc"
}
namespace csol::client {metahook_api_t* api{};}
namespace {void* nativeModule{};void* nativeFunction{};int resolverCalls{};
int __cdecl UnusedCRT(){return 16384;}using CRT=int(__cdecl*)();CRT crtSlot=UnusedCRT;}
int wmain(int argc,wchar_t** argv){
    try{
        if(argc!=2)throw std::runtime_error("expected verified native PE path");
        const auto binary=std::filesystem::path(argv[1]).string();
        native_random_test::Image image(binary.c_str(),fixture::spans);
        nativeModule=image.Base();nativeFunction=image.Address<void*>(fixture::functionRva);
        static metahook_api_t sdk{};csol::client::api=&sdk;
        sdk.GetClientBase=[](){return nativeModule;};
        sdk.ResolveGameSymbol=[](void* module,const char* name,mh_gamesymbol_kind_t kind,void** out){
            ++resolverCalls;
            if(!std::strcmp(name,"CSOL_ClientCRTRand")){
                Check(module==nativeModule&&kind==MH_GAMESYMBOL_KIND_GLOBAL,"host CRT import uses global kind");
                *out=&crtSlot;return MH_GAMESYMBOL_OK;
            }
            Check(module==nativeModule&&!std::strcmp(name,"UTIL_SharedRandomFloat")&&kind==MH_GAMESYMBOL_KIND_FUNCTION,
                "host resolver receives exact module/name/kind");
            *out=nativeFunction;return MH_GAMESYMBOL_OK;};
        csol::client::InitializeSharedRandom();
        const int result=OriginalGunChecks();
        Check(resolverCalls==2,"weapon prediction reuses initialized native RNG entries");
        csol::client::ShutdownSharedRandom();
        std::printf("native shared-RNG weapon regression: %u checks, %u failures\n",unsigned(checks),unsigned(failures));
        return result||failures?1:0;
    }catch(const std::exception& error){std::printf("ERROR %s\n",error.what());return 2;}
}
