// SPDX-License-Identifier: GPL-3.0-or-later
#include "boss_health.hpp"
#include <algorithm>
#include <bit>
#include <cmath>

#pragma clang fp contract(off)
namespace csol::boss_health
{
namespace
{
unsigned Byte(std::byte value) { return std::to_integer<unsigned>(value); }
std::uint32_t Long(std::span<const std::byte> bytes, std::size_t at)
{ return Byte(bytes[at]) | Byte(bytes[at+1]) << 8 | Byte(bytes[at+2]) << 16 | Byte(bytes[at+3]) << 24; }
}
ParseResult Decode(std::span<const std::byte> bytes, Message& output)
{
    if (bytes.empty() || Byte(bytes[0]) != 27) return ParseResult::Unrelated;
    if (bytes.size() < 2) return ParseResult::Malformed;
    const auto subtype = Byte(bytes[1]);
    if (subtype != 13 && subtype != 27) return ParseResult::Unrelated;
    if (bytes.size() != 12) return ParseResult::Malformed;
    Message value;
    value.appearance = std::bit_cast<std::int16_t>(std::uint16_t(Byte(bytes[2]) | Byte(bytes[3]) << 8));
    value.entity = std::bit_cast<std::int32_t>(Long(bytes, 8));
    value.initialize = subtype == 13;
    if (value.initialize)
    {
        value.maximum = std::bit_cast<std::int32_t>(Long(bytes, 4));
        if (value.maximum <= 0) return ParseResult::Malformed;
        value.ratio = 1;
    }
    else
    {
        value.ratio = std::bit_cast<float>(Long(bytes, 4));
        if (!std::isfinite(value.ratio)) return ParseResult::Malformed;
    }
    if (value.entity < 0) return ParseResult::Malformed;
    output = value;
    return ParseResult::Accepted;
}
ParseResult DecodeAbilities(std::span<const std::byte> bytes, AbilityMessage& output)
{
    if (bytes.empty() || Byte(bytes[0]) != 27) return ParseResult::Unrelated;
    if (bytes.size() < 2) return ParseResult::Malformed;
    if (Byte(bytes[1]) != 28) return ParseResult::Unrelated;
    if (bytes.size() < 6) return ParseResult::Malformed;
    const unsigned appearance = Byte(bytes[2]) | Byte(bytes[3]) << 8;
    const unsigned count = Byte(bytes[4]) | Byte(bytes[5]) << 8;
    if (appearance > 0x7fff || count > AbilityNames.size() || bytes.size() != 6 + count * 2)
        return ParseResult::Malformed;
    AbilityMessage value{int(appearance), {}};
    value.abilities.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        const unsigned id = Byte(bytes[6 + i*2]) | Byte(bytes[7 + i*2]) << 8;
        if (id >= AbilityNames.size()) return ParseResult::Malformed;
        value.abilities.push_back(int(id));
    }
    output = std::move(value);
    return ParseResult::Accepted;
}
void State::ApplyAbilities(const AbilityMessage& message)
{
    if (message.appearance < 0 || message.appearance > 0x7fff || message.abilities.size() > AbilityNames.size() ||
        std::any_of(message.abilities.begin(), message.abilities.end(), [](int id)
            { return id < 0 || std::size_t(id) >= AbilityNames.size(); })) return;
    abilityAppearance_ = message.appearance;
    abilities_ = message.abilities;
}
void State::Apply(const Message& message)
{
    // Original 029CB090 keys entries by appearance, not entity index.
    if (message.appearance < 0) return;
    if (message.ratio <= 0)
    {
        if (abilityAppearance_ == message.appearance) { abilityAppearance_ = -1; abilities_.clear(); }
        entries_.erase(message.appearance);
        selected_ = entries_.empty() ? -1 : entries_.begin()->first;
        return;
    }
    auto [entry, inserted] = entries_.try_emplace(message.appearance);
    entry->second.entity = message.entity;
    entry->second.ratio = message.ratio;
    if (message.initialize) entry->second.maximum = message.maximum;
    if (inserted) selected_ = message.appearance;
}
const Entry* State::Current() const
{
    const auto found = entries_.find(selected_);
    return found == entries_.end() ? nullptr : &found->second;
}
Layout MakeLayout(int width, int height, float ratio)
{
    Layout result{};
    if (width <= 0 || height <= 0 || !std::isfinite(ratio)) return result;
    // Original main panel: 029C97F0, caller y=11 and horizontal offset=0.
    const float scale = float(height) / 768.0f;
    const float backgroundHeight = float(height) * 0.1380208283662796f;
    const float backgroundWidth = backgroundHeight * 5.4528303146362305f;
    const float left = (float(width) - backgroundWidth) * 0.5f - 23.0f;
    const int x = int(left), y = 11;
    const int w = int(backgroundWidth), h = int(backgroundHeight);
    const int leftWidth = int(backgroundWidth * 0.23183390498161316f);
    const int rightWidth = int(backgroundWidth * 0.022491348907351494f);
    result.background = {{{x,y,leftWidth,h},{x+leftWidth,y,w-leftWidth-rightWidth,h},
        {x+w-rightWidth,y,rightWidth,h}}};
    const int fullWidth = int(scale * 560.0f);
    // Reject non-finite packets above, and bound oversized server ratios to
    // the panel for host safety. Healthy original packets stay in [0,1].
    result.gauge = {int(left + scale * 9.0f), int(11.0f + scale * 91.0f),
        int(float(fullWidth) * std::clamp(ratio, 0.0f, 1.0f)), int(scale * 6.0f)};
    result.color = double(ratio) <= 0.2 ? std::array{255,47,45,255} : std::array{255,206,67,255};
    return result;
}
}
