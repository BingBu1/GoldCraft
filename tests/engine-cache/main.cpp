// SPDX-License-Identifier: GPL-3.0-or-later
// Execute the unchanged production allocator with real engine types; console,
// command registration and Studio ownership bookkeeping are explicit stubs.
#include "precompiled.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include "../../external/ReHLDS/rehlds/engine/zone.cpp"

namespace
{
int checks{}, resets{}, allocations{};
std::string diagnostic;
struct Fatal : std::runtime_error { using std::runtime_error::runtime_error; };
struct Arena
{
    void* data;
    int size;
    explicit Arena(int bytes) : data(_aligned_malloc(bytes, 16)), size(bytes)
    { if (!data) throw std::bad_alloc(); std::memset(data, 0, bytes); }
    ~Arena() { _aligned_free(data); }
};
void Check(bool success, const char* label)
{
    ++checks;
    if (!success) throw std::runtime_error(std::string("check failed: ") + label);
}
template<class Callback> void ExpectFatal(Callback callback, const char* label)
{
    bool caught{};
    try { callback(); } catch (const Fatal&) { caught = true; }
    Check(caught, label);
}
bool Inside(void* pointer, const Arena& arena)
{
    const auto address = reinterpret_cast<uintptr_t>(pointer);
    const auto begin = reinterpret_cast<uintptr_t>(arena.data);
    return address >= begin && address < begin + arena.size;
}
int Used()
{
    int bytes{};
    for (auto* entry = cache_head.next; entry != &cache_head; entry = entry->next) bytes += entry->size;
    return bytes;
}
void* Allocate(cache_user_t& owner, int bytes, char pattern = 'M')
{
    char name[] = "model.mdl";
    void* memory = Cache_Alloc(&owner, bytes, name);
    Check(memory == owner.data, "owner published");
    std::memset(memory, pattern, bytes);
    return memory;
}
void Separate()
{
    Arena hunk(8 * 1024 * 1024), cache(1024 * 1024);
    Memory_Init(hunk.data, hunk.size, cache.data, cache.size);
    Check(cache_base == cache.data && cache_size == cache.size, "separate arena installed");
    Check(mainzone && Inside(mainzone, hunk), "zone remains in hunk");
    cache_user_t first{};
    auto* initial = Allocate(first, 200000);
    const auto evictions = goldcraftCacheEvictions;
    const int low = Hunk_LowMark(), high = Hunk_HighMark();
    Check(Inside(first.data, cache), "model in cache arena");
    Check(Inside(Hunk_AllocName(2 * 1024 * 1024, "map"), hunk), "map low allocation");
    Check(Inside(Hunk_HighAllocName(2 * 1024 * 1024, "load"), hunk), "map high allocation");
    Check(first.data == initial && goldcraftCacheEvictions == evictions, "hunk growth preserves cache address and LRU");
    Check(static_cast<char*>(first.data)[0] == 'M' && static_cast<char*>(first.data)[199999] == 'M', "cache payload preserved");
    Hunk_FreeToLowMark(low); Hunk_FreeToHighMark(high);
    Check(first.data == initial, "free marks preserve cache");
    Check(Inside(Hunk_TempAlloc(1024), hunk) && hunk_tempactive, "temp stays in hunk");
    Hunk_HighMark(); Check(!hunk_tempactive && first.data == initial, "temp release preserves cache");
    Cache_Force_Flush(); Check(!first.data && Used() == 0, "force flush owner cleared");
    cache_user_t a{}, b{}, c{}, d{};
    Allocate(a, 250000, 'a'); Allocate(b, 250000, 'b'); Allocate(c, 250000, 'c');
    Check(Cache_Check(&a) == a.data, "touch retains owner");
    Allocate(d, 400000, 'd');
    Check(a.data && !b.data && !c.data && d.data, "LRU evicts least recent contiguous pair");
    Check(static_cast<char*>(a.data)[0] == 'a', "LRU survivor payload");
    Check(goldcraftCacheEvictions == evictions + 2, "LRU eviction count");
    g_pcl.maxclients = 2; sv_cheats.value = 0;
    Cache_Flush(); Check(a.data && d.data, "multiplayer flush policy unchanged");
    sv_cheats.value = 1; Cache_Flush(); Check(!a.data && !d.data, "allowed flush clears both");
    sv_cheats.value = 0;
    cache_user_t boundary{};
    const int full = (cache.size & ~15) - int(sizeof(cache_system_t));
    Allocate(boundary, full); Check(Used() == cache.size, "exact cache capacity");
    ExpectFatal([&] { cache_user_t bad{}; Allocate(bad, full + 1); }, "oversized cache allocation rejected");
    Check(boundary.data != nullptr, "oversized request retains existing cache");
    ExpectFatal([&] { cache_user_t bad{}; Allocate(bad, std::numeric_limits<int>::max()); }, "size overflow rejected");
    ExpectFatal([&] { cache_user_t bad{}; Allocate(bad, 0); }, "zero allocation rejected");
    ExpectFatal([&] { Allocate(boundary, 10); }, "duplicate owner rejected");
    auto* hunkOwner = static_cast<cache_user_t*>(Hunk_AllocName(sizeof(cache_user_t), "owner"));
    Cache_Force_Flush(); Allocate(*hunkOwner, 128);
    Memory_Shutdown();
    Check(!hunkOwner->data, "shutdown clears hunk-resident cache owner before releasing hunk");
    Check(!hunk_base && !cache_base && !mainzone && !memory_initialized, "shutdown resets arena state");
    const int oldResets = resets; Memory_Shutdown(); Check(resets == oldResets, "shutdown idempotent");
}
void Reinitialize()
{
    Arena hunk(4 * 1024 * 1024), cache(4096), otherHunk(4 * 1024 * 1024), otherCache(4096);
    Memory_Init(hunk.data, hunk.size, cache.data, cache.size);
    cache_user_t owner{}; Allocate(owner, 128);
    ExpectFatal([&] { Memory_Init(hunk.data, hunk.size, hunk.data, 4096); }, "overlap rejected before teardown");
    Check(owner.data && hunk_base == hunk.data, "invalid replacement leaves old arena");
    ExpectFatal([&] { Memory_Init(nullptr, hunk.size); }, "null hunk rejected");
    ExpectFatal([&] { Memory_Init(hunk.data, hunk.size, nullptr, 12); }, "null cache with size rejected");
    ExpectFatal([&] { Memory_Init(hunk.data, hunk.size, cache.data, int(sizeof(cache_system_t))); }, "header-only cache rejected");
    ExpectFatal([&] { Memory_Init(static_cast<byte*>(hunk.data) + 1, hunk.size - 1); }, "unaligned hunk rejected");
    ExpectFatal([&] { Memory_Init(hunk.data, hunk.size, static_cast<byte*>(cache.data) + 1, 1000); }, "unaligned cache rejected");
    ExpectFatal([&] { Memory_Init(reinterpret_cast<void*>(uintptr_t(-16)), 32); }, "hunk address overflow rejected");
    ExpectFatal([&] { Memory_Init(hunk.data, hunk.size, reinterpret_cast<void*>(uintptr_t(-16)), 4096); }, "cache address overflow rejected");
    Memory_Init(otherHunk.data, otherHunk.size, otherCache.data, otherCache.size);
    Check(!owner.data && hunk_base == otherHunk.data && cache_base == otherCache.data, "reinitialize clears old owner");
    Check(goldcraftCacheAllocations == 0 && goldcraftCacheEvictions == 0, "new lifetime resets diagnostics");
    Allocate(owner, 128); Memory_Init(otherHunk.data, otherHunk.size);
    Check(!owner.data && !cache_base && cache_size == 0, "reinitialize to legacy shared arena");
    Allocate(owner, 128); Check(Inside(owner.data, otherHunk), "legacy cache shares hunk");
    Memory_Shutdown();
}
void Shared()
{
    Arena hunk(4 * 1024 * 1024);
    Memory_Init(hunk.data, hunk.size);
    cache_user_t owner{};
    auto* previous = Allocate(owner, 256 * 1024, 's');
    Hunk_AllocName(256 * 1024, "map growth");
    Check(owner.data && owner.data != previous && static_cast<char*>(owner.data)[123] == 's', "shared cache moves around low hunk");
    Cache_Force_Flush();
    const int room = hunk_size - hunk_low_used - hunk_high_used;
    Allocate(owner, room - int(sizeof(cache_system_t)) - 32);
    Hunk_HighAllocName(1024, "high eviction");
    Check(!owner.data, "shared cache evicts when hunk leaves insufficient room");
    Memory_Shutdown();
}
}

