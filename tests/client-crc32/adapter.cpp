// SPDX-License-Identifier: GPL-3.0-or-later
// Include the actual adapter with the actual MetaHook SDK, without loading a game.
#include "../../native/csol/client/model_crc.cpp"
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace
{
csol::client::HostCrcFunction replacement{};
unsigned resolved{}, hooked{}, delegated{};
bool missing{};
void __cdecl Original(std::uint32_t* value, void* data, int length)
{
    if (data != nullptr || length != -7) throw std::runtime_error("fallback arguments");
    ++delegated; *value = 0x12345678;
}
}
namespace csol::client
{
void* Resolve(const char* name, mh_gamesymbol_kind_t kind)
{
    ++resolved;
    if (std::strcmp(name, "CSOL_CRC32_ProcessBuffer") || kind != MH_GAMESYMBOL_KIND_FUNCTION)
        throw std::runtime_error("wrong symbol contract");
    if (missing) throw std::runtime_error("missing catalog");
    return reinterpret_cast<void*>(Original);
}
hook_t* Hook(void* address, void* callback, void** original)
{
    if (address != reinterpret_cast<void*>(Original) || !callback || !original)
        throw std::runtime_error("wrong hook contract");
    ++hooked; replacement = reinterpret_cast<HostCrcFunction>(callback); *original = address;
    return nullptr; // opaque manager receipt is not consumed by this installer
}
}
int main()
{
    try
    {
        csol::client::InstallModelCrc();
#if CSOL_OPTIMIZE_HOST_CRC
        if (resolved != 1 || hooked != 1 || !replacement) throw std::runtime_error("not installed");
        std::uint32_t state = 0xffffffff;
        replacement(&state, const_cast<char*>("123456789"), 9);
        if (state != 0x340bc6d9 || delegated) throw std::runtime_error("actual callback result");
        replacement(&state, nullptr, -7);
        if (state != 0x12345678 || delegated != 1) throw std::runtime_error("actual callback fallback");
        missing = true; bool rejected{};
        try { csol::client::InstallModelCrc(); } catch (const std::exception&) { rejected = true; }
        if (!rejected || hooked != 1) throw std::runtime_error("missing symbol was accepted");
        std::puts("adapter enabled: install, callback, fallback, missing-symbol rejection passed");
#else
        if (resolved || hooked || replacement || delegated) throw std::runtime_error("baseline changed host");
        std::puts("adapter baseline: native host untouched");
#endif
        return 0;
    }
    catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
