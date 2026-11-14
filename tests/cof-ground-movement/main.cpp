// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/scenario/custom/cof_ground_movement.hpp"
#include <Windows.h>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <span>
#include <utility>
#include <vector>
#include "original_oracle.inc"

namespace custom = csol::scenario::custom;
namespace
{
unsigned checks{};
void Check(bool value, const char* why)
{ ++checks; if (!value) { std::printf("FAIL %s\n", why); std::exit(1); } }
bool Bits(float a, float b) { return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b); }
class OriginalCode
{
public:
    explicit OriginalCode(std::span<const unsigned char> bytes)
    {
        size_ = bytes.size(); memory_ = VirtualAlloc(nullptr, size_, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        Check(memory_ != nullptr, "workspace fixture allocates a bounded oracle buffer");
        std::memcpy(memory_, bytes.data(), size_);
    }
    void Constant(std::uint32_t original, const float* actual)
    {
        unsigned replacements{};
        auto* bytes = static_cast<unsigned char*>(memory_);
        for (std::size_t i = 0; i + 4 <= size_; ++i)
        {
            std::uint32_t value; std::memcpy(&value, bytes + i, 4);
            if (value == original)
            {
                const auto address = std::uint32_t(reinterpret_cast<std::uintptr_t>(actual));
                std::memcpy(bytes + i, &address, 4); ++replacements; i += 3;
            }
        }
        Check(replacements != 0, "explicit original constant relocation exists");
    }
    void Seal()
    {
        DWORD old{};
        Check(VirtualProtect(memory_, size_, PAGE_EXECUTE_READ, &old) != 0,
            "self-test oracle becomes read/execute without writable executable memory");
        Check(FlushInstructionCache(GetCurrentProcess(), memory_, size_) != 0, "fixture code cache is synchronized");
    }
    template<class Function> Function FunctionPointer() const { return reinterpret_cast<Function>(memory_); }
    ~OriginalCode() { VirtualFree(memory_, 0, MEM_RELEASE); }
private:
    void* memory_{};
    std::size_t size_{};
};
float zero{}, one{1};
std::array<std::byte, 0x45474> movement{};
void Float(unsigned offset, float value) { std::memcpy(movement.data() + offset, &value, 4); }
float Float(unsigned offset) { float value; std::memcpy(&value, movement.data() + offset, 4); return value; }
std::uint32_t randomState{0x6ab14f3d};
float Input()
{
    randomState = randomState * 1664525u + 1013904223u;
    return float(int((randomState >> 6) & 0xffff) - 32768) / 32.0f;
}
}
int main()
{
    Check(sizeof(void*) == 4, "original oracle executes only in the matching x86 ABI");
    OriginalCode normalize(NormalizeCode), limit(LimitCode);
    normalize.Constant(0x101a1370, &zero); normalize.Constant(0x101a1390, &one);
    limit.Constant(0x101a1370, &zero);
    normalize.Seal(); limit.Seal();
    const auto nativeNormalize = normalize.FunctionPointer<float (__cdecl*)(float*)>();
    const auto nativeLimit = limit.FunctionPointer<void (__cdecl*)(void*)>();
    for (unsigned i = 0; i != 4096; ++i)
    {
        custom::MovementVector original{Input(), Input(), Input()}, restored = original;
        if (i == 0) original = restored = {};
        const float expected = nativeNormalize(original.data());
        const float actual = custom::NormalizeCofVector(restored);
        Check(Bits(actual, expected), "length matches the actual original x86 instruction window bit for bit");
        for (unsigned axis = 0; axis != 3; ++axis)
            Check(Bits(original[axis], restored[axis]), "normalized direction matches original float reciprocal order");
        custom::CofMoveCommand command{Input(), Input(), Input()};
        if (i == 0) command = {};
        const float maximum = i % 3 == 0 ? 300.0f : i % 3 == 1 ? 150.0f : 17.5f;
        Float(0x1f4, maximum); Float(0x45468, command.forward);
        Float(0x4546c, command.side); Float(0x45470, command.up);
        nativeLimit(movement.data());
        const auto candidate = custom::LimitCofCommand(command, maximum);
        Check(bool(candidate), "finite bounded command is accepted");
        Check(Bits(candidate->forward, Float(0x45468)) && Bits(candidate->side, Float(0x4546c)) &&
            Bits(candidate->up, Float(0x45470)), "raw3D cap matches original machine instructions bit for bit");
    }
    const custom::MovementVector forward{1, 0, 0}, right{0, -1, 0};
    for (const auto [percentage, expected] : {std::pair{75.f, 225.f}, {45.f, 135.f}, {125.f, 300.f}})
    {
        const auto wish = custom::CofNormalGroundWish({300, 0, 0}, forward, right, 300, percentage);
        Check(wish && wish->speed == expected && wish->direction[0] == 1, "walk/slow/sprint refs are percentages before the final speed cap");
    }
    const auto half = custom::CofNormalGroundWish({150, 0, 0}, forward, right, 300, 75);
    Check(half && half->speed == 112.5f, "half-input cannot be replaced by an absolute maxspeed225 cap");
    const auto diagonal = custom::CofNormalGroundWish({300, 200, 0}, forward, right, 300, 75);
    Check(diagonal && std::abs(diagonal->speed - 225) < .001f && diagonal->speed < 226,
        "diagonal input is capped before applying75 percent, not270.416");
    const auto up = custom::CofNormalGroundWish({300, 0, 300}, forward, right, 300, 75);
    Check(up && up->speed < 160 && up->speed > 159, "rawup participates in the original precap before planar ground movement");
    const auto pitched = custom::CofNormalGroundWish({300, 0, 0}, {0, .25f, .8f}, right, 300, 75);
    Check(pitched && pitched->speed == 225 && pitched->direction[1] == 1, "horizontal view basis is normalized before forming wish direction");
    const auto zeroWish = custom::CofNormalGroundWish({}, forward, right, 300, 75);
    Check(zeroWish && zeroWish->speed == 0 && zeroWish->direction == custom::MovementVector{}, "zero input remains motionless");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    Check(!custom::CofNormalGroundWish({nan, 0, 0}, forward, right, 300, 75), "invalid input cannot become movement authority");
    Check(!custom::CofNormalGroundWish({300, 0, 0}, forward, right, 0, 75), "invalid server maximum is rejected");
    Check(!custom::CofNormalGroundWish({300, 0, 0}, forward, right, 300, -1), "invalid percentage is rejected");
    std::printf("CoF original ground arithmetic: %u checks passed\n", checks);
}
