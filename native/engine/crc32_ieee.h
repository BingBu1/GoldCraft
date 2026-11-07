// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

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

inline std::uint32_t Update(std::uint32_t crc, const void* buffer, int length)
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
}
