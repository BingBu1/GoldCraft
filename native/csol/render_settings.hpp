// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <optional>

// Server-forced client presentation settings (host adaptation, not CSO data).
// The original CSO engine blends translucent sprites and TriAPI particles in
// gamma space (hw 378EDD90 Color4f r*a/g*a/b*a, 378EE07D TransAdd ONE,ONE).
// MetaHookSv Renderer r_gamma_blend 0 linearizes them first, which makes
// low-alpha additive effects such as the Balrog11 trail far darker.
namespace csol::render
{
inline constexpr char Message[] = "CsolRender";
inline constexpr int MessageBytes = 1;
inline constexpr std::uint8_t NoForce = 255;

// Server cvar csol_gamma_blend: -1 leaves clients alone, 0/1 force the value.
inline std::optional<std::uint8_t> EncodeGammaBlend(float value)
{
    if (value == -1.0f) return NoForce;
    if (value == 0.0f || value == 1.0f) return static_cast<std::uint8_t>(value);
    return {};
}
// Returns -1 for "not forced", 0/1 for a forced value, nothing for bad bytes.
inline std::optional<int> DecodeGammaBlend(std::uint8_t value)
{
    if (value == NoForce) return -1;
    if (value <= 1) return int(value);
    return {};
}
}
