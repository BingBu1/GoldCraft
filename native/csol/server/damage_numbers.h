// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>

struct edict_s;

// Presentation only. The damage owner selects the proven producer stage,
// validates attacker/target/session and sends exactly once. This interface
// does not calculate damage, modify health or turn a trace into a hit.
struct CSOLDamageNumber
{
    std::int16_t target{};
    std::uint8_t style{}, flags{};
    std::int32_t value{};
};

// Original x86 CVTTSS2SI truncates toward zero. Invalid/out-of-range input
// produces the CPU's indefinite integer there; a new host must reject it
// explicitly instead of treating INT_MIN as real damage or invoking C++ UB.
inline std::optional<std::int32_t> CSOLTruncateDamageNumber(float value)
{
    if (!std::isfinite(value) || value < -2147483648.0f || value >= 2147483648.0f)
        return std::nullopt;
    return static_cast<std::int32_t>(value);
}

// MPToCL/206 is nine bytes, independent of /177 statistics and /199 effects.
// No packed struct, host pointers, float serialization or native sizeof(int).
inline std::array<std::uint8_t, 9> CSOLDamageNumberBytes(CSOLDamageNumber message)
{
    const auto target = std::bit_cast<std::uint16_t>(message.target);
    const auto value = std::bit_cast<std::uint32_t>(message.value);
    return {206, static_cast<std::uint8_t>(target), static_cast<std::uint8_t>(target >> 8),
        message.style, message.flags, static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8), static_cast<std::uint8_t>(value >> 16),
        static_cast<std::uint8_t>(value >> 24)};
}

// messageId is the current engine's registered MPToCL ID, never an original
// binary ID. Call on the server thread with no other engine message open.
// The authoritative owner supplies the attacker recipient; no broadcast,
// spectator fanout, value-based deduplication or unreliable-message retry.
bool CSOLWriteDamageNumber(int messageId, edict_s* recipient, CSOLDamageNumber message);
