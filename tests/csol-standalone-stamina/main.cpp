// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/scenario/custom/standalone_stamina.hpp"
#include <Windows.h>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <utility>
#include "original_recovery.inc"

namespace custom = csol::scenario::custom;
namespace
{
unsigned checks{}, low{}, recovered{};
void Check(bool value, const char* text)
{
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL %s\n", text); std::exit(1); }
}
[[noreturn]] void InvariantFailure() { std::fputs("FAIL original invariant callback\n", stderr); ExitProcess(3); }
std::array<std::byte, 0x240> entity{};
std::array<std::byte, 16> player{};
void* __cdecl ResolvePlayer(int index)
{
    Check(index == 1, "original resolves declared player index");
    return player.data();
}
void __cdecl Emit(int recipient, int channel, const char* path, float volume, float attenuation, int flags, int pitch)
{
    Check(recipient == 42 && channel == 2 && volume == 1 && attenuation == .8f && flags == 0 && pitch == 100,
        "original player/audio arguments retain exact recorded values");
    if (!std::strcmp(path, "pve_standalone/stamina_down_loop.wav")) ++low;
    else if (!std::strcmp(path, "pve_standalone/stamina_up.wav")) ++recovered;
    else Check(false, "unexpected original audio path");
}
class Original
{
public:
    Original()
    {
        static_assert(sizeof(void*) == 4);
        auto* address = entity.data();
        std::memcpy(player.data() + 8, &address, 4);
        const int recipient = 42; std::memcpy(entity.data() + 0x238, &recipient, 4);
        code_ = static_cast<unsigned char*>(VirtualAlloc(nullptr, sizeof(OriginalRecovery), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        Check(code_ != nullptr, "bounded original code allocation");
        std::memcpy(code_, OriginalRecovery, sizeof(OriginalRecovery));
        for (const auto& fix : OriginalFixups)
        {
            std::uintptr_t target{};
            if (fix.kind == 0) target = reinterpret_cast<std::uintptr_t>(Binding(fix.target));
            else if (fix.kind == 1) target = reinterpret_cast<std::uintptr_t>(&ResolvePlayer);
            else if (fix.kind == 2) target = reinterpret_cast<std::uintptr_t>(&Emit);
            else if (fix.kind == 3) target = reinterpret_cast<std::uintptr_t>(&InvariantFailure);
            else Check(false, "unknown original relocation");
            const auto word = std::uint32_t(fix.kind == 0 ? target : target - reinterpret_cast<std::uintptr_t>(code_ + fix.offset + 4));
            std::memcpy(code_ + fix.offset, &word, 4);
        }
        DWORD previous{};
        Check(VirtualProtect(code_, sizeof(OriginalRecovery), PAGE_EXECUTE_READ, &previous) != 0, "original code sealed RX");
        Check(FlushInstructionCache(GetCurrentProcess(), code_, sizeof(OriginalRecovery)) != 0, "original code cache flush");
    }
    ~Original() { VirtualFree(code_, 0, MEM_RELEASE); }
    void Run(std::array<std::byte, 128>& state, float now, float dt)
    {
        clock_ = {now, dt};
        reinterpret_cast<void (__thiscall*)(void*)>(code_)(state.data());
    }
private:
    void* Binding(unsigned kind)
    {
        switch (kind)
        {
        case 0: return &emptyCallback_;
        case 1: return &guard_;
        case 2: return &delayPointer_;
        case 3: return &clockPointer_;
        case 4: return &thresholdPointer_;
        case 5: return &maximum_;
        case 6: return &accelerationPointer_;
        case 7: return object_.data();
        case 8: return const_cast<char*>("pve_standalone/stamina_down_loop.wav");
        case 9: return const_cast<char*>("pve_standalone/stamina_up.wav");
        default: Check(false, "unbound original global"); return nullptr;
        }
    }
    unsigned char* code_{};
    void* emptyCallback_{};
    bool guard_{};
    float delay_{3}, threshold_{11}, maximum_{100}, acceleration_{.1f};
    float* delayPointer_{&delay_};
    float* thresholdPointer_{&threshold_};
    float* accelerationPointer_{&acceleration_};
    std::array<float, 2> clock_{};
    float* clockPointer_{clock_.data()};
    std::array<std::byte, 64> object_{};
};
template<class T> void Put(std::array<std::byte, 128>& data, unsigned offset, T value)
{ Check(offset + sizeof(value) <= data.size(), "explicit original state field bounds"); std::memcpy(data.data() + offset, &value, sizeof(value)); }
template<class T> T Read(const std::array<std::byte, 128>& data, unsigned offset)
{ T value; std::memcpy(&value, data.data() + offset, sizeof(value)); return value; }
void Compare(Original& original, custom::StandaloneRecoveryState state, float now, float dt)
{
    std::array<std::byte, 128> owner{};
    Put(owner, 0, 1);
    Put(owner, 0x18, state.wait); Put(owner, 0x1c, state.deadline); Put(owner, 0x20, state.started);
    Put(owner, 0x30, state.amount); Put(owner, 0x34, state.observed); Put(owner, 0x38, state.accumulated);
    Put(owner, 0x78, std::uint8_t(state.lowSound));
    low = recovered = 0;
    original.Run(owner, now, dt);
    const auto events = custom::AdvanceStandaloneRecovery(state, {}, now, dt);
    Check(bool(events), "valid original fixture accepted by production recovery");
    for (const auto [offset, actual] : std::array<std::pair<unsigned, float>, 6>{{
        {0x18, state.wait}, {0x1c, state.deadline}, {0x20, state.started},
        {0x30, state.amount}, {0x34, state.observed}, {0x38, state.accumulated}}})
        Check(std::bit_cast<std::uint32_t>(actual) == std::bit_cast<std::uint32_t>(Read<float>(owner, offset)),
            "production float field equals actual original instruction result bit for bit");
    Check(state.lowSound == bool(Read<std::uint8_t>(owner, 0x78)), "original low-sound latch retained");
    Check(unsigned(events->low) == low && unsigned(events->recovered) == recovered, "production audio edges match original calls exactly");
}
}
int main()
{
    Original original;
    constexpr std::array amounts{0.f, .1f, 10.f, 11.f, 15.f, 50.f, 99.f, 100.f};
    constexpr std::array frames{0.f, .001f, .016f, .033f, .1f, .255f};
    unsigned cases{};
    for (const auto amount : amounts)
    for (const bool changed : {false, true})
    for (const bool latched : {false, true})
    for (const float accumulated : {0.f, .1f, 1.25f})
    for (const auto dt : frames)
    for (const float when : {99.f, 100.f, std::nextafter(100.f, 101.f), 103.f, std::nextafter(103.f, 104.f)})
    {
        custom::StandaloneRecoveryState state;
        state.amount = amount; state.observed = changed ? (amount == 100 ? 99 : 100) : amount;
        state.accumulated = accumulated; state.lowSound = latched;
        state.deadline = 100; state.started = 97;
        Compare(original, state, when, dt); ++cases;
    }
    custom::StandaloneRecoveryState bad, before = bad;
    Check(!custom::AdvanceStandaloneRecovery(bad, {}, std::numeric_limits<float>::infinity(), .016f) &&
        std::memcmp(&bad, &before, sizeof(bad)) == 0, "invalid host clock rejected without state mutation");
    Check(!custom::CanAffordStandaloneJump(14.99f, 15, false) && custom::CanAffordStandaloneJump(15, 15, false),
        "human cost threshold rejects insufficient balance and accepts exact fee");
    Check(custom::CanAffordStandaloneJump(0, 15, true) && !custom::CanAffordStandaloneJump(0, 15, false),
        "human unlimited noon exception does not grant finite zero-stamina jumping");
    std::printf("CSOL StandAlone original recovery: cases=%u checks=%u failures=0\n", cases, checks);
}
