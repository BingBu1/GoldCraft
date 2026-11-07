// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <span>
#include <string_view>

namespace csol::elitefx
{
struct Emitter
{
    std::array<float, 3> offset{}, velocity{};
    int owner{}, sprite{}; // burn5,6,7 are indices0,1,2.
    float interval{}, life{}, spread{}, size{}, growth{}, fade{}, alpha{};
    bool fireball{};
};
// Original TempEmit11493000 is a NUL string followed by typed little-endian
// GoldSrc fields. Only the elite fire-wall form is handled by this module.
inline bool DecodeFirewall(std::span<const std::uint8_t> bytes, Emitter& result)
{
    const auto zero = std::find(bytes.begin(), bytes.end(), 0);
    if (zero == bytes.end()) return false;
    const auto length = static_cast<std::size_t>(zero - bytes.begin());
    const std::string_view path(reinterpret_cast<const char*>(bytes.data()), length);
    int sprite = path == "sprites/burn5.spr" ? 0 : path == "sprites/burn6.spr" ? 1 : path == "sprites/burn7.spr" ? 2 : -1;
    //2 owner+12 coords+2 count+8 times+4 flags+8 bytes+28 floats.
    if (sprite < 0 || bytes.size() != length + 1 + 64) return false;
    std::size_t at = length + 1;
    const auto byte = [&] { return bytes[at++]; };
    const auto word = [&] { const unsigned lo = byte(); return lo | unsigned(byte()) << 8; };
    const auto integer = [&] { const auto lo = word(); return lo | std::uint32_t(word()) << 16; };
    const auto real = [&] { return std::bit_cast<float>(integer()); };
    const auto coord = [&] { return static_cast<float>(static_cast<std::int16_t>(word())) / 8; };
    Emitter next; next.sprite = sprite; next.owner = static_cast<std::int16_t>(word());
    for (auto& n : next.offset) n = coord();
    for (auto& n : next.velocity) n = coord();
    if (static_cast<std::int16_t>(word()) != -1) return false;
    next.interval = real(); next.life = real();
    if (integer() != 0x830 || byte() != 0 || byte() != 0 || byte() != 15 || byte() != 5) return false;
    next.alpha = byte();
    if (byte() != 255 || byte() != 255 || byte() != 255) return false;
    next.spread = real(); next.size = real(); const float gravity = real();
    next.fade = real(); next.growth = real(); const float minimum = real(), maximum = real();
    if (next.owner < 1 || next.interval != .04f || !std::isfinite(next.life) || next.life < 0 ||
        next.life > 3600 || next.spread < 70 || next.spread > 120 || !std::isfinite(next.spread) ||
        next.size < 80 || next.size > 100 || !std::isfinite(next.size) || gravity != 0 || next.fade != 5 ||
        next.growth < 2 || next.growth > 5 || !std::isfinite(next.growth) || minimum != 0 || maximum != 0 ||
        next.alpha != 150) return false;
    result = next; return true;
}
inline bool DecodeFireball(std::span<const std::uint8_t> bytes, int& owner)
{
    if (bytes.size() != 6 || bytes[0] != 20 || bytes[1] != 4) return false;
    std::uint32_t index{};
    for (unsigned i = 0; i < 4; ++i) index |= std::uint32_t(bytes[2 + i]) << (8 * i);
    if (index < 1 || index > 0x7fff) return false;
    owner = static_cast<int>(index); return true;
}
inline float Size(const Emitter& emitter, float elapsed)
{ return emitter.size + elapsed * (emitter.growth * 30.0f); }
inline float Alpha(const Emitter& emitter, float elapsed)
{
    return emitter.fade != 0 ? emitter.alpha - (emitter.fade * 30.0f) * elapsed :
        emitter.life > 0 ? emitter.alpha * (1.0f - elapsed / emitter.life) : 0;
}
}
