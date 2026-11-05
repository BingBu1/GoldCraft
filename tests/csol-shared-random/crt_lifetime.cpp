// SPDX-License-Identifier: GPL-3.0-or-later
// Additional lifetime cases only; the prior 63-case adapter suite is retained.
#define main PreviousAdapterChecks
#include "crt_adapter.cpp"
#undef main

int main()
{
    using namespace csol::client;
    static metahook_api_t sdk{};api=&sdk;
    static int secondModule{};
    static Random replacementSlot=Second;
    static bool wrongKind{};
    sdk.GetClientBase=[](){return static_cast<void*>(&marker);};
    sdk.ResolveGameSymbol=[](void* base,const char* name,mh_gamesymbol_kind_t kind,void** result){
        Check(base==&marker,"first initialization binds the first module");
        if(!std::strcmp(name,"UTIL_SharedRandomFloat")){
            Check(kind==MH_GAMESYMBOL_KIND_FUNCTION,"first shared function kind");
            *result=reinterpret_cast<void*>(Shared);return MH_GAMESYMBOL_OK;
        }
        Check(!std::strcmp(name,"CSOL_ClientCRTRand")&&kind==MH_GAMESYMBOL_KIND_GLOBAL,"first CRT global kind");
        // A failed resolver may leave an invalid output. Its failure result
        // must be checked before the adapter dereferences that output.
        *result=wrongKind?reinterpret_cast<void*>(1):&slot;
        return wrongKind?MH_GAMESYMBOL_KIND_MISMATCH:MH_GAMESYMBOL_OK;
    };
    InitializeSharedRandom();Check(ClientCRTRandom()==12001,"initial import is callable");
    wrongKind=true;Check(InitializeFails(),"kind mismatch rejects poisoned output without dereference");
    Check(CRTThrows()&&SharedThrows(),"kind mismatch clears both previous bindings");
    wrongKind=false;
    sdk.GetClientBase=[](){return static_cast<void*>(&secondModule);};
    sdk.ResolveGameSymbol=[](void* base,const char* name,mh_gamesymbol_kind_t kind,void** result){
        Check(base==&secondModule,"module reload requests current module base");
        if(!std::strcmp(name,"UTIL_SharedRandomFloat")){
            Check(kind==MH_GAMESYMBOL_KIND_FUNCTION,"new shared function kind");
            *result=reinterpret_cast<void*>(Shared);return MH_GAMESYMBOL_OK;
        }
        Check(!std::strcmp(name,"CSOL_ClientCRTRand")&&kind==MH_GAMESYMBOL_KIND_GLOBAL,"new CRT global kind");
        *result=&replacementSlot;return MH_GAMESYMBOL_OK;
    };
    slot=nullptr;
    InitializeSharedRandom();
    Check(ClientCRTRandom()==32002&&firstCalls==1&&secondCalls==1,"module reload discards old import slot");
    ShutdownSharedRandom();replacementSlot=nullptr;
    Check(CRTThrows()&&SharedThrows(),"shutdown does not dereference invalidated old slots");
    std::cout<<"CRT lifetime: "<<checks<<" checks, "<<failures<<" failures\n";
    return failures?1:0;
}
