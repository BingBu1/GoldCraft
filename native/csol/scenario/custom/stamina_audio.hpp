// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace csol::scenario::custom
{
inline constexpr char BreathMessage[]="CsoBreath";
inline constexpr std::size_t BreathMessageSize=14;
inline constexpr std::array<const char*,2> RecoverySounds{
    "vox/man/sprint_end_default.wav","vox/woman/sprint_end_default.wav"};
struct RecoverySound {bool male{};std::uint64_t life{};std::uint32_t event{};};
inline std::optional<std::array<std::byte,BreathMessageSize>> EncodeRecoverySound(RecoverySound value)
{
    if(!value.life||!value.event)return {};
    std::array<std::byte,BreathMessageSize> bytes{};bytes[0]=std::byte{1};bytes[1]=std::byte(value.male);
    for(unsigned i=0;i<8;++i)bytes[2+i]=std::byte(value.life>>(i*8));
    for(unsigned i=0;i<4;++i)bytes[10+i]=std::byte(value.event>>(i*8));
    return bytes;
}
inline std::optional<RecoverySound> DecodeRecoverySound(std::span<const std::byte> bytes)
{
    if(bytes.size()!=BreathMessageSize||bytes[0]!=std::byte{1}||std::to_integer<unsigned>(bytes[1])>1)return {};
    RecoverySound value{bytes[1]!=std::byte{}};
    for(unsigned i=0;i<8;++i)value.life|=std::uint64_t(std::to_integer<unsigned>(bytes[2+i]))<<(i*8);
    for(unsigned i=0;i<4;++i)value.event|=std::uint32_t(std::to_integer<unsigned>(bytes[10+i]))<<(i*8);
    return value.life&&value.event?std::optional{value}:std::nullopt;
}
}
