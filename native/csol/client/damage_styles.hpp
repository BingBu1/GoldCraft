// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "damage_numbers.hpp"
#include <string>

namespace csol::damage
{
inline constexpr char StyleResource[] = "resource/FloatingDamage/FloatingDamageInfo.jsoo";
struct Part
{
    unsigned type{};
    std::string resource;
    float width{}, height{}, start{}, end{}, distance{}, interval{};
    int x{}, y{}, horizontal{}, vertical{};
    bool relative{};
};
struct Skin
{
    int id{}, itemId{}, randomWidth{}, randomHeight{};
    std::string preview;
    Animation animation;
    std::vector<Part> styles, icons;
    const Part* Find(Style style) const;
};
// Consumes the original encoded/decoded configuration, never a font list.
std::vector<Skin> ReadSkins(std::span<const std::byte> resource);
struct TextureMetrics { unsigned width{}, height{}, paddedWidth{}, paddedHeight{}; };
struct Quad { float x{}, y{}, width{}, height{}, u0{}, v0{}, u1{}, v1{}; std::uint8_t alpha{}; };
// The original widget uses measured glyphWidth-1 and an additional 1px UV
// stride, independently of the configured screen-space ContentsInterval.
std::size_t LayoutDigits(const Number& number, const Part& part, TextureMetrics image,
    Anchor camera, float x, float y, std::span<Quad> output);
std::optional<Quad> LayoutMiss(const Number& number, const Part& part, TextureMetrics image,
    Anchor camera, float x, float y);
std::optional<Quad> LayoutIcon(const Number& number, const Part& icon, const Part& digits,
    Anchor camera, std::span<const Quad> bounds);
}
