// SPDX-License-Identifier: GPL-3.0-or-later
#define NOMINMAX
#include <Windows.h>
#include "../../native/csol/client/model_crc.hpp"
#include <array>
#include <chrono>
#include <climits>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

using Bytes = std::vector<unsigned char>;
using Clock = std::chrono::steady_clock;
using Function = csol::client::HostCrcFunction;
static unsigned checks{};
static void Require(bool value, const char* why) { ++checks; if (!value) throw std::runtime_error(why); }
static Bytes Read(const char* path)
{
    std::ifstream file(std::filesystem::u8path(path), std::ios::binary | std::ios::ate);
    if (!file || file.tellg() < 0 || file.tellg() > 128 * 1024 * 1024) throw std::runtime_error("invalid input file");
    Bytes bytes(static_cast<std::size_t>(file.tellg())); file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) throw std::runtime_error("short read");
    return bytes;
}

struct HostCode
{
    unsigned char* image{};
    Function process{};
    explicit HostCode(const char* path)
    {
        static_assert(sizeof(void*) == 4);
        const auto bytes = Read(path);
        Require(bytes.size() >= 28, "short fixture header");
        auto word = [&](std::size_t offset) { std::uint32_t value{}; std::memcpy(&value, bytes.data() + offset, 4); return value; };
        Require(word(0) == 0x52434347 && word(4) == 1, "fixture format");
        const auto code = word(8), codeSize = word(12), table = word(16), tableSize = word(20), count = word(24);
        Require(code == 0x1bc430 && codeSize == 0x20d && table == 0x2ba290 && tableSize == 1024 && count <= 256,
                "fixture range identity");
        const auto start = 28 + count * 4;
        Require(bytes.size() == start + codeSize + tableSize, "fixture length");
        image = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x2bb000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (!image) throw std::runtime_error("VirtualAlloc");
        std::memcpy(image + code, bytes.data() + start, codeSize);
        std::memcpy(image + table, bytes.data() + start + codeSize, tableSize);
        const auto delta = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(image) - 0x10000000u);
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const auto rva = word(28 + i * 4);
            Require(rva >= code && rva + 4 <= code + codeSize, "relocation outside code");
            std::uint32_t value{}; std::memcpy(&value, image + rva, 4);
            Require((value >= 0x10000000u + code && value < 0x10000000u + code + codeSize) ||
                    (value >= 0x10000000u + table && value < 0x10000000u + table + tableSize), "external original dependency");
            value += delta; std::memcpy(image + rva, &value, 4);
        }
        DWORD previous{};
        Require(VirtualProtect(image + 0x1bc000, 4096, PAGE_EXECUTE_READ, &previous) != 0, "code protection");
        Require(VirtualProtect(image + 0x2ba000, 4096, PAGE_READONLY, &previous) != 0, "table protection");
        FlushInstructionCache(GetCurrentProcess(), image + code, codeSize);
        process = reinterpret_cast<Function>(image + 0x1bc450);
    }
    ~HostCode() { if (image) VirtualFree(image, 0, MEM_RELEASE); }
};

static std::uint32_t Original(Function original, std::uint32_t seed, void* bytes, int length)
{ original(&seed, bytes, length); return seed; }
static std::uint32_t Current(Function original, std::uint32_t seed, void* bytes, int length)
{ csol::client::ProcessHostCrc(&seed, bytes, length, original); return seed; }
static std::uint32_t Random(std::uint32_t& seed)
{ seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }

static std::uint32_t* fallbackState{};
static void* fallbackBuffer{};
static int fallbackLength{};
static int fallbackCalls{};
static void __cdecl Fallback(std::uint32_t* state, void* buffer, int length)
{
    ++fallbackCalls;
    Require(state == fallbackState && buffer == fallbackBuffer && length == fallbackLength, "fallback arguments");
    *state = 0x89abcdef;
}

