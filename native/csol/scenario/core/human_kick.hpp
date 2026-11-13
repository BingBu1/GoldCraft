// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace csol::scenario::human_kick
{
// MP39f962f3: 11486710 cooldown; 105A2020 range initialization;
// 11486E71..11486F82 impulse; 114E2EC4 animation-completion recovery.
// The human requested multi-target scenario damage15 and the F-key hint.
inline constexpr float BaseDamage = 15.0f;
inline constexpr float Cooldown = 1.1f;
inline constexpr float Reach = 2.8f * 39.369998931884766f;
inline constexpr float StaggerDuration = 1.0f;
inline constexpr float HorizontalForce = 500.0f;
inline constexpr float VerticalForce = 300.0f;
inline constexpr float PlayerFrameRate = 360.0f;
inline constexpr char FootModel[] = "models/v_foot.mdl";
inline constexpr char SwingSound[] = "zombi/z4/all/kick.wav";
inline constexpr std::array HitSounds{
    "zombi/z4/all/kick_metal1.wav", "zombi/z4/all/kick_metal2.wav",
    "zombi/z4/all/kick_wood1.wav", "zombi/z4/all/kick_wood2.wav",
    "zombi/z4/all/kick_stone1.wav", "zombi/z4/all/kick_stone2.wav"};

// A host message, not a copied CSO ABI or an authority claim from the client.
inline constexpr char MessageName[] = "CsoKick";
inline constexpr std::uint8_t Version = 1;
inline constexpr std::size_t MessageSize = 10;
// ResumePhase is emitted only to clients advertising csol_kick_phase1.
enum Flag : std::uint8_t { Enabled = 1, Ready = 2, Active = 4, ResumePhase = 8 };
struct Snapshot
{
    std::uint32_t life{};
    std::uint8_t flags{};
    float remaining{};
};
inline bool Valid(const Snapshot& value)
{
    return value.life && !(value.flags & ~(Enabled | Ready | Active | ResumePhase)) &&
        (!(value.flags & (Ready | Active | ResumePhase)) || (value.flags & Enabled)) &&
        !((value.flags & Ready) && (value.flags & Active)) &&
        !((value.flags & Active) && (value.flags & ResumePhase)) &&
        std::isfinite(value.remaining) && value.remaining >= 0 &&
        value.remaining <= Cooldown && (!(value.flags & Ready) || value.remaining == 0);
}
inline std::optional<std::array<std::byte, MessageSize>> Encode(const Snapshot& value)
{
    if (!Valid(value)) return {};
    std::array<std::byte, MessageSize> bytes{};
    bytes[0] = std::byte{Version};
    bytes[1] = std::byte{value.flags};
    const auto remaining = std::bit_cast<std::uint32_t>(value.remaining);
    for (unsigned i = 0; i < 4; ++i)
    {
        bytes[2 + i] = std::byte((value.life >> (i * 8)) & 255);
        bytes[6 + i] = std::byte((remaining >> (i * 8)) & 255);
    }
    return bytes;
}
inline std::optional<Snapshot> Decode(std::span<const std::byte> bytes)
{
    if (bytes.size() != MessageSize || bytes[0] != std::byte{Version}) return {};
    Snapshot value;
    value.flags = std::to_integer<std::uint8_t>(bytes[1]);
    std::uint32_t remaining{};
    for (unsigned i = 0; i < 4; ++i)
    {
        value.life |= std::uint32_t(std::to_integer<unsigned>(bytes[2 + i])) << (i * 8);
        remaining |= std::uint32_t(std::to_integer<unsigned>(bytes[6 + i])) << (i * 8);
    }
    value.remaining = std::bit_cast<float>(remaining);
    return Valid(value) ? std::optional{value} : std::nullopt;
}

class Clock
{
public:
    bool ReadyAt(float now) const
    { return std::isfinite(now) && !active_ && last_ + Cooldown < now; }
    bool Begin(float now)
    {
        if (!ReadyAt(now)) return false;
        last_ = now;
        active_ = true;
        emergency_ = false;
        return true;
    }
    // User's emergency rule: stamina owns admission and an active action is
    // exclusive; no additional post-action cooldown blocks a second kick.
    bool BeginEmergency(float now)
    {
        if (!std::isfinite(now) || active_) return false;
        last_ = now; active_ = emergency_ = true;
        return true;
    }
    void Finish() { active_ = false; }
    bool ActiveNow() const { return active_; }
    float Remaining(float now) const
    {
        if (emergency_) return 0;
        if (!std::isfinite(now)) return Cooldown;
        const float remaining = last_ + Cooldown - now;
        return remaining > 0 ? (remaining < Cooldown ? remaining : Cooldown) : 0;
    }
private:
    float last_{};
    bool active_{}, emergency_{};
};
}
