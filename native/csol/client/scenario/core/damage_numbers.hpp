// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace csol::damage
{
inline constexpr char MessageName[] = "MPToCL";
inline constexpr std::uint8_t Opcode = 206;
inline constexpr std::size_t WireSize = 9;

enum class Style : std::uint8_t
{
    Normal, Dot, LegacyCritical, Text, Head, Miss, Heal
};
enum Flag : std::uint8_t { Back = 1, Critical = 2, HealIcon = 4 };
struct Message
{
    std::int16_t target{};
    std::uint8_t style{}, flags{};
    std::int32_t value{};
    bool operator==(const Message&) const = default;
};
enum class ParseResult { Unrelated, Malformed, Number };
ParseResult Decode(std::span<const std::byte> bytes, Message& message);

struct Anchor
{
    float x{}, y{}, z{};
    bool operator==(const Anchor&) const = default;
};
struct Animation
{
    float duration{}, skipTime{}, speed{};
};
struct ImageMetrics
{
    float glyphWidth{}, glyphHeight{};
};
// Original image-number measurement (0295C660). Dimensions are the original
// image object's logical GetSize result, not assumed GL allocation dimensions.
std::optional<ImageMetrics> MeasureDigits(unsigned imageWidth, unsigned imageHeight);
struct ImageStyle
{
    ImageMetrics image;
    float startScale{}, endScale{}, distanceRatio{};
};
struct Appearance
{
    int width{}, height{};
    std::uint8_t alpha{};
    float rise{};
};

struct Number
{
    Message message;
    Anchor anchor; // Capture once; later entity-index reuse cannot move this hit.
    Animation animation;
    double receivedAt{};
    float age{}, effectiveAge{}, supersededAtAge{};
    int offsetX{}, offsetY{};
    bool newest{true}, supersedeObserved{}, skipped{};
    std::array<char, 12> text{}; // signed int32 including INT_MIN and terminator
    std::uint8_t textLength{};
    std::string_view Text() const { return {text.data(), textLength}; }
};

// One instance per local recipient/session. The original panel vector appends
// accepted display messages and removes expired panels; no five-entry policy.
// A physical hit need not produce one message: the server owns that decision.
class Numbers
{
public:
    bool Push(Message message, Anchor anchor, Animation animation, double receivedAt, int offsetX = 0, int offsetY = 0);
    void Advance(double renderTime);
    void Reset();
    std::span<const Number> Entries() const { return entries_; }
private:
    std::vector<Number> entries_;
    double renderTime_{};
    bool hasRenderTime_{};
};

// Matches the recovered image-number scale/fade/rise path. Resource lookup,
// original image UVs, widget spacing, projection and adornments are separate.
std::optional<Appearance> Sample(const Number& number, const ImageStyle& style, Anchor camera);
}
