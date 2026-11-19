// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#define GOLDCRAFT_CRC32_IEEE_X86
#include <immintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#if defined(__clang__) || defined(__GNUC__)
#define GOLDCRAFT_CRC32_IEEE_PCLMUL __attribute__((target("pclmul,sse2")))
#else
#define GOLDCRAFT_CRC32_IEEE_PCLMUL
#endif
#endif

namespace goldcraft::crc32_ieee
{
// Same reflected IEEE polynomial and unfinalized state as GoldSrc CRC32.
// This host optimization changes neither the covered bytes nor the checksum.
// The SSE4.2 CRC instruction uses a different polynomial (CRC32C).
inline constexpr auto tables = [] {
    std::array<std::array<std::uint32_t, 256>, 8> result{};
    for (std::uint32_t value = 0; value < 256; ++value)
    {
        auto crc = value;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0u);
        result[0][value] = crc;
    }
    for (std::size_t slice = 1; slice < result.size(); ++slice)
        for (std::size_t value = 0; value < 256; ++value)
        {
            const auto crc = result[slice - 1][value];
            result[slice][value] = result[0][crc & 255] ^ (crc >> 8);
        }
    return result;
}();

namespace detail
{
inline std::uint32_t UpdateSlicing(std::uint32_t crc, const void* buffer, int length)
{
    static_assert(std::endian::native == std::endian::little);
    const auto* bytes = static_cast<const unsigned char*>(buffer);
    while (length >= 8)
    {
        std::uint32_t low, high;
        std::memcpy(&low, bytes, sizeof(low));
        std::memcpy(&high, bytes + 4, sizeof(high));
        low ^= crc;
        crc = tables[7][low & 255] ^ tables[6][(low >> 8) & 255] ^
            tables[5][(low >> 16) & 255] ^ tables[4][low >> 24] ^
            tables[3][high & 255] ^ tables[2][(high >> 8) & 255] ^
            tables[1][(high >> 16) & 255] ^ tables[0][high >> 24];
        bytes += 8;
        length -= 8;
    }
    while (length > 0)
    {
        crc = tables[0][(crc ^ *bytes++) & 255] ^ (crc >> 8);
        --length;
    }
    return crc;
}

#ifdef GOLDCRAFT_CRC32_IEEE_X86
inline bool HasPclmul()
{
    static const bool supported = [] {
#if defined(_MSC_VER)
        int registers[4];
        __cpuid(registers, 1);
        return (registers[2] & (1 << 1)) != 0;
#else
        unsigned eax, ebx, ecx, edx;
        return __get_cpuid(1, &eax, &ebx, &ecx, &edx) && (ecx & (1u << 1));
#endif
    }();
    return supported;
}

constexpr std::uint64_t Polynomial = 0x104c11db7;
constexpr std::uint64_t LeadingBit = 1ull << 32;

consteval std::uint64_t Reflect(std::uint64_t value, unsigned width)
{
    std::uint64_t result = 0;
    for (unsigned i = 0; i < width; ++i)
    {
        result = (result << 1) | (value & 1);
        value >>= 1;
    }
    return result;
}

consteval std::uint64_t ReflectedPower(unsigned exponent)
{
    std::uint64_t remainder = 1;
    for (unsigned i = 0; i < exponent; ++i)
    {
        remainder <<= 1;
        if (remainder & LeadingBit)
            remainder ^= Polynomial;
    }
    return Reflect(remainder, 32) << 1;
}

consteval std::uint64_t BarrettQuotient()
{
    // Divide x^64 by the polynomial, starting after its leading one is removed.
    std::uint64_t remainder = Polynomial ^ LeadingBit;
    std::uint64_t quotient = 1;
    for (unsigned i = 0; i < 32; ++i)
    {
        remainder <<= 1;
        quotient <<= 1;
        if (remainder & LeadingBit)
        {
            remainder ^= Polynomial;
            quotient |= 1;
        }
    }
    return quotient;
}

GOLDCRAFT_CRC32_IEEE_PCLMUL inline __m128i LoadBlock(const void* bytes)
{
    __m128i block;
    std::memcpy(&block, bytes, sizeof(block));
    return block;
}

GOLDCRAFT_CRC32_IEEE_PCLMUL inline __m128i FoldBlock(__m128i state, __m128i factors, __m128i next)
{
    const auto low = _mm_clmulepi64_si128(state, factors, 0x00);
    const auto high = _mm_clmulepi64_si128(state, factors, 0x11);
    return _mm_xor_si128(next, _mm_xor_si128(low, high));
}

// Reflected folding and Barrett reduction (Gopal et al., Fast CRC Computation
// for Generic Polynomials Using PCLMULQDQ). The caller supplies at least 64 bytes.
GOLDCRAFT_CRC32_IEEE_PCLMUL inline std::uint32_t UpdatePclmul(
    std::uint32_t crc, const unsigned char* bytes, int length)
{
    constexpr int blockBytes = 16;
    constexpr int batchBytes = 4 * blockBytes;
    constexpr std::array batchPowers{ReflectedPower(batchBytes * 8 + 32), ReflectedPower(batchBytes * 8 - 32)};
    constexpr std::array blockPowers{ReflectedPower(blockBytes * 8 + 32), ReflectedPower(blockBytes * 8 - 32)};
    constexpr std::array reduce64{ReflectedPower(64), std::uint64_t{0}};
    constexpr std::array barrett{Reflect(Polynomial, 33), Reflect(BarrettQuotient(), 33)};

    auto first = _mm_xor_si128(LoadBlock(bytes), _mm_cvtsi32_si128(std::bit_cast<std::int32_t>(crc)));
    auto second = LoadBlock(bytes + blockBytes);
    auto third = LoadBlock(bytes + 2 * blockBytes);
    auto fourth = LoadBlock(bytes + 3 * blockBytes);
    bytes += batchBytes;
    length -= batchBytes;
    const auto batchFactors = LoadBlock(batchPowers.data());
    while (length >= batchBytes)
    {
        first = FoldBlock(first, batchFactors, LoadBlock(bytes));
        second = FoldBlock(second, batchFactors, LoadBlock(bytes + blockBytes));
        third = FoldBlock(third, batchFactors, LoadBlock(bytes + 2 * blockBytes));
        fourth = FoldBlock(fourth, batchFactors, LoadBlock(bytes + 3 * blockBytes));
        bytes += batchBytes;
        length -= batchBytes;
    }

    const auto blockFactors = LoadBlock(blockPowers.data());
    auto state = FoldBlock(first, blockFactors, second);
    state = FoldBlock(state, blockFactors, third);
    state = FoldBlock(state, blockFactors, fourth);
    while (length >= blockBytes)
    {
        state = FoldBlock(state, blockFactors, LoadBlock(bytes));
        bytes += blockBytes;
        length -= blockBytes;
    }

    const auto lowWord = _mm_cvtsi32_si128(-1);
    state = _mm_xor_si128(_mm_srli_si128(state, 8), _mm_clmulepi64_si128(state, blockFactors, 0x10));
    state = _mm_xor_si128(_mm_srli_si128(state, 4),
        _mm_clmulepi64_si128(_mm_and_si128(state, lowWord), LoadBlock(reduce64.data()), 0x00));
    const auto reduction = LoadBlock(barrett.data());
    const auto quotient = _mm_clmulepi64_si128(_mm_and_si128(state, lowWord), reduction, 0x10);
    state = _mm_xor_si128(state, _mm_clmulepi64_si128(_mm_and_si128(quotient, lowWord), reduction, 0x00));
    const auto folded = static_cast<std::uint32_t>(_mm_cvtsi128_si32(_mm_srli_si128(state, 4)));
    return UpdateSlicing(folded, bytes, length);
}
#endif
}

inline std::uint32_t Update(std::uint32_t crc, const void* buffer, int length)
{
#ifdef GOLDCRAFT_CRC32_IEEE_X86
    if (length >= 64 && detail::HasPclmul())
        return detail::UpdatePclmul(crc, static_cast<const unsigned char*>(buffer), length);
#endif
    return detail::UpdateSlicing(crc, buffer, length);
}
}

#undef GOLDCRAFT_CRC32_IEEE_PCLMUL
#undef GOLDCRAFT_CRC32_IEEE_X86
