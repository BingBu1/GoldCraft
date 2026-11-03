// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "shared_random.hpp"
#include <stdexcept>

namespace csol::client {
namespace {
using NativeSharedRandomFloat = float (__cdecl *)(unsigned, float, float);
NativeSharedRandomFloat nativeSharedRandomFloat{};
}

void InitializeSharedRandom()
{
    nativeSharedRandomFloat = nullptr;
    void* address{};
    if (api->ResolveGameSymbol(api->GetClientBase(), "UTIL_SharedRandomFloat",
            MH_GAMESYMBOL_KIND_FUNCTION, &address) != MH_GAMESYMBOL_OK || !address)
        throw std::runtime_error("unavailable matched client symbol: UTIL_SharedRandomFloat");
    nativeSharedRandomFloat = reinterpret_cast<NativeSharedRandomFloat>(address);
}

void ShutdownSharedRandom() { nativeSharedRandomFloat = nullptr; }

float SharedRandomFloat(unsigned seed, float low, float high)
{
    if (!nativeSharedRandomFloat)
        throw std::logic_error("client shared random is not initialized");
    return nativeSharedRandomFloat(seed, low, high);
}
}
