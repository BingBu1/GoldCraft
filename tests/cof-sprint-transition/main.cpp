// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/scenario/custom/stamina.hpp"
#include <Windows.h>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "original_sprint.inc"

namespace custom = csol::scenario::custom;
namespace
{
unsigned checks{}, failures{};
void Check(bool value, const char* why)
{
    ++checks;
    if (!value) { std::printf("FAIL %s\n", why); std::exit(2); }
}
[[noreturn]] void UnsupportedOriginalCall()
{
    std::fputs("FAIL oracle entered excluded grounded-stairs/sqrt-error behavior\n", stderr);
    ExitProcess(3);
}
class Original
{
public:
    Original()
    {
        static_assert(sizeof(void*) == 4);
        memory_ = static_cast<unsigned char*>(VirtualAlloc(nullptr, sizeof(OriginalSprint),
            MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        Check(memory_ != nullptr, "bounded original oracle allocation");
        std::memcpy(memory_, OriginalSprint, sizeof(OriginalSprint));
        for (const auto& fix : OriginalFixups)
        {
            std::uintptr_t target{};
            if (fix.kind == 0) target = reinterpret_cast<std::uintptr_t>(&constants_[fix.target]);
            else if (fix.kind == 1) target = reinterpret_cast<std::uintptr_t>(memory_ + LadderOffset);
            else if (fix.kind == 2) target = reinterpret_cast<std::uintptr_t>(UnsupportedOriginalCall);
            else Check(false, "unknown oracle relocation");
            const auto word = std::uint32_t(fix.kind == 0 ? target : target -
                reinterpret_cast<std::uintptr_t>(memory_ + fix.offset + 4));
            std::memcpy(memory_ + fix.offset, &word, 4);
        }
        DWORD previous{};
        Check(VirtualProtect(memory_, sizeof(OriginalSprint), PAGE_EXECUTE_READ, &previous) != 0,
            "original code seals RX without writable executable pages");
        Check(FlushInstructionCache(GetCurrentProcess(), memory_, sizeof(OriginalSprint)) != 0,
            "original code cache flush");
    }
    ~Original() { VirtualFree(memory_, 0, MEM_RELEASE); }
    void Run(void* player) const { reinterpret_cast<void (__cdecl*)(void*)>(memory_)(player); }
private:
    unsigned char* memory_{};
    // Controlled native globals. Their resulting maxspeed writes are outside
    // this gate/stamina comparison; original amount/flags are compared below.
    const std::array<float, 6> constants_{1, 5, 75, 45, 125, 45};
};
template<std::size_t N, class T> void Put(std::array<std::byte, N>& data, unsigned offset, T value)
{ Check(offset + sizeof(T) <= N, "explicit original field stays within fixture"); std::memcpy(data.data() + offset, &value, sizeof(value)); }
template<class T, std::size_t N> T Read(const std::array<std::byte, N>& data, unsigned offset)
{ T value; std::memcpy(&value, data.data() + offset, sizeof(value)); return value; }
}
int main()
{
    Original original;
    const custom::LifeKey life{{1, 1}, 1};
    unsigned cases{};
    for (unsigned infinite = 0; infinite != 2; ++infinite)
    for (unsigned prior = 0; prior != 2; ++prior)
    for (unsigned water = 0; water != 4; ++water)
    for (unsigned mask = 0; mask != 512; ++mask)
    {
        const auto phase = infinite ? custom::Phase::Noon : custom::Phase::Night;
        custom::StaminaState state;
        Check(state.Begin(life, phase, 0), "production profile begins a real test life");
        custom::StaminaInput input;
        input.key = life; input.phase = phase; input.command = 1;
        input.wantsSprint = input.moving = prior != 0;
        const auto before = state.Step(input);
        Check(before && before->sprinting == bool(prior), "controlled prior sprint state is reachable through production API");
        input.command = 2;
        input.wantsSprint = (mask & 1) != 0; input.moving = (mask & 2) != 0;
        input.backward = (mask & 4) != 0; input.reloading = (mask & 8) != 0;
        input.aiming = (mask & 16) != 0; input.ladder = (mask & 32) != 0;
        input.ducked = (mask & 64) != 0; input.onStairs = (mask & 128) != 0;
        input.weaponAction = (mask & 256) != 0; input.waterLevel = int(water);
        std::array<std::byte, 0x2600> player{};
        std::array<std::byte, 0x280> entity{};
        std::array<std::byte, 0x180> weapon{};
        Put(player, 4, entity.data()); Put(player, 0x252c, weapon.data());
        Put(player, 0x21f0, before->amount); Put(player, 0x21e8, std::uint8_t(infinite));
        Put(player, 0x1ec8, std::uint8_t(input.onStairs));
        Put(entity, 0x20, input.moving ? 100.0f : 0.0f);
        Put(entity, 0x108, input.ladder ? 5 : 3);
        // No grounded-stairs helper is simulated. This is an explicit original
        // midair/region input, and any unexpected helper call fails the process.
        Put(entity, 0x1a8, std::uint32_t((prior ? 0x40000 : 0) | (input.ducked ? 0x4000 : 0)));
        Put(entity, 0x184, std::uint32_t((input.wantsSprint ? 0x1000 : 0) | (input.backward ? 0x10 : 0)));
        Put(entity, 0x1c4, int(water));
        Put(weapon, prior ? 0x106 : 0x104, std::uint8_t(input.reloading));
        Put(weapon, 0x178, std::uint8_t(input.aiming));
        Put(weapon, 0x140, int(input.weaponAction));
        original.Run(player.data());
        const auto actual = state.Step(input);
        Check(bool(actual), "production accepts valid same-life transition");
        const bool expectedRunning = (Read<std::uint32_t>(entity, 0x1a8) & 0x40000) != 0;
        const float expectedAmount = Read<float>(player, 0x21f0);
        ++cases;
        checks += 2;
        if (actual->sprinting != expectedRunning ||
            std::bit_cast<std::uint32_t>(actual->amount) != std::bit_cast<std::uint32_t>(expectedAmount))
        {
            if (++failures <= 6)
                std::printf("FAIL original mask=%u prior=%u water=%u infinite=%u running=%d/%d amount=%g/%g\n",
                    mask, prior, water, infinite, int(actual->sprinting), int(expectedRunning), actual->amount, expectedAmount);
        }
    }
    std::printf("CoF original sprint transition: cases=%u checks=%u failures=%u\n", cases, checks, failures);
    return failures ? 1 : 0;
}
