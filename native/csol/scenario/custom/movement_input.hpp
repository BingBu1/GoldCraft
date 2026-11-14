// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>

namespace csol::scenario::custom
{
inline constexpr char MovementInputMessage[] = "CsoInput";
inline constexpr std::size_t MovementInputMessageSize = 26;
struct MovementInputPolicy
{
    std::uint64_t life{};
    std::uint32_t revision{};
    bool enabled{};
    // Authoritative directional references, not final movement speed. The
    // untouched native up axis still participates in the real PM raw3D cap.
    std::array<float, 3> axes{}; // forward/back/side
};
inline bool Valid(const MovementInputPolicy& value) noexcept
{
    if (!value.life || !value.revision) return false;
    for (const float axis : value.axes)
        if (!std::isfinite(axis) || (value.enabled ? axis <= 0 || axis > 10000 : axis != 0)) return false;
    return true;
}
inline std::optional<std::array<std::byte, MovementInputMessageSize>> EncodeMovementInput(const MovementInputPolicy& value) noexcept
{
    if (!Valid(value)) return {};
    std::array<std::byte, MovementInputMessageSize> bytes{};
    bytes[0] = std::byte{1}; bytes[1] = std::byte(value.enabled);
    for (unsigned i = 0; i != 4; ++i) bytes[2 + i] = std::byte((value.revision >> (8 * i)) & 255);
    for (unsigned i = 0; i != 8; ++i) bytes[6 + i] = std::byte((value.life >> (8 * i)) & 255);
    for (unsigned axis = 0; axis != 3; ++axis)
    {
        const auto word = std::bit_cast<std::uint32_t>(value.axes[axis]);
        for (unsigned i = 0; i != 4; ++i) bytes[14 + 4 * axis + i] = std::byte((word >> (8 * i)) & 255);
    }
    return bytes;
}
inline std::optional<MovementInputPolicy> DecodeMovementInput(std::span<const std::byte> bytes) noexcept
{
    if (bytes.size() != MovementInputMessageSize || bytes[0] != std::byte{1} ||
        std::to_integer<unsigned>(bytes[1]) > 1) return {};
    MovementInputPolicy value;
    value.enabled = bytes[1] != std::byte{};
    for (unsigned i = 0; i != 4; ++i) value.revision |= std::to_integer<std::uint32_t>(bytes[2 + i]) << (8 * i);
    for (unsigned i = 0; i != 8; ++i) value.life |= std::to_integer<std::uint64_t>(bytes[6 + i]) << (8 * i);
    for (unsigned axis = 0; axis != 3; ++axis)
    {
        std::uint32_t word{};
        for (unsigned i = 0; i != 4; ++i) word |= std::to_integer<std::uint32_t>(bytes[14 + 4 * axis + i]) << (8 * i);
        value.axes[axis] = std::bit_cast<float>(word);
    }
    return Valid(value) ? std::optional{value} : std::nullopt;
}
class MovementInputAuthority final
{
public:
    bool Receive(std::span<const std::byte> bytes) noexcept
    {
        const auto value = DecodeMovementInput(bytes);
        if (!value || (known_ && (value->life < value_.life ||
            !std::uint32_t(value->revision - value_.revision) ||
            std::uint32_t(value->revision - value_.revision) >= 0x80000000u ||
            (retired_ && value->life == value_.life && value->enabled)))) return false;
        retired_ = known_ && value->life == value_.life && (retired_ || (value_.enabled && !value->enabled));
        value_ = *value; known_ = true; return true;
    }
    const MovementInputPolicy* Current() const noexcept { return known_ ? &value_ : nullptr; }
    void Reset() noexcept { value_ = {}; known_ = retired_ = false; }
private:
    MovementInputPolicy value_;
    bool known_{}, retired_{};
};

// Match native kbutton's two-key ownership; callbacks themselves are always
// chained so this observer cannot replace ordinary CS +speed behavior.
class SprintKeys final
{
public:
    void Down(int key) noexcept
    {
        if (!key || keys_[0] == key || keys_[1] == key) return;
        if (!keys_[0]) keys_[0] = key;
        else if (!keys_[1]) keys_[1] = key;
    }
    void Up(int key) noexcept
    {
        if (key == -1) { Reset(); return; }
        for (auto& current : keys_) if (current == key) current = 0;
    }
    bool Held() const noexcept { return keys_[0] || keys_[1]; }
    void Reset() noexcept { keys_ = {}; }
private:
    std::array<int, 2> keys_{};
};
}
