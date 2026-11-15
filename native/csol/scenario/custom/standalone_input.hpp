// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace csol::scenario::custom
{
// Original client f4145136, 02A59180: this is one integer, not kbutton's
// physical-key pair. Store before calling out; duplicate states fall through.
class StandaloneSprintEdge final
{
public:
    template<class Send> bool Dispatch(int state, Send&& send)
    {
        if (state == state_) return false;
        state_ = state;
        if (state == 1) { send("sprintON"); return true; }
        if (state == 0) { send("sprintOFF"); return true; }
        return false;
    }
    void Reset() noexcept { state_ = 0; }
    int State() const noexcept { return state_; }
private:
    int state_{};
};

// Host session adapter. Only an authoritative, complete movement owner may
// publish this grant. The stamina display alone never activates native input.
inline constexpr char StandaloneInputMessage[] = "CsoRun";
inline constexpr std::size_t StandaloneInputMessageSize = 14;
struct StandaloneInputGrant
{
    std::uint64_t life{};
    std::uint32_t revision{};
    bool enabled{};
};
inline std::optional<std::array<std::byte, StandaloneInputMessageSize>> EncodeStandaloneInput(
    StandaloneInputGrant value) noexcept
{
    if (!value.life || !value.revision) return {};
    std::array<std::byte, StandaloneInputMessageSize> bytes{};
    bytes[0] = std::byte{1}; bytes[1] = std::byte(value.enabled);
    for (unsigned i = 0; i != 8; ++i) bytes[2 + i] = std::byte(value.life >> (8 * i));
    for (unsigned i = 0; i != 4; ++i) bytes[10 + i] = std::byte(value.revision >> (8 * i));
    return bytes;
}
inline std::optional<StandaloneInputGrant> DecodeStandaloneInput(std::span<const std::byte> bytes) noexcept
{
    if (bytes.size() != StandaloneInputMessageSize || bytes[0] != std::byte{1} ||
        std::to_integer<unsigned>(bytes[1]) > 1) return {};
    StandaloneInputGrant value;
    value.enabled = bytes[1] != std::byte{};
    for (unsigned i = 0; i != 8; ++i) value.life |= std::to_integer<std::uint64_t>(bytes[2 + i]) << (8 * i);
    for (unsigned i = 0; i != 4; ++i) value.revision |= std::to_integer<std::uint32_t>(bytes[10 + i]) << (8 * i);
    return value.life && value.revision ? std::optional{value} : std::nullopt;
}
class StandaloneInputAuthority final
{
public:
    bool Receive(std::span<const std::byte> bytes) noexcept
    {
        const auto next = DecodeStandaloneInput(bytes);
        if (!next || (known_ && (next->life < value_.life || (next->life == value_.life &&
            (next->revision <= value_.revision || (retired_ && next->enabled)))))) return false;
        const bool sameLife = known_ && next->life == value_.life;
        retired_ = sameLife ? retired_ || !next->enabled : !next->enabled;
        value_ = *next; known_ = true;
        return true;
    }
    const StandaloneInputGrant* Current() const noexcept { return known_ ? &value_ : nullptr; }
    void Reset() noexcept { value_ = {}; known_ = retired_ = false; }
private:
    StandaloneInputGrant value_;
    bool known_{}, retired_{};
};
}
