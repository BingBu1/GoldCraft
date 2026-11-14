// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>

namespace csol::scenario::custom
{
inline constexpr char StaminaMessage[]="CsoEnergy";
inline constexpr std::size_t StaminaMessageSize=22;
struct StaminaSnapshot
{
    std::uint64_t life{};
    std::uint32_t revision{};
    float amount{},maximum{};
    bool visible{},infinite{},exhausted{};
};
inline bool Valid(const StaminaSnapshot& value) noexcept
{
    if(!value.life||!value.revision||!std::isfinite(value.amount)||!std::isfinite(value.maximum))return false;
    if(!value.visible)return value.amount==0&&value.maximum==0&&!value.infinite&&!value.exhausted;
    // The actual JSON bounds are maximum1e6 * dusk multiplier1000.
    return value.maximum>0&&value.maximum<=1e9f&&value.amount>=0&&value.amount<=value.maximum&&
        !(value.infinite&&value.exhausted);
}
inline std::optional<std::array<std::byte,StaminaMessageSize>> EncodeStamina(const StaminaSnapshot& value) noexcept
{
    if(!Valid(value))return {};
    std::array<std::byte,StaminaMessageSize> bytes{};
    bytes[0]=std::byte{1};bytes[1]=std::byte(unsigned(value.visible)|(unsigned(value.infinite)<<1)|(unsigned(value.exhausted)<<2));
    for(unsigned i=0;i<8;++i)bytes[2+i]=std::byte(value.life>>(8*i));
    for(unsigned i=0;i<4;++i)bytes[10+i]=std::byte(value.revision>>(8*i));
    const auto amount=std::bit_cast<std::uint32_t>(value.amount),maximum=std::bit_cast<std::uint32_t>(value.maximum);
    for(unsigned i=0;i<4;++i){bytes[14+i]=std::byte(amount>>(8*i));bytes[18+i]=std::byte(maximum>>(8*i));}
    return bytes;
}
inline std::optional<StaminaSnapshot> DecodeStamina(std::span<const std::byte> bytes) noexcept
{
    if(bytes.size()!=StaminaMessageSize||bytes[0]!=std::byte{1}||std::to_integer<unsigned>(bytes[1])>7)return {};
    const auto flags=std::to_integer<unsigned>(bytes[1]);
    StaminaSnapshot value{};value.visible=flags&1;value.infinite=flags&2;value.exhausted=flags&4;
    std::uint32_t amount{},maximum{};
    for(unsigned i=0;i<8;++i)value.life|=std::to_integer<std::uint64_t>(bytes[2+i])<<(8*i);
    for(unsigned i=0;i<4;++i)
    {
        value.revision|=std::to_integer<std::uint32_t>(bytes[10+i])<<(8*i);
        amount|=std::to_integer<std::uint32_t>(bytes[14+i])<<(8*i);
        maximum|=std::to_integer<std::uint32_t>(bytes[18+i])<<(8*i);
    }
    value.amount=std::bit_cast<float>(amount);value.maximum=std::bit_cast<float>(maximum);
    return Valid(value)?std::optional{value}:std::nullopt;
}
class StaminaPresentation final
{
public:
    bool Receive(std::span<const std::byte> bytes) noexcept
    {
        const auto next=DecodeStamina(bytes);
        if(!next)return false;
        if(known_)
        {
            if(next->life<value_.life)return false;
            if(next->life==value_.life&&next->revision<=value_.revision)return false;
        }
        value_=*next;known_=true;return true;
    }
    std::optional<StaminaSnapshot> Sample() const noexcept
    {return known_?std::optional{value_}:std::nullopt;}
    void Reset() noexcept{value_={};known_=false;}
private:
    StaminaSnapshot value_;
    bool known_{};
};
}
