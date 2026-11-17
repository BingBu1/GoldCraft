// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/client/weapons/sniper_scope_host.hpp"
#include "../../native/csol/client/weapons/weapons.hpp"
#include <bit>
#include <cstring>
#include <iostream>
#include <stdexcept>
namespace {
int checks{},failures{},calls{},hooks{},marker{};bool owned{},available=true;
void* owner{};std::uint32_t timeBits{};void* replacement{};
void Check(bool b,const char* text){++checks;if(!b){++failures;std::cerr<<text<<'\n';}}
int __fastcall Original(void* self,void*,float time){++calls;owner=self;timeBits=std::bit_cast<std::uint32_t>(time);return 73;}
}
namespace csol::client {
metahook_api_t* api{};
bool OwnsNativeSniperScope(){return owned;}
hook_t* Hook(void* address,void* replace,void** original)
{
    Check(address==reinterpret_cast<void*>(Original),"matched original function");
    *original=address;replacement=replace;++hooks;return nullptr;
}
}
int main()
{
    static metahook_api_t sdk{};csol::client::api=&sdk;
    sdk.GetClientBase=[]()->void*{return &marker;};
    sdk.ResolveGameSymbol=[](void* base,const char* name,mh_gamesymbol_kind_t kind,void** value){
        Check(base==&marker,"current native module");Check(!std::strcmp(name,"GoldCraftSniperScope_Draw"),"exact symbol");
        Check(kind==MH_GAMESYMBOL_KIND_FUNCTION,"function kind");*value=available?reinterpret_cast<void*>(Original):nullptr;return MH_GAMESYMBOL_OK;
    };
    available=false;bool threw=false;try{csol::client::InstallSniperScopeHostGuard();}catch(const std::runtime_error&){threw=true;}
    Check(threw&&hooks==0,"missing function cannot install a hook");available=true;csol::client::InstallSniperScopeHostGuard();
    auto draw=reinterpret_cast<int (__fastcall *)(void*,void*,float)>(replacement);
    for(auto bits:{0u,0x80000000u,0x3f800000u,0x42b40000u,0x7fc01234u,0x7f800000u}){
        float time=std::bit_cast<float>(bits);owned=false;int before=calls;
        Check(draw(&marker,nullptr,time)==73,"native scope result preserved");Check(calls==before+1&&owner==&marker&&timeBits==bits,"native this/time ABI preserved");
        owned=true;before=calls;Check(draw(&marker,nullptr,time)==1,"extended scope suppresses only duplicate layer");Check(calls==before,"extended scope calls no native draw");
        owned=false;Check(draw(&marker,nullptr,time)==73&&calls==before+1,"release restores native scope immediately");
    }
    std::cout<<"native scope guard "<<checks<<" checks "<<failures<<" failures\n";return failures?1:0;
}
