// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace csol
{
// CSO configuration files contain little-endian 32-round TEA blocks without a
// header or checksum. Decode owned memory; retain every byte, including padding.
// Unlike the original routine, reject an incomplete block before writing.
inline bool DecodeConfiguration(std::span<std::byte> bytes)
{
    if (bytes.size() % 8) return false;
    constexpr std::uint32_t key[]{0xb38520fd, 0x5f30e75f, 0xe2eb67e0, 0xfc67a521};
    constexpr std::uint32_t delta = 0x9e3779b9;
    const auto read = [](const std::byte* p)
    {
        return std::to_integer<std::uint32_t>(p[0]) |
            (std::to_integer<std::uint32_t>(p[1]) << 8) |
            (std::to_integer<std::uint32_t>(p[2]) << 16) |
            (std::to_integer<std::uint32_t>(p[3]) << 24);
    };
    const auto write = [](std::byte* p, std::uint32_t word)
    {
        for (int i = 0; i < 4; ++i) p[i] = std::byte((word >> (8 * i)) & 255);
    };
    for (std::size_t at = 0; at < bytes.size(); at += 8)
    {
        auto left = read(bytes.data() + at);
        auto right = read(bytes.data() + at + 4);
        auto sum = delta * 32;
        for (int round = 0; round < 32; ++round)
        {
            right -= ((left << 4) + key[2]) ^ (left + sum) ^ ((left >> 5) + key[3]);
            left -= ((right << 4) + key[0]) ^ (right + sum) ^ ((right >> 5) + key[1]);
            sum -= delta;
        }
        write(bytes.data() + at, left);
        write(bytes.data() + at + 4, right);
    }
    return true;
}
}
