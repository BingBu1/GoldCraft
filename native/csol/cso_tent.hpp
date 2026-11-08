// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>

// CSO engine-private temp entities 133/137/138. Original HW CL_ParseTEnt
// 37850920 dispatches them through byte table 37855950 to 378546ED (133),
// 3785289D (137) and 37854871 (138); stock GoldSrc has no such services.
// The scenario carries them inside CsoMonFX with the original field order:
// coordinates as 1/8-unit shorts and angles as the original 256-step byte.
namespace csol::cso_tent
{
inline constexpr std::uint8_t ModelTent = 6;   // TE133
inline constexpr std::uint8_t SpriteTent = 7;  // TE137
inline constexpr std::uint8_t ModelTentEx = 8; // TE138
inline constexpr std::uint8_t ExplosionTent = 9; // TE3 with CSO sound selector
inline constexpr std::size_t ExplosionTentSize = 13;
inline constexpr std::uint8_t MooncakeSound = 2;
inline constexpr char MooncakeSoundPath[] = "weapons/mooncake_exp.wav";
inline constexpr std::size_t ModelTentSize = 17, SpriteTentSize = 12, ModelTentExSize = 20;

struct Model
{
    bool extended{};                 // TE138: all three angles and fade speed
    std::array<float, 3> origin{};
    std::array<float, 3> angles{};   // TE133 transmits only angles[1]
    std::uint16_t model{};
    std::uint8_t life{}, framerate{}, fade{}, renderamt{}, rendermode{};
    std::int16_t follow{-1};
    std::uint8_t fadeSpeed{};        // TE138 only
};

struct Sprite
{
    std::array<float, 3> origin{};
    std::uint16_t model{};
    std::uint8_t scale{}, brightness{}, framerate{};
};

// Only the evidenced extra sound selector is admitted. Other CSO selectors
// require their own resource-consumer evidence before extending this adapter.
struct Explosion
{
    std::array<float, 3> origin{};
    std::uint16_t model{};
    std::uint8_t scale{}, framerate{}, flags{}, sound{MooncakeSound};
};

namespace detail
{
// ReHLDS MSG_WriteCoord/WriteAngle truncation; HW 378657A0 also uses
// (int)(angle*256/360) as one byte.
inline bool Coord(float value, std::int16_t& out)
{
    if (!std::isfinite(value)) return false;
    const float scaled = value * 8.0f;
    if (!(scaled > -32769.0f && scaled < 32768.0f)) return false;
    out = static_cast<std::int16_t>(static_cast<int>(scaled));
    return true;
}
inline bool Angle(float value, std::uint8_t& out)
{
    const float scaled = value * 256.0f / 360.0f;
    // INT32_MAX rounds up to 2^31 as float, so the upper bound is exclusive.
    // This also rejects NaN, infinity and overflow in the scale operation.
    if (!(scaled >= -2147483648.0f && scaled < 2147483648.0f)) return false;
    out = static_cast<std::uint8_t>(static_cast<std::int32_t>(scaled) & 255);
    return true;
}
struct Writer
{
    std::span<std::uint8_t> bytes;
    std::size_t at{};
    void Byte(std::uint8_t value) { bytes[at++] = value; }
    void Word(std::uint16_t value) { Byte(static_cast<std::uint8_t>(value)); Byte(static_cast<std::uint8_t>(value >> 8)); }
};
struct Reader
{
    std::span<const std::uint8_t> bytes;
    std::size_t at{};
    std::uint8_t Byte() { return bytes[at++]; }
    std::uint16_t Word() { const auto low = Byte(); return static_cast<std::uint16_t>(low | (Byte() << 8)); }
    float Coord() { return float(std::bit_cast<std::int16_t>(Word())) * .125f; }
    // HW 37864AB0: signed byte * 360/256.
    float Angle() { return float(std::bit_cast<std::int8_t>(Byte())) * (360.0f / 256.0f); }
};
}

inline std::optional<std::array<std::uint8_t, ModelTentExSize>> Write(const Model& tent, std::size_t& size)
{
    std::array<std::uint8_t, ModelTentExSize> bytes{};
    std::array<std::int16_t, 3> coord{};
    for (unsigned i = 0; i < 3; ++i) if (!detail::Coord(tent.origin[i], coord[i])) return std::nullopt;
    if (!tent.extended && (tent.angles[0] != 0 || tent.angles[2] != 0)) return std::nullopt;
    std::array<std::uint8_t, 3> angles{};
    for (unsigned i = 0; i < 3; ++i)
        if (!detail::Angle(tent.angles[i], angles[i])) return std::nullopt;
    detail::Writer out{bytes};
    out.Byte(tent.extended ? ModelTentEx : ModelTent);
    for (auto value : coord) out.Word(std::bit_cast<std::uint16_t>(value));
    if (tent.extended) for (auto angle : angles) out.Byte(angle);
    else out.Byte(angles[1]);
    out.Word(tent.model);
    out.Byte(tent.life);
    out.Byte(tent.framerate);
    out.Byte(tent.fade);
    out.Byte(tent.renderamt);
    out.Byte(tent.rendermode);
    out.Word(std::bit_cast<std::uint16_t>(tent.follow));
    if (tent.extended) out.Byte(tent.fadeSpeed);
    size = out.at;
    return bytes;
}

inline std::optional<std::array<std::uint8_t, SpriteTentSize>> Write(const Sprite& tent)
{
    std::array<std::uint8_t, SpriteTentSize> bytes{};
    std::array<std::int16_t, 3> coord{};
    for (unsigned i = 0; i < 3; ++i) if (!detail::Coord(tent.origin[i], coord[i])) return std::nullopt;
    detail::Writer out{bytes};
    out.Byte(SpriteTent);
    for (auto value : coord) out.Word(std::bit_cast<std::uint16_t>(value));
    out.Word(tent.model);
    out.Byte(tent.scale);
    out.Byte(tent.brightness);
    out.Byte(tent.framerate);
    return bytes;
}

inline std::optional<Model> ReadModel(std::span<const std::uint8_t> bytes)
{
    if (bytes.empty()) return std::nullopt;
    const bool extended = bytes[0] == ModelTentEx;
    if ((bytes[0] != ModelTent && !extended) || bytes.size() != (extended ? ModelTentExSize : ModelTentSize))
        return std::nullopt;
    detail::Reader in{bytes, 1};
    Model tent;
    tent.extended = extended;
    for (auto& value : tent.origin) value = in.Coord();
    if (extended) for (auto& angle : tent.angles) angle = in.Angle();
    else tent.angles[1] = in.Angle();
    tent.model = in.Word();
    tent.life = in.Byte();
    tent.framerate = in.Byte();
    tent.fade = in.Byte();
    tent.renderamt = in.Byte();
    tent.rendermode = in.Byte();
    tent.follow = std::bit_cast<std::int16_t>(in.Word());
    if (extended) tent.fadeSpeed = in.Byte();
    return tent;
}

inline std::optional<Sprite> ReadSprite(std::span<const std::uint8_t> bytes)
{
    if (bytes.size() != SpriteTentSize || bytes[0] != SpriteTent) return std::nullopt;
    detail::Reader in{bytes, 1};
    Sprite tent;
    for (auto& value : tent.origin) value = in.Coord();
    tent.model = in.Word();
    tent.scale = in.Byte();
    tent.brightness = in.Byte();
    tent.framerate = in.Byte();
    return tent;
}
inline std::optional<std::array<std::uint8_t, ExplosionTentSize>> Write(const Explosion& tent)
{
    if (!tent.model || tent.model > 32767 || tent.sound != MooncakeSound) return std::nullopt;
    std::array<std::uint8_t, ExplosionTentSize> bytes{};
    std::array<std::int16_t, 3> coord{};
    for (unsigned i = 0; i < 3; ++i) if (!detail::Coord(tent.origin[i], coord[i])) return std::nullopt;
    detail::Writer out{bytes};
    out.Byte(ExplosionTent);
    for (auto value : coord) out.Word(std::bit_cast<std::uint16_t>(value));
    out.Word(tent.model);
    out.Byte(tent.scale); out.Byte(tent.framerate); out.Byte(tent.flags); out.Byte(tent.sound);
    return bytes;
}

inline std::optional<Explosion> ReadExplosion(std::span<const std::uint8_t> bytes)
{
    if (bytes.size() != ExplosionTentSize || bytes[0] != ExplosionTent) return std::nullopt;
    detail::Reader in{bytes, 1};
    Explosion tent;
    for (auto& value : tent.origin) value = in.Coord();
    tent.model = in.Word();
    tent.scale = in.Byte(); tent.framerate = in.Byte(); tent.flags = in.Byte(); tent.sound = in.Byte();
    if (!tent.model || tent.model > 32767 || tent.sound != MooncakeSound) return std::nullopt;
    return tent;
}

}
