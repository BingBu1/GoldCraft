// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace csol::playerclip
{
inline constexpr char Message[] = "CsoPClip";
inline constexpr std::size_t Capacity = 64;
enum class Operation : std::uint8_t { Reset, Set, Remove };
struct Descriptor
{
    std::uint16_t entity{}, model{};
    std::array<float, 3> origin{}, angles{};
    bool operator==(const Descriptor&) const = default;
};
struct Packet
{
    Operation operation{};
    Descriptor clip;
};
inline bool Valid(const Descriptor& d)
{
    if (!d.entity || !d.model) return false;
    for (float v : d.origin) if (!std::isfinite(v)) return false;
    for (float v : d.angles) if (!std::isfinite(v)) return false;
    return true;
}
inline std::optional<Packet> Read(std::span<const std::uint8_t> bytes)
{
    if (bytes.size() < 2 || bytes[0] != 1 || bytes[1] > 2) return {};
    Packet p; p.operation = static_cast<Operation>(bytes[1]);
    if (p.operation == Operation::Reset) return bytes.size() == 2 ? std::optional(p) : std::nullopt;
    if (bytes.size() != (p.operation == Operation::Remove ? 4 : 30)) return {};
    p.clip.entity = static_cast<std::uint16_t>(bytes[2] | unsigned(bytes[3]) << 8);
    if (!p.clip.entity) return {};
    if (p.operation == Operation::Remove) return p;
    p.clip.model = static_cast<std::uint16_t>(bytes[4] | unsigned(bytes[5]) << 8);
    for (unsigned i = 0; i != 6; ++i)
    {
        const auto offset = 6 + i * 4;
        const auto bits = std::uint32_t(bytes[offset]) | std::uint32_t(bytes[offset+1]) << 8 |
            std::uint32_t(bytes[offset+2]) << 16 | std::uint32_t(bytes[offset+3]) << 24;
        (i < 3 ? p.clip.origin[i] : p.clip.angles[i-3]) = std::bit_cast<float>(bits);
    }
    return Valid(p.clip) ? std::optional(p) : std::nullopt;
}
inline std::size_t Write(const Packet& p, std::array<std::uint8_t, 30>& bytes)
{
    bytes = {}; bytes[0] = 1; bytes[1] = static_cast<std::uint8_t>(p.operation);
    if (p.operation == Operation::Reset) return 2;
    if (!p.clip.entity) return 0;
    bytes[2] = static_cast<std::uint8_t>(p.clip.entity); bytes[3] = p.clip.entity >> 8;
    if (p.operation == Operation::Remove) return 4;
    if (p.operation != Operation::Set || !Valid(p.clip)) return 0;
    bytes[4] = static_cast<std::uint8_t>(p.clip.model); bytes[5] = p.clip.model >> 8;
    for (unsigned i = 0; i != 6; ++i)
    {
        const auto bits = std::bit_cast<std::uint32_t>(i < 3 ? p.clip.origin[i] : p.clip.angles[i-3]);
        for (unsigned j = 0; j != 4; ++j) bytes[6+i*4+j] = static_cast<std::uint8_t>(bits >> (j*8));
    }
    return 30;
}
}
