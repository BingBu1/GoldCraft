// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <span>

namespace csol::skillfx
{
struct ScreenMessage
{
    int status{};
    float duration{};
    std::array<std::uint8_t, 4> color{255,255,255,255};
    bool overlay{};
    bool transient{};
    bool clearStatus{};
};
enum class ScreenParse { Unrelated, Malformed, Accepted };
inline const char* StatusImage(int status)
{
    switch (status)
    {
    case 27: return "resource/ZBSVER2/buffclassskill_heal.tga";
    case 28: return "resource/ZBSVER2/buffclassskill_damage.tga";
    case 29: return "resource/ZBSVER2/buffclassskill_buffup.tga";
    case 30: return "resource/ZBSVER2/buffclassskill_shield.tga";
    case 31: return "resource/ZBSVER2/buffclassskill_maxhealth.tga";
    case 40: return "resource/ZBSVER2/buffclassskill_invincible.tga";
    case 45: return "resource/ZBSVER2/buffclassskill_damage_heal.tga";
    case 49: return "resource/ZBSVER2/buffclassskill_damage_shield.tga";
    default: return nullptr;
    }
}
inline const char* TransientImage(int status)
{
    switch (status)
    {
    case 46: return "resource/ZBSVER2/reflect.tga";
    case 47: return "resource/ZBSVER2/reflect_damage.tga";
    default: return nullptr;
    }
}
inline ScreenParse DecodeScreen(std::span<const std::uint8_t> bytes, ScreenMessage& output)
{
    if (!bytes.empty() && bytes[0] == 0xCF)
    {
        if (bytes.size() != 1 && bytes.size() != 3) return ScreenParse::Malformed;
        // Original02816E0F ignores the optional status short and clears the
        // single status slot. It does not clear the independent0x50 layers.
        ScreenMessage value; value.clearStatus = true; output = value;
        return ScreenParse::Accepted;
    }
    if (bytes.empty() || (bytes[0] != 0x36 && bytes[0] != 0x50 && bytes[0] != 0x51)) return ScreenParse::Unrelated;
    if (bytes.size() != 10) return ScreenParse::Malformed;
    ScreenMessage value;
    value.overlay = bytes[0] == 0x36;
    value.transient = bytes[0] == 0x50;
    const bool rgba = value.overlay || value.transient;
    value.status = rgba ? bytes[1] : int(bytes[1]) | int(bytes[2]) << 8;
    if (value.overlay ? value.status != 0 :
        value.transient ? !TransientImage(value.status) : !StatusImage(value.status)) return ScreenParse::Unrelated;
    const unsigned offset = rgba ? 2 : 3;
    std::uint32_t bits{};
    for (unsigned i = 0; i < 4; ++i) bits |= std::uint32_t(bytes[offset + i]) << (8 * i);
    value.duration = std::bit_cast<float>(bits);
    if (!std::isfinite(value.duration) || value.duration < -1 || value.duration > 86400) return ScreenParse::Malformed;
    for (unsigned i = 0; i < (rgba ? 4u : 3u); ++i) value.color[i] = bytes[offset + 4 + i];
    output = value; return ScreenParse::Accepted;
}
struct EdgeFrame { int alpha{}, offset{}, width{}, bottom{}; };
inline EdgeFrame SkillEdges(float elapsed, int height)
{
    if (!std::isfinite(elapsed) || elapsed < 0 || height < 1) return {};
    // Original client02A9CC08: repeat every second, rise15 pixels and
    // fade during its first/last0.2s. The right side mirrors the same image.
    const float phase = elapsed - std::floor(elapsed);
    const float alpha = phase < .2f ? phase / .2f * 255 :
        phase > .8f ? 255 - (phase - .8f) / .2f * 255 : 255;
    const float offset = -phase * 15;
    return {static_cast<int>(alpha), static_cast<int>(offset),
        static_cast<int>((height * (1.0f / 256.0f)) * 96),
        static_cast<int>(height + offset)};
}
inline int OverlayAlpha(float now)
{
    if (!std::isfinite(now) || now < 0) return 0;
    //02A9C364 uses time200 modulo200, then150+triangle(0..100).
    const int phase = static_cast<int>(std::fmod(now * 200.0f, 200.0f));
    return 150 + (phase <= 100 ? phase : 200 - phase);
}
}
