// SPDX-License-Identifier: GPL-3.0-or-later
#include "sniper_scope_host.hpp"
#include "weapons.hpp"
#include <stdexcept>

namespace csol::client {
namespace {
using NativeDraw = int (__thiscall *)(void*,float);
NativeDraw original{};
int __fastcall Draw(void* self,void*,float time)
{
    // CSO has one scope owner (02AB83A0). The native CS scope ignores weapon
    // identity below FOV45, so do not draw it behind an extended scope.
    if(OwnsNativeSniperScope())return 1;
    return original(self,time);
}
}
void InstallSniperScopeHostGuard()
{
    void* address{};
    if(api->ResolveGameSymbol(api->GetClientBase(),"GoldCraftSniperScope_Draw",
        MH_GAMESYMBOL_KIND_FUNCTION,&address)!=MH_GAMESYMBOL_OK || !address)
        throw std::runtime_error("unavailable matched client symbol: GoldCraftSniperScope_Draw");
    Hook(address,reinterpret_cast<void*>(Draw),reinterpret_cast<void**>(&original));
}
}
