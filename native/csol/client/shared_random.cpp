// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "shared_random.hpp"
#include <stdexcept>

namespace csol::client {
namespace {
using NativeSharedRandomFloat = float (__cdecl *)(unsigned, float, float);
NativeSharedRandomFloat nativeSharedRandomFloat{};
using NativeCRTRandom = int (__cdecl *)();
NativeCRTRandom* nativeCRTRandom{};
}

void InitializeSharedRandom()
{
    nativeSharedRandomFloat = nullptr;
    nativeCRTRandom = nullptr;
    void* address{};
    if (api->ResolveGameSymbol(api->GetClientBase(), "UTIL_SharedRandomFloat",
            MH_GAMESYMBOL_KIND_FUNCTION, &address) != MH_GAMESYMBOL_OK || !address)
        throw std::runtime_error("unavailable matched client symbol: UTIL_SharedRandomFloat");
    const auto shared = reinterpret_cast<NativeSharedRandomFloat>(address);
    address = nullptr;
    if (api->ResolveGameSymbol(api->GetClientBase(), "CSOL_ClientCRTRand",
            MH_GAMESYMBOL_KIND_GLOBAL, &address) != MH_GAMESYMBOL_OK || !address ||
        !*static_cast<NativeCRTRandom*>(address))
        throw std::runtime_error("unavailable matched client import: rand");
    nativeSharedRandomFloat = shared;
    nativeCRTRandom = static_cast<NativeCRTRandom*>(address);
}

void ShutdownSharedRandom() { nativeSharedRandomFloat = nullptr; nativeCRTRandom = nullptr; }

int ClientCRTRandom()
{
    if (!nativeCRTRandom)
        throw std::logic_error("client CRT random is not initialized");
    return (*nativeCRTRandom)();
}

float SharedRandomFloat(unsigned seed, float low, float high)
{
    if (!nativeSharedRandomFloat)
        throw std::logic_error("client shared random is not initialized");
    return nativeSharedRandomFloat(seed, low, high);
}
}
