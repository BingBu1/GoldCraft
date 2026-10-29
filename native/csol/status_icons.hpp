// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace csol::status
{
inline constexpr char MessageName[] = "MPToCL2";
inline constexpr char ProtectionImage[] = "resource/Hud/Skillicon/hud_playerstatus_7.tga";
inline constexpr std::uint8_t Opcode = 0x40;
inline constexpr std::size_t Capacity = 30;

enum class Operation { Add, Remove, Clear };
struct Message
{
    Operation operation{};
    std::int32_t id{}, type{};
    std::uint8_t style{};
    float remaining{};
};
enum class ParseResult { Unrelated, Malformed, Parsed };

inline ParseResult Decode(std::span<const std::byte> bytes, Message& output)
{
    if (bytes.empty() || std::to_integer<unsigned>(bytes[0]) != Opcode) return ParseResult::Unrelated;
    if (bytes.size() < 2) return ParseResult::Malformed;
    const unsigned op = std::to_integer<unsigned>(bytes[1]);
    if (op > 2) return ParseResult::Unrelated;
    if (bytes.size() != (op == 0 ? 15u : op == 1 ? 6u : 2u)) return ParseResult::Malformed;
    const auto word = [&](std::size_t offset)
    {
        std::uint32_t value{};
        for (unsigned i = 0; i < 4; ++i) value |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (8 * i);
        return value;
    };
    Message parsed;
    parsed.operation = static_cast<Operation>(op);
    if (op != 2) parsed.id = std::bit_cast<std::int32_t>(word(2));
    if (op == 0)
    {
        parsed.style = std::to_integer<std::uint8_t>(bytes[6]);
        parsed.type = std::bit_cast<std::int32_t>(word(7));
        parsed.remaining = std::bit_cast<float>(word(11));
        if (!std::isfinite(parsed.remaining)) return ParseResult::Malformed;
    }
    output = parsed;
    return ParseResult::Parsed;
}

struct Icon
{
    std::int32_t id{};
    double total{-1}, remaining{-1};
    bool visible{};
};
struct Quad
{
    float left{}, top{}, right{}, bottom{}, u1{}, v0{}, v1{}, alpha{};
};

// The verified style0/type7 consumer. Other types keep their downstream path
// until their resource and secondary-consumer contracts are implemented.
class ProtectionIcons
{
public:
    bool Add(const Message& message, float option)
    {
        if (message.operation != Operation::Add || message.style != 0 || message.type != 7 ||
            !(option > 0.0001f) || !std::isfinite(message.remaining)) return false;
        auto slot = std::find_if(m_icons.begin(), m_icons.end(), [](const Icon& icon) { return !icon.visible; });
        if (slot == m_icons.end()) return false;
        *slot = {};
        slot->id = message.id;
        // FLT_MAX preserves the original reset's -1. Zero expires immediately
        // as a defined host adaptation of the original NaN/Inf removal path.
        if (message.remaining < std::numeric_limits<float>::max())
            slot->total = slot->remaining = message.remaining;
        slot->visible = message.remaining != 0;
        return slot->visible;
    }
    void Remove(std::int32_t id)
    {
        const auto found = std::find_if(m_icons.begin(), m_icons.end(),
            [id](const Icon& icon) { return icon.visible && icon.id == id; });
        if (found != m_icons.end()) *found = {};
    }
    void Clear() { m_icons = {}; }
    void ResetSession() { Clear(); m_hasTime = false; }
    void Advance(float time)
    {
        if (!std::isfinite(time)) return;
        const double elapsed = m_hasTime ? std::max(0.0, double(time) - double(m_time)) : 0.0;
        m_time = time;
        m_hasTime = true;
        for (auto& icon : m_icons)
        {
            if (!icon.visible || icon.total < 0) continue;
            icon.remaining -= elapsed;
            const double ratio = icon.remaining / icon.total;
            if (!(ratio >= 0 && ratio <= 1)) icon = {};
        }
    }
    std::size_t VisibleCount() const
    {
        return std::count_if(m_icons.begin(), m_icons.end(), [](const Icon& icon) { return icon.visible; });
    }
    const std::array<Icon, Capacity>& Items() const { return m_icons; }
    std::size_t Layout(int screenWidth, int screenHeight, unsigned imageWidth, unsigned imageHeight,
        std::span<Quad, Capacity * 2> quads) const
    {
        if (screenWidth <= 0 || screenHeight <= 0 || !imageWidth || !imageHeight) return 0;
        const int parentX = (screenWidth - 354) / 2, parentY = screenHeight - 100;
        const int width = int(std::min(30u, imageWidth)), height = int(std::min(30u, imageHeight));
        std::size_t count{}, index{};
        for (const auto& icon : m_icons)
        {
            if (!icon.visible) continue;
            const int left = parentX + 36 * int(index % 10), top = parentY + 72 - 36 * int(index / 10);
            ++index;
            const auto slice = [&](int from, int to, float alpha)
            {
                from = std::clamp(from, 0, height);
                to = std::clamp(to, 0, height);
                if (from >= to) return;
                quads[count++] = {float(left), float(top + from), float(left + width), float(top + to),
                    float(width) / imageWidth, float(from) / imageHeight, float(to) / imageHeight, alpha};
            };
            if (icon.total < 0) slice(0, 30, 1);
            else if (icon.total > 0)
            {
                const double ratio = icon.remaining / icon.total;
                if (!(ratio >= 0 && ratio <= 1)) continue;
                const int dark = int((1 - ratio) * 30), bright = int(ratio * 30);
                slice(0, dark, 70.0f / 255.0f);
                slice(dark, dark + bright, 1);
            }
        }
        return count;
    }
private:
    std::array<Icon, Capacity> m_icons{};
    float m_time{};
    bool m_hasTime{};
};
}
