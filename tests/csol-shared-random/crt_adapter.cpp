// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/client/shared_random.cpp"
#include <iostream>
#include <cstring>

namespace csol::client {metahook_api_t* api{};}
namespace {
int checks{},failures{},resolves{},firstCalls{},secondCalls{};
int mode{};int marker{};
using Random=int(__cdecl*)();
void Check(bool value,const char* name){++checks;if(!value){++failures;std::cerr<<"FAIL "<<name<<'\n';}}
int __cdecl First(){++firstCalls;return 12001;}
int __cdecl Second(){++secondCalls;return 32002;}
Random slot=First;
float __cdecl Shared(unsigned seed,float low,float high){return float(seed)+low+high;}
bool InitializeFails(){try{csol::client::InitializeSharedRandom();return false;}catch(const std::runtime_error&){return true;}}
bool CRTThrows(){try{(void)csol::client::ClientCRTRandom();return false;}catch(const std::logic_error&){return true;}}
bool SharedThrows(){try{(void)csol::client::SharedRandomFloat(1,2,3);return false;}catch(const std::logic_error&){return true;}}
}
int main()
{
    using namespace csol::client;
    static metahook_api_t sdk{};api=&sdk;
    sdk.GetClientBase=[](){return static_cast<void*>(&marker);};
    sdk.ResolveGameSymbol=[](void* base,const char* name,mh_gamesymbol_kind_t kind,void** result){
        ++resolves;Check(base==&marker,"resolver uses selected host module");
        if(!std::strcmp(name,"UTIL_SharedRandomFloat")){
            Check(kind==MH_GAMESYMBOL_KIND_FUNCTION,"shared RNG requests function kind");
            *result=mode==2?nullptr:reinterpret_cast<void*>(Shared);
            return mode==1?MH_GAMESYMBOL_SYMBOL_NOT_FOUND:MH_GAMESYMBOL_OK;
        }
        Check(!std::strcmp(name,"CSOL_ClientCRTRand")&&kind==MH_GAMESYMBOL_KIND_GLOBAL,"CRT requests identity-matched IAT global");
        *result=mode==4?nullptr:&slot;
        return mode==3?MH_GAMESYMBOL_SYMBOL_NOT_FOUND:MH_GAMESYMBOL_OK;
    };
    ShutdownSharedRandom();Check(CRTThrows()&&SharedThrows(),"calls before initialize rejected");
    InitializeSharedRandom();Check(resolves==2,"both symbols resolved exactly once");
    Check(SharedRandomFloat(5,2,3)==10,"shared function forwards arguments and return");
    Check(ClientCRTRandom()==12001&&firstCalls==1,"CRT invokes imported target");
    slot=Second;const auto before=resolves;
    Check(ClientCRTRandom()==32002&&secondCalls==1&&firstCalls==1,"each call dereferences current IAT slot");
    Check(resolves==before,"runtime RNG does not resolve symbols repeatedly");
    for(int failure=1;failure<=5;++failure){
        mode=failure;slot=failure==5?nullptr:First;
        Check(InitializeFails(),"resolver/null address/null imported pointer rejects initialize");
        Check(CRTThrows()&&SharedThrows(),"failed initialization leaves neither RNG callable");
        mode=0;slot=First;InitializeSharedRandom();
        Check(ClientCRTRandom()==12001&&SharedRandomFloat(1,2,3)==6,"success after failed reinitialize restores both entries");
    }
    ShutdownSharedRandom();Check(CRTThrows()&&SharedThrows(),"shutdown clears both imported capabilities");
    ShutdownSharedRandom();Check(CRTThrows()&&SharedThrows(),"repeat shutdown is safe");
    std::cout<<"CRT adapter: "<<checks<<" checks, "<<failures<<" failures\n";
    return failures?1:0;
}
