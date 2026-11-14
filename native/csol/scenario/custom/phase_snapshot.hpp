// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "phase.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <span>

namespace csol::scenario::custom
{
inline constexpr char PhaseMessage[] = "CsoTime";
inline constexpr std::size_t PhaseMessageSize = 19;
struct PhaseSnapshot
{
    Phase phase{Phase::Ended};
    bool preparing{};
    std::uint32_t revision{};
    std::uint64_t challenge{};
    float remaining{};
};
inline bool Valid(const PhaseSnapshot& value) noexcept
{
    return value.revision && value.challenge && value.phase <= Phase::Ended &&
        (!value.preparing || value.phase == Phase::Noon) && std::isfinite(value.remaining) &&
        value.remaining >= 0 && value.remaining <= 86400 &&
        (value.phase < Phase::Night || (!value.preparing && value.remaining == 0));
}
inline std::optional<std::array<std::byte, PhaseMessageSize>> EncodePhase(const PhaseSnapshot& value) noexcept
{
    if (!Valid(value)) return {};
    std::array<std::byte, PhaseMessageSize> bytes{};
    bytes[0] = std::byte{1}; bytes[1] = std::byte(value.phase); bytes[2] = std::byte(value.preparing);
    for (unsigned i=0;i<4;++i) bytes[3+i]=std::byte((value.revision>>(8*i))&255);
    for (unsigned i=0;i<8;++i) bytes[7+i]=std::byte((value.challenge>>(8*i))&255);
    const auto remaining=std::bit_cast<std::uint32_t>(value.remaining);
    for (unsigned i=0;i<4;++i) bytes[15+i]=std::byte((remaining>>(8*i))&255);
    return bytes;
}
inline std::optional<PhaseSnapshot> DecodePhase(std::span<const std::byte> bytes) noexcept
{
    if (bytes.size()!=PhaseMessageSize || bytes[0]!=std::byte{1} ||
        std::to_integer<unsigned>(bytes[1])>unsigned(Phase::Ended) ||
        std::to_integer<unsigned>(bytes[2])>1) return {};
    PhaseSnapshot value;
    value.phase=Phase(std::to_integer<unsigned>(bytes[1])); value.preparing=bytes[2]!=std::byte{};
    std::uint32_t remaining{};
    for(unsigned i=0;i<4;++i) value.revision|=std::to_integer<std::uint32_t>(bytes[3+i])<<(8*i);
    for(unsigned i=0;i<8;++i) value.challenge|=std::to_integer<std::uint64_t>(bytes[7+i])<<(8*i);
    for(unsigned i=0;i<4;++i) remaining|=std::to_integer<std::uint32_t>(bytes[15+i])<<(8*i);
    value.remaining=std::bit_cast<float>(remaining);
    return Valid(value)?std::optional{value}:std::nullopt;
}
class PhasePresentation final
{
public:
    bool Receive(std::span<const std::byte> bytes,double now) noexcept
    {
        const auto next=DecodePhase(bytes);
        if (!next || !std::isfinite(now) || now<0 || (known_ && now<received_)) return false;
        if (known_)
        {
            const auto delta=next->revision-value_.revision;
            if (!delta || delta>=0x80000000u || next->challenge<value_.challenge ||
                (next->challenge==value_.challenge && (next->phase<value_.phase ||
                    (next->preparing && !value_.preparing)))) return false;
        }
        value_=*next;received_=now;known_=true;return true;
    }
    std::optional<PhaseSnapshot> Sample(double now) const noexcept
    {
        if (!known_ || !std::isfinite(now) || now<received_) return {};
        auto result=value_;
        if (!result.preparing && result.phase<Phase::Night)
            result.remaining=static_cast<float>(std::max(0.0,double(result.remaining)-(now-received_)));
        // Reaching zero never predicts the next phase. Only the server owns it.
        return result;
    }
    void Reset() noexcept {value_={};received_=0;known_=false;}
private:
    PhaseSnapshot value_;
    double received_{};
    bool known_{};
};
struct PhaseHudLayout {int x{},y{},width{},height{},icon{},padding{},fontHeight{};};
inline PhaseHudLayout PhaseLayout(int width,int height) noexcept
{
    if (width<320 || height<240 || width>16384 || height>16384) return {};
    const float scale=std::clamp(float(height)/768.0f,.8f,2.0f);
    const int padding=std::max(6,int(10*scale));
    const int panelWidth=std::min(width-2*padding,int(188*scale));
    const int panelHeight=int(52*scale);
    // Below the existing original Boss panel, clear of bottom skills/ammo.
    const int y=std::max(padding,int(11+float(height)*.1380208283662796f)+padding);
    return {width-padding-panelWidth,std::min(y,height-padding-panelHeight),panelWidth,panelHeight,
        int(34*scale),padding,std::clamp(int(16*scale+1)/2*2,12,32)};
}
}
