// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

namespace csol::wallstatus
{
inline constexpr char MessageName[] = "StatusValue";
inline constexpr int WallType = 3;
enum class ParseResult { Legacy, Extended, Malformed };
struct Field { std::uint8_t index{}; std::int32_t value{}; };

inline ParseResult Decode(std::span<const std::byte> bytes, Field& output)
{
    // Stock CS: BYTE index + SHORT value. CSO: BYTE index + LONG value.
    // Never send the extended form into stock's short/index decoder.
    if (bytes.size() == 3) return ParseResult::Legacy;
    if (bytes.size() != 5) return ParseResult::Malformed;
    Field field;
    field.index = std::to_integer<std::uint8_t>(bytes[0]);
    std::uint32_t bits{};
    for (unsigned i = 0; i < 4; ++i)
        bits |= std::uint32_t(std::to_integer<std::uint8_t>(bytes[i + 1])) << (8 * i);
    field.value = std::bit_cast<std::int32_t>(bits);
    // The original client has eight integer fields before its text buffer.
    // Its formatter has types 0..13; reject invalid discriminants explicitly.
    if (field.index >= 8 || (field.index == 0 && (field.value < 0 || field.value > 13)))
        return ParseResult::Malformed;
    output = field;
    return ParseResult::Extended;
}

class State
{
public:
    void Reset() { type_ = health_ = 0; }
    void Apply(Field field)
    {
        if (field.index == 0)
        {
            if (type_ != field.value) health_ = 0;
            type_ = field.value;
        }
        else if (field.index == 1) health_ = field.value;
    }
    bool Visible() const { return type_ == WallType && health_ > 0; }
    std::int32_t Health() const { return health_; }
private:
    std::int32_t type_{}, health_{};
};
}
