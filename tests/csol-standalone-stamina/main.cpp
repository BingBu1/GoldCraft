// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/scenario/custom/standalone_stamina.hpp"
#include "../../native/csol/scenario/custom/standalone_stamina_state.hpp"
#include "../../native/csol/scenario/custom/jump_prediction.hpp"
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
    const custom::LifeKey life{{1, 1}, 1};
    custom::StandaloneStaminaState state;
    Check(state.Begin(life, custom::Phase::Night, 100), "current owner begins a finite life");
    for (unsigned i = 0; i != 9; ++i) Check(state.DebitKick(life), "accepted kicks deplete actual profile");
    Check(!state.CanJump(life) && !state.DebitJump(life) && state.View(life)->amount == 10,
        "insufficient balance cannot accept or debit jump");
    Check(state.DebitKick(life) && state.View(life)->amount == 0, "finite depletion reaches zero");
    Check(state.Step(life, 1, 100, .1f)->amount == 0 && !state.RecoveryVoice(), "debit observation starts original wait");
    Check(state.Step(life, 2, 103, .1f)->amount == 0, "exact original deadline cannot recover");
    const auto recoveredView = state.Step(life, 3, 103.1f, .1f);
    Check(recoveredView && recoveredView->amount > 0 && state.RecoveryVoice(), "first real recovery emits depleted voice once");
    const auto sameFrame = state.Step(life, 4, 103.1f, .1f);
    Check(sameFrame->amount == recoveredView->amount && !state.RecoveryVoice(), "same native frame cannot repeat recovery or voice");
    Check(state.Configure(life, {}, custom::Phase::Noon) && state.CanJump(life) && state.DebitJump(life) &&
        state.View(life)->amount == sameFrame->amount, "authoritative noon allows jumps without refill or debit");
    Check(state.Retire(life) && !state.CanJump(life) && !state.Begin(life, custom::Phase::Night, 200),
        "retired life cannot jump or rebind full");
    custom::StandaloneStaminaConfig stopped;
    stopped.recoveryPerSecond=0;
    custom::StandaloneStaminaState disabledRecovery;
    Check(disabledRecovery.Begin(life,custom::Phase::Night,100,stopped),"explicit zero recovery override is accepted");
    for(unsigned i=0;i<10;++i)Check(disabledRecovery.DebitKick(life),"zero-recovery life reaches actual depletion");
    Check(bool(disabledRecovery.Step(life,1,100,.1f)),"zero-recovery debit observation retains original wait");
    const auto noRecovery=disabledRecovery.Step(life,2,104,.1f);
    Check(noRecovery&&noRecovery->amount==0&&noRecovery->exhausted&&!disabledRecovery.RecoveryVoice(),
        "explicit zero rate cannot manufacture actual recovery or recovery voice");
    custom::StandaloneStaminaConfig lowConfig;
    lowConfig.recovery.maximum=5;lowConfig.kickCost=5;lowConfig.jumpCost=5;
    custom::StandaloneStaminaState lowCapacity;
    Check(lowCapacity.Begin(life,custom::Phase::Night,100,lowConfig)&&lowCapacity.DebitJump(life)&&!lowCapacity.CanJump(life),
        "configured small capacity retains user fee gate without rejecting original low-sound threshold");
    struct Movement
    {
        int dead{}, spectator{}, movetype{3}, oldbuttons{};
        struct { int buttons{2}; } cmd;
    } movement;
    Check(custom::ApplyJumpPredictionGate(movement, custom::JumpDenied("1")) && movement.oldbuttons == 2,
        "actual outgoing denial selects native oldbuttons latch");
    movement.oldbuttons = 0;
    Check(!custom::ApplyJumpPredictionGate(movement, custom::JumpDenied("10")) && movement.oldbuttons == 0,
        "malformed or absent server key cannot block ordinary prediction");
    movement.movetype = 5;
    Check(!custom::ApplyJumpPredictionGate(movement, true) && movement.oldbuttons == 0,
        "native ladder movement does not receive ground jump latch");
    std::printf("CSOL StandAlone original recovery: cases=%u checks=%u failures=0\n", cases, checks);
}