static void Functional(Function original)
{
    std::uint32_t random = 0x12344567;
    Bytes input(65536 + 32);
    for (auto& byte : input) byte = static_cast<unsigned char>(Random(random));
    for (int length = 0; length <= 1024; ++length)
        for (int alignment = 0; alignment < 16; ++alignment)
            for (int trial = 0; trial < 4; ++trial)
            {
                const auto seed = Random(random);
                Require(Original(original, seed, input.data() + alignment, length) ==
                        Current(original, seed, input.data() + alignment, length), "original instruction mismatch");
            }
    for (int length : {1025, 2047, 2048, 4095, 4096, 65535, 65536})
        for (int alignment = 0; alignment < 16; ++alignment)
        {
            const auto seed = Random(random);
            Require(Original(original, seed, input.data() + alignment, length) ==
                    Current(original, seed, input.data() + alignment, length), "large original instruction mismatch");
        }
    for (int split = 0; split <= 1024; ++split)
    {
        const auto seed = Random(random);
        const auto first = Current(original, seed, input.data(), split);
        Require(Current(original, first, input.data() + split, 1024 - split) ==
                Original(original, seed, input.data(), 1024), "split state");
    }
    Require(Current(original, 0xffffffff, const_cast<char*>("123456789"), 9) == 0x340bc6d9, "known IEEE vector");
    Require(Current(original, 0x12345678, nullptr, 0) == 0x12345678, "empty null buffer");
    for (int length : {-1, -7, INT_MIN})
    {
        std::uint32_t state = 17;
        fallbackState = &state; fallbackBuffer = nullptr; fallbackLength = length;
        csol::client::ProcessHostCrc(&state, nullptr, length, Fallback);
        Require(state == 0x89abcdef, "negative path not delegated");
    }
    Require(fallbackCalls == 3, "fallback call count");
    for (int offset : {0, 4, 24, 60})
    {
        Bytes oldData(128, 0x5a), newData = oldData;
        original(reinterpret_cast<std::uint32_t*>(oldData.data() + offset), oldData.data(), 128);
        csol::client::ProcessHostCrc(reinterpret_cast<std::uint32_t*>(newData.data() + offset), newData.data(), 128, original);
        Require(oldData == newData, "aliased state writes before input consumption");
    }
}

static void Boundaries(Function original)
{
    auto* region = static_cast<unsigned char*>(VirtualAlloc(nullptr, 12288, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!region) throw std::runtime_error("guard allocation");
    DWORD old{};
    Require(VirtualProtect(region, 4096, PAGE_NOACCESS, &old) != 0, "leading guard protection");
    Require(VirtualProtect(region + 8192, 4096, PAGE_NOACCESS, &old) != 0, "trailing guard protection");
    std::memset(region + 4096, 0xa7, 4096);
    for (int length = 0; length <= 4096; ++length)
        for (auto* pointer : {region + 4096, region + 8192 - length})
            Require(Original(original, 0x12345678, pointer, length) == Current(original, 0x12345678, pointer, length),
                    "guarded boundary mismatch");
    VirtualFree(region, 0, MEM_RELEASE);
}

int main(int argc, char** argv)
{
    try
    {
        if (argc != 3) throw std::runtime_error("expected private code fixture and model list");
        HostCode host(argv[1]); Functional(host.process); Boundaries(host.process);
        std::ifstream list(argv[2]); if (!list) throw std::runtime_error("model list");
        std::string name; unsigned models{}; double oldTotal{}, newTotal{};
        while (std::getline(list, name))
        {
            if (!name.empty() && name.back() == '\r') name.pop_back();
            auto bytes = Read(name.c_str()); const auto length = static_cast<int>(bytes.size());
            std::uint32_t oldValue{}, newValue{}; double oldTime{}, newTime{};
            auto oldRun = [&] { const auto start = Clock::now(); oldValue = Original(host.process, 0xffffffff, bytes.data(), length);
                oldTime = std::chrono::duration<double>(Clock::now() - start).count(); };
            auto newRun = [&] { const auto start = Clock::now(); newValue = Current(host.process, 0xffffffff, bytes.data(), length);
                newTime = std::chrono::duration<double>(Clock::now() - start).count(); };
            if (models & 1) { oldRun(); newRun(); } else { newRun(); oldRun(); }
            Require(oldValue == newValue, "real model differs");
            oldTotal += oldTime; newTotal += newTime;
            std::printf("model\t%u\t%zu\t%08x\t%.9f\t%.9f\n", models, bytes.size(), newValue, oldTime, newTime);
            for (const auto at : {std::size_t(0), bytes.size() / 2, bytes.size() - 1})
            {
                bytes[at] ^= 1;
                const auto oldChanged = Original(host.process, 0xffffffff, bytes.data(), length);
                const auto newChanged = Current(host.process, 0xffffffff, bytes.data(), length);
                Require(oldChanged == newChanged && oldChanged != oldValue, "modified model not detected");
                bytes[at] ^= 1;
            }
            ++models;
        }
        Require(models > 0, "no real models");
        std::printf("complete\t%u\t%u\t%.9f\t%.9f\n", checks, models, oldTotal, newTotal);
        return 0;
    }
    catch (const std::exception& error) { std::fprintf(stderr, "CRC fixture: %s\n", error.what()); return 1; }
}