client_state_t g_pcl{};
cvar_t sv_cheats{};
int com_argc = 1;
char* fixtureArguments[] = {const_cast<char*>("cache-fixture"), nullptr};
char** com_argv = fixtureArguments;
void NORETURN Sys_Error(const char* format, ...)
{
    char text[1024]; va_list args; va_start(args, format); std::vsnprintf(text, sizeof(text), format, args); va_end(args);
    throw Fatal(text);
}
void Con_Printf(const char* format, ...)
{
    char text[1024]; va_list args; va_start(args, format); std::vsnprintf(text, sizeof(text), format, args); va_end(args);
    diagnostic += text;
}
void Con_DPrintf(const char*, ...) {}
void Cmd_AddCommand(const char*, xcommand_t) {}
int COM_CheckParm(const char*) { return 0; }
extern "C" void* A_memset(void* target, int value, size_t bytes) { return std::memset(target, value, bytes); }
extern "C" void* A_memcpy(void* target, const void* source, size_t bytes) { return std::memcpy(target, source, bytes); }
void GoldCraftStudioAllocating(cache_user_t*, int) { ++allocations; }
void GoldCraftStudioMemoryReset() { ++resets; }

int main()
{
    try
    {
        Separate(); Reinitialize(); Shared();
        std::printf("engine-cache checks=%d passed; actual zone.cpp; SDK boundary stubs; no live FPS evidence\n", checks);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::printf("engine-cache failure after %d checks: %s\n", checks, error.what());
        return 1;
    }
}
