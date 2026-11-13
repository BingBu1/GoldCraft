// SPDX-License-Identifier: GPL-3.0-or-later
// Real AMXX_Attach and registered DLL natives; no copied native implementation.
#include <Windows.h>
#include <amxxmodule.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>

namespace
{
constexpr int AdminCfg = 1 << 7;
int checks{}, failures{}, ingame{1}, flags{AdminCfg}, alive{}, ingameCalls{}, flagCalls{}, aliveCalls{}, addrCalls{}, stringCalls{}, conversions{};
std::map<std::string, AMX_NATIVE> natives;
std::array<cell, 8192> memory;
AMX vm{};
void Check(bool ok, const char* text)
{ ++checks; if (!ok) { ++failures; std::cerr << "FAIL " << text << '\n'; } }
int AddNatives(const AMX_NATIVE_INFO* list)
{ for (; list && list->name; ++list) natives[list->name] = list->func; return 1; }
int IsIngame(int id) { ++ingameCalls; Check(id >= 1 && id <= 32, "module queries valid player index only"); return ingame; }
int Flags(int id) { ++flagCalls; Check(id >= 1 && id <= 32, "module flags query uses a valid slot"); return flags; }
int IsAlive(int) { ++aliveCalls; return alive; }
cell RealToCell(REAL value) { ++conversions; return std::bit_cast<cell>(value); }
REAL CellToReal(cell value) { ++conversions; return std::bit_cast<REAL>(value); }
cell* Address(AMX* amx, cell offset)
{
    ++addrCalls;
    if (amx != &vm || offset < 0 || offset % sizeof(cell) || std::size_t(offset / sizeof(cell)) >= memory.size())
        throw std::runtime_error("unexpected AMXX address boundary");
    return memory.data() + offset / sizeof(cell);
}
int SetString(AMX* amx, cell offset, const char* source, int length)
{
    ++stringCalls;
    auto* destination = Address(amx, offset);
    if (length < 0 || length > 4096 || destination + length >= memory.data() + memory.size())
        throw std::runtime_error("unexpected AMXX string boundary");
    const auto size = std::min<std::size_t>(std::strlen(source), length);
    for (std::size_t i = 0; i < size; ++i) destination[i] = static_cast<unsigned char>(source[i]);
    destination[size] = 0; return static_cast<int>(size);
}
int Unexpected() { throw std::runtime_error("unexpected AMXX callback outside the explicit SDK boundary"); }
void* Request(const char* name)
{
    if (!std::strcmp(name, "AddNatives")) return reinterpret_cast<void*>(&AddNatives);
    if (!std::strcmp(name, "IsPlayerInGame")) return reinterpret_cast<void*>(&IsIngame);
    if (!std::strcmp(name, "GetPlayerFlags")) return reinterpret_cast<void*>(&Flags);
    if (!std::strcmp(name, "IsPlayerAlive")) return reinterpret_cast<void*>(&IsAlive);
    if (!std::strcmp(name, "GetAmxAddr")) return reinterpret_cast<void*>(&Address);
    if (!std::strcmp(name, "SetAmxString")) return reinterpret_cast<void*>(&SetString);
    if (!std::strcmp(name, "RealToCell")) return reinterpret_cast<void*>(&RealToCell);
    if (!std::strcmp(name, "CellToReal")) return reinterpret_cast<void*>(&CellToReal);
    return reinterpret_cast<void*>(&Unexpected);
}
template<class T> T Function(HMODULE module, const char* name)
{
    auto result = reinterpret_cast<T>(GetProcAddress(module, name));
    if (!result) throw std::runtime_error(std::string("missing export ") + name);
    return result;
}
std::string Text(int byteOffset)
{
    const auto* value = memory.data() + byteOffset / sizeof(cell);
    std::string output; while (*value) output += static_cast<char>(*value++); return output;
}
void RevokeFlags() { flags = 0; }
void RevokeIngame() { ingame = 0; }
void (*setUserid)(int){};
void ReuseSlot() { setUserid(72); }
}
int main(int argc, char** argv)
{
    static_assert(sizeof(void*) == 4 && sizeof(cell) == 4 && sizeof(REAL) == 4);
    if (argc != 3) return 2;
    HMODULE module{}, provider{};
    try
    {
        module = LoadLibraryA(argv[1]);
        if (!module) throw std::runtime_error("actual AMXX module LoadLibrary failed");
        const auto attach = Function<int (*)(PFN_REQ_FNPTR)>(module, "AMXX_Attach");
        const auto detach = Function<int (*)()>(module, "AMXX_Detach");
        Check(attach(Request) == AMXX_OK, "actual module attaches and registers real native addresses");
        const auto version = natives.at("csol_custom_api_version"), info = natives.at("csol_custom_info"),
            read = natives.at("csol_custom_read"), edit = natives.at("csol_custom_edit");
        cell noargs[]{0};
        cell infoArgs[]{6 * sizeof(cell), 4, 8, 12, 16, 128, 255};
        cell readArgs[]{11 * sizeof(cell), 1, 256, 63, 1024, 511, 20, 24, 28, 32, 36, 40};
        cell editArgs[]{5 * sizeof(cell), 1, 71, 17, 0, std::bit_cast<cell>(0.0f)};
        Check(version(&vm, noargs) == 0 && info(&vm, infoArgs) == 0 && read(&vm, readArgs) == 0,
            "missing MP provider keeps all read natives unavailable");
        Check(edit(&vm, editArgs) == -1, "missing provider cannot authorize a permanent edit");
        provider = LoadLibraryA(argv[2]);
        if (!provider) throw std::runtime_error("controlled MP provider LoadLibrary failed");
        const auto setAvailable = Function<void (*)(int)>(provider, "SetAvailable");
        const auto setStatusAvailable = Function<void (*)(int)>(provider, "SetStatusAvailable");
        setUserid = Function<void (*)(int)>(provider, "SetUserId");
        const auto setCallback = Function<void (*)(void (*)())>(provider, "SetUserIdCallback");
        const auto reset = Function<void (*)()>(provider, "ResetCounters");
        const auto count = Function<int (*)(int)>(provider, "Counter");
        const auto lastValue = Function<double (*)()>(provider, "LastValue");
        Check(version(&vm, noargs) == 0, "unavailable interface is not cached as success");
        setAvailable(1);
        Check(version(&vm, noargs) == 1, "exact GameplaySettings001 interface resolves when provider becomes ready");
        reset();
        Check(version(&vm, nullptr) == 0 && version(&vm, infoArgs) == 0, "version validates null and exact zero argc");
        Check(info(&vm, nullptr) == 0 && read(&vm, nullptr) == 0 && edit(&vm, nullptr) == -1,
            "all actual natives reject null argument blocks");
        for (auto native : {info, read})
            for (const cell bytes : {cell(0), cell(sizeof(cell)), cell(100 * sizeof(cell))})
            { cell wrong[]{bytes}; Check(native(&vm, wrong) == 0, "read natives reject incorrect argument byte counts before dereferencing"); }
        for (const cell bytes : {cell(0), cell(4 * sizeof(cell)), cell(6 * sizeof(cell))})
        { cell wrong[]{bytes}; Check(edit(&vm, wrong) == -1, "edit validates exact argument byte count"); }
        Check(count(0) == 0 && count(1) == 0 && count(2) == 0 && count(3) == 0,
            "malformed native arguments never enter provider methods");

        for (const cell length : {-1, 4097})
        {
            infoArgs[6] = length; Check(info(&vm, infoArgs) == 0, "info rejects invalid AMXX string length");
            readArgs[3] = length; Check(read(&vm, readArgs) == 0, "entry rejects invalid key length");
            readArgs[3] = 63; readArgs[5] = length; Check(read(&vm, readArgs) == 0, "entry rejects invalid comment length");
            readArgs[5] = 511;
        }
        Check(count(0) == 0 && count(1) == 0, "invalid output lengths are rejected before provider reads");
        infoArgs[6] = 255; memory.fill(0x5a5a5a5a);
        Check(info(&vm, infoArgs) == 1 && memory[1] == 17 && memory[2] == 3 && memory[3] == 1 && memory[4] == 1,
            "real native copies typed revision count busy and queued status");
        Check(Text(128) == "pending controlled save", "real SetAmxString writes the provider error text");
        infoArgs[6] = 0; memory[128 / 4] = 77;
        Check(info(&vm, infoArgs) == 1 && memory[128 / 4] == 0, "zero output length writes only its required terminator");
        infoArgs[6] = 4096; Check(info(&vm, infoArgs) == 1, "declared maximum output length is accepted");
        infoArgs[6] = 255; setStatusAvailable(0);
        Check(info(&vm, infoArgs) == 0 && Text(128) == "controlled store unavailable",
            "unavailable store preserves actual diagnostic without reporting readiness");
        setStatusAvailable(1);

        memory.fill(0x5a5a5a5a);
        Check(read(&vm, readArgs) == 1 && Text(256) == "midday_seconds" && Text(1024) == "持久配置受控说明",
            "real entry native writes bounded key and Chinese-capable explanation field");
        Check(memory[5] == 1 && std::bit_cast<float>(memory[6]) == 180 && std::bit_cast<float>(memory[7]) == 1 &&
            std::bit_cast<float>(memory[8]) == 86400 && memory[9] == 1 && memory[10] == 1,
            "numeric native ABI preserves integer type float value range map scope and available flag");
        readArgs[1] = 2;
        Check(read(&vm, readArgs) == 1 && memory[5] == 2 && memory[10] == 0, "unknown optional value remains unavailable rather than default success");
        readArgs[1] = -1;
        Check(read(&vm, readArgs) == 0, "provider entry refusal is retained"); readArgs[1] = 0;
        readArgs[3] = 2;
        memory[256 / 4 + 3] = 0x5a5a5a5a;
        Check(read(&vm, readArgs) == 1 && Text(256) == "ki" && memory[256 / 4 + 3] == cell(0x5a5a5a5a),
            "actual string output obeys caller maximum and does not overrun sentinel");
        readArgs[3] = 63;

        reset(); flags = 0;
        Check(edit(&vm, editArgs) == -1 && count(2) == 0 && count(3) == 0, "ordinary ingame player cannot enter persistent edit provider");
        flags = AdminCfg; ingame = 0;
        Check(edit(&vm, editArgs) == -1 && count(2) == 0 && count(3) == 0, "disconnected administrator cannot edit");
        ingame = 1; alive = 0;
        Check(edit(&vm, editArgs) == 1 && count(3) == 1 && aliveCalls == 0, "dead but ingame administrator may edit without an alive-only gate");
        Check(count(5) == 1 && count(6) == 71 && count(7) == 17 && count(8) == 0 && lastValue() == 0,
            "real native passes exact actor userid revision index and Float-to-double value");
        for (const cell actor : {0, -1, 33})
        { editArgs[1] = actor; const auto before = count(3); Check(edit(&vm, editArgs) == -1 && count(3) == before, "actor zero and invalid slots never acquire console authority"); }
        editArgs[1] = 1;
        for (const cell id : {0, -1, 72})
        { editArgs[2] = id; const auto before = count(3); Check(edit(&vm, editArgs) == -1 && count(3) == before, "zero negative and reused-slot userid cannot submit"); }
        editArgs[2] = 71; setUserid(0);
        Check(edit(&vm, editArgs) == -1, "provider engine identity zero cannot match a claimed live userid"); setUserid(71);

        for (auto callback : {RevokeFlags, RevokeIngame})
        {
            flags = AdminCfg; ingame = 1; setCallback(callback); const auto before = count(3);
            Check(edit(&vm, editArgs) == -1 && count(3) == before,
                "native rechecks permission and ingame after the engine userid callback");
        }
        setCallback(nullptr); flags = AdminCfg; ingame = 1; setUserid(71);
        setCallback(ReuseSlot); const auto beforeReuse = count(3);
        Check(edit(&vm, editArgs) == -4 && count(3) == beforeReuse + 1,
            "provider final identity guard rejects slot reuse after captured userid lookup");
        setCallback(nullptr); setUserid(71);
        editArgs[3] = 16;
        Check(edit(&vm, editArgs) == -2, "actual native preserves provider stale-revision status"); editArgs[3] = 17;
        editArgs[4] = 999;
        Check(edit(&vm, editArgs) == -1, "actual native preserves provider invalid-field status"); editArgs[4] = 0;
        for (float number : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()})
        {
            editArgs[5] = std::bit_cast<cell>(number);
            Check(edit(&vm, editArgs) == -1 && !std::isfinite(lastValue()),
                "nonfinite Float reaches the authoritative provider and its explicit rejection is retained");
        }
        editArgs[5] = std::bit_cast<cell>(1.125f);
        Check(edit(&vm, editArgs) == 1 && lastValue() == 1.125, "finite Float is converted exactly to double for provider validation");

        Check(detach() == AMXX_OK, "actual module detach succeeds");
        setAvailable(0);
        Check(attach(Request) == AMXX_OK && version(&vm, noargs) == 0,
            "detach clears cached settings provider before reattach");
        Check(info(&vm, infoArgs) == 0 && read(&vm, readArgs) == 0 && edit(&vm, editArgs) == -1,
            "reattached module cannot use unavailable cached provider");
        Check(detach() == AMXX_OK, "second detach succeeds");
        Check(addrCalls > 0 && stringCalls > 0 && ingameCalls > 0 && flagCalls > 0 && conversions > 0,
            "actual DLL consumed the matched explicit AMXX callback boundaries");
    }
    catch (const std::exception& error) { ++failures; std::cerr << "EXCEPTION " << error.what() << '\n'; }
    if (module) FreeLibrary(module);
    if (provider) FreeLibrary(provider);
    std::cout << "checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
