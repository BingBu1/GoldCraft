// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/scenario/custom/standalone_movement.hpp"
#include <Windows.h>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include "original_movement.inc"

namespace custom = csol::scenario::custom;
namespace
{
unsigned checks{}, speedReads{}, speedResets{}, cases{};
void Check(bool value, const char* text)
{
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL case=%u %s\n", cases, text); std::exit(1); }
}
[[noreturn]] void Forbidden()
{ std::fputs("FAIL excluded original service/invariant was invoked\n", stderr); ExitProcess(3); }
constexpr unsigned Cookie = 0x3752a19b;
void __fastcall CheckCookie(unsigned value) { Check(value == Cookie, "original stack cookie retained"); }
std::array<std::byte, 0x244> entity{};
std::array<std::byte, 16> player{};
std::array<std::uintptr_t, 108> vtable{};
std::array<float, 2> speedLimits{};
float weaponSpeed{};
void* speedCode{};
void* currentOwner{};
void* __cdecl ResolvePlayer(int slot)
{ Check(slot == 1, "original resolves controller player"); return player.data(); }
float __cdecl ReadLimit(const char* name)
{
    Check(!std::strcmp(name, "sv_maxspeed") && speedReads < 2, "original maxspeed query contract");
    return speedLimits[speedReads++];
}
void __fastcall ResetSpeed(void* actor, void*)
{
    Check(actor == player.data(), "original native speed owner");
    ++speedResets;
    reinterpret_cast<void (__thiscall*)(void*, float)>(speedCode)(currentOwner, weaponSpeed);
}
class Original
{
public:
    Original()
    {
        static_assert(sizeof(void*) == 4);
        auto* address = entity.data(); std::memcpy(player.data() + 8, &address, 4);
        auto* table = vtable.data(); std::memcpy(player.data(), &table, 4);
        vtable[0x1ac / 4] = reinterpret_cast<std::uintptr_t>(&ResetSpeed);
        run_ = Load(OriginalRun, OriginalRunFixups);
        speed_ = Load(OriginalSpeed, OriginalSpeedFixups);
        speedCode = speed_;
    }
    ~Original() { VirtualFree(speed_, 0, MEM_RELEASE); VirtualFree(run_, 0, MEM_RELEASE); }
    void Run(std::array<std::byte, 128>& state, float cost)
    {
        currentOwner = state.data(); cost_ = cost;
        reinterpret_cast<void (__thiscall*)(void*)>(run_)(state.data());
    }
    void Speed(std::array<std::byte, 128>& state, float base)
    { reinterpret_cast<void (__thiscall*)(void*, float)>(speed_)(state.data(), base); }
private:
    template<std::size_t N, std::size_t M>
    void* Load(const unsigned char (&bytes)[N], const Fixup (&fixups)[M])
    {
        auto* code = static_cast<unsigned char*>(VirtualAlloc(nullptr, N, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        Check(code != nullptr, "original code allocation"); std::memcpy(code, bytes, N);
        for (const auto& fix : fixups)
        {
            std::uintptr_t target{};
            if (fix.kind == 0) target = reinterpret_cast<std::uintptr_t>(Binding(fix.target));
            else if (fix.kind == 1) target = reinterpret_cast<std::uintptr_t>(&ResolvePlayer);
            else if (fix.kind == 2) target = reinterpret_cast<std::uintptr_t>(&CheckCookie);
            else if (fix.kind == 3) target = reinterpret_cast<std::uintptr_t>(&Forbidden);
            else Check(false, "unbound original relocation");
            const auto word = std::uint32_t(fix.kind == 0 ? target : target - reinterpret_cast<std::uintptr_t>(code + fix.offset + 4));
            std::memcpy(code + fix.offset, &word, 4);
        }
        DWORD previous{};
        Check(VirtualProtect(code, N, PAGE_EXECUTE_READ, &previous) != 0, "original code sealed RX");
        Check(FlushInstructionCache(GetCurrentProcess(), code, N) != 0, "original code cache flush");
        return code;
    }
    void* Binding(unsigned kind)
    {
        switch (kind)
        {
        case 0: return &cookie_;
        case 1: return &emptyCallback_;
        case 2: return &guard_;
        case 3: return &costPointer_;
        case 4: return &clockPointer_;
        case 5: return object_.data();
        case 6: return &forbiddenPointer_;
        case 7: return &noisePeriod_;
        case 8: return const_cast<char*>("makenoise %d %.2f %.2f %.2f %.2f\n");
        case 9: return &limitPointer_;
        case 10: return &duckMultiplier_;
        case 11: return &walkMultiplier_;
        case 12: return const_cast<char*>("sv_maxspeed");
        default: Check(false, "unknown original global"); return nullptr;
        }
    }
    void* run_{}, *speed_{}, *emptyCallback_{};
    bool guard_{};
    unsigned cookie_{Cookie};
    float cost_{.1f}, noisePeriod_{.2f}, duckMultiplier_{.8f}, walkMultiplier_{.5f};
    float* costPointer_{&cost_};
    std::array<float, 2> clock_{10, .016f};
    float* clockPointer_{clock_.data()};
    void (*forbiddenPointer_)(){&Forbidden};
    float (*limitPointer_)(const char*){&ReadLimit};
    std::array<std::byte, 128> object_{};
};
template<class T, std::size_t N> void Put(std::array<std::byte, N>& data, unsigned offset, T value)
{ Check(offset + sizeof(value) <= N, "original field bounds"); std::memcpy(data.data() + offset, &value, sizeof(value)); }
template<class T, std::size_t N> T Read(const std::array<std::byte, N>& data, unsigned offset)
{ T value; std::memcpy(&value, data.data() + offset, sizeof(value)); return value; }
void Same(float actual, float expected, const char* message)
{ Check(std::bit_cast<std::uint32_t>(actual) == std::bit_cast<std::uint32_t>(expected), message); }
void CompareRun(Original& original, custom::StandaloneRunState state, float amount,
    const std::array<float, 3>& origin, bool ducking, float cost)
{
    ++cases;
    std::array<std::byte, 128> owner{};
    Put(owner, 0, 1); Put(owner, 4, std::uint8_t(state.requested)); Put(owner, 5, std::uint8_t(state.running));
    for (unsigned i = 0; i < 3; ++i) { Put(owner, 8 + i * 4, state.previous[i]); Put(entity, 8 + i * 4, origin[i]); }
    Put(owner, 0x30, amount); Put(owner, 0x7c, 10.f);
    Put(entity, 0x1c8, ducking ? 0x4000u : 0u);
    weaponSpeed = 250; speedLimits = {200, 180}; speedReads = speedResets = 0;
    original.Run(owner, cost);
    const auto distance = custom::AdvanceStandaloneRunDisplacement(state, amount, origin, ducking, cost);
    Check(bool(distance), "valid displacement accepted");
    Same(amount, Read<float>(owner, 0x30), "remaining amount matches original bits");
    Check(state.requested == bool(Read<std::uint8_t>(owner, 4)) &&
          state.running == bool(Read<std::uint8_t>(owner, 5)), "request and actual flags match original");
    for (unsigned i = 0; i < 3; ++i) Same(state.previous[i], Read<float>(owner, 8 + i * 4), "prior xyz matches original bits");
    const auto reads = speedReads;
    unsigned wantedReads{};
    const float speed = custom::StandaloneMaxSpeed(weaponSpeed, ducking, state.running,
        [&] { Check(wantedReads < 2, "bounded production maxspeed reads"); return speedLimits[wantedReads++]; });
    Same(speed, Read<float>(entity, 0x240), "post-debit actual flag drives original speed");
    Check(speedResets == 1 && reads == wantedReads, "original speed reset/query ordering retained");
}
}
int main()
{
    Original original;
    constexpr std::array<std::array<float, 3>, 7> points{{
        {0,0,0}, {3,4,200}, {0,0,2048}, {4096.125f,-320.5f,64},
        {4096.15625f,-320.4375f,2048}, {65535,-65535,0}, {-2048.03125f,17.125f,0}}};
    for (const auto& previous : points)
    for (const auto& origin : points)
    for (const bool requested : {false, true})
    for (const bool running : {false, true})
    for (const bool ducking : {false, true})
    for (const float amount : {0.f,.01f,.1f,1.f,15.f,50.f,100.f,150.f})
    for (const float cost : {0.f,.1f,.17f,2.f})
        CompareRun(original, {previous, requested, running}, amount, origin, ducking, cost);
    const auto displacementCases = cases;
    for (const float base : {0.f,1.f,80.f,125.f,235.f,250.f,260.f,400.f,4096.f})
    for (const float cap : {0.f,.5f,80.f,120.f,125.f,200.f,250.f,320.f,4096.f})
    for (const bool ducking : {false, true})
    for (const bool running : {false, true})
    {
        ++cases;
        std::array<std::byte, 128> owner{}; Put(owner, 0, 1); Put(owner, 5, std::uint8_t(running));
        Put(entity, 0x1c8, ducking ? 0x4000u : 0u);
        speedLimits = {cap, cap + 17}; speedReads = 0;
        original.Speed(owner, base);
        unsigned reads{};
        const float speed = custom::StandaloneMaxSpeed(base, ducking, running, [&] { return speedLimits[reads++]; });
        Same(speed, Read<float>(entity, 0x240), "standalone speed bits match original");
        Check(reads == speedReads, "standalone speed query count matches original");
    }
    custom::StandaloneRunState invalid{{1,2,3},true,true}, before = invalid;
    float amount = 10;
    Check(!custom::AdvanceStandaloneRunDisplacement(invalid, amount, {0,0,std::numeric_limits<float>::infinity()}, false) &&
          amount == 10 && invalid.previous == before.previous && invalid.running == before.running,
          "host rejects invalid coordinates before mutation");
    Check(!custom::AdvanceStandaloneRunDisplacement(invalid, amount, {0,0,0}, false, -1) && amount == 10,
          "host rejects invalid configured distance debit");
    std::printf("displacement=%u speed=%u checks=%u failures=0\n", displacementCases, cases-displacementCases, checks);
}
