// SPDX-License-Identifier: GPL-3.0-or-later
#include <emmintrin.h>
#include <cstdint>
#include <cstdlib>
#include <iostream>
using uint32 = std::uint32_t;

// Each case fits in one pending word. The real production writer below is
// unchanged; only its unused output-buffer flush is replaced at this boundary.
struct BitWriter
{
    union alignas(16) { std::uint64_t u64[2]; uint32 u32[4]; } pendingData{};
    int nCurOutputBit{};
} bfwrite;
void MSG_WBits_MaybeFlush() { if (bfwrite.nCurOutputBit >= 32) std::abort(); }
#include "engine_ammo_writer.inc"

int main(int argc, char** argv)
{
    if (argc != 3) return 2;
    const int bits = std::atoi(argv[1]);
    const bool sign = std::atoi(argv[2]) != 0;
    if (bits < 2 || bits > 20) return 2;
    int checks = 0, failures = 0;
    for (int ammo = 0; ammo <= 32767; ++ammo)
    {
        bfwrite = {};
        MSG_WriteBits(5, 3);
        if (sign) MSG_WriteSBits(ammo, bits);
        else MSG_WriteBits(ammo, bits);
        MSG_WriteBits(6, 3);
        const auto raw = bfwrite.pendingData.u64[0];
        const auto payload = (raw >> 3) & ((std::uint64_t{1} << bits) - 1);
        const int decoded = sign ? int(payload >> 1) * (payload & 1 ? -1 : 1) : int(payload);
        ++checks;
        if (decoded != ammo || (raw & 7) != 5 || (raw >> (bits + 3)) != 6 ||
            bfwrite.nCurOutputBit != bits + 6)
        {
            if (failures++ == 0) std::cout << "first mismatch sent=" << ammo << " received=" << decoded << '\n';
        }
    }
    std::cout << "checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
