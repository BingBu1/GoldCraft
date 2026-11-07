// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "model_crc.hpp"

namespace csol::client
{
namespace
{
HostCrcFunction originalProcessBuffer{};
void __cdecl ProcessBuffer(std::uint32_t* state, void* bytes, int length)
{
    ProcessHostCrc(state, bytes, length, originalProcessBuffer);
}
}

void InstallModelCrc()
{
#if CSOL_OPTIMIZE_HOST_CRC
    // The catalog binds this cdecl ABI to the verified host SHA/CRC64. Keep
    // native Init/Final and every model/network consistency caller unchanged.
    Hook(Resolve("CSOL_CRC32_ProcessBuffer", MH_GAMESYMBOL_KIND_FUNCTION),
        reinterpret_cast<void*>(ProcessBuffer), reinterpret_cast<void**>(&originalProcessBuffer));
#endif
}
}
