// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <span>

namespace csol {
constexpr char CharacterMessage[]="CSOChar";
constexpr int CharacterMessageBytes=10;
struct CharacterState {
    std::uint8_t slot{}, active{};
    std::uint16_t id{};
    std::uint32_t userid{};
    bool operator==(const CharacterState&) const = default;
};
inline std::array<std::uint8_t,CharacterMessageBytes> EncodeCharacter(const CharacterState& s)
{
    return {1,s.slot,s.active,0,std::uint8_t(s.id),std::uint8_t(s.id>>8),std::uint8_t(s.userid),std::uint8_t(s.userid>>8),std::uint8_t(s.userid>>16),std::uint8_t(s.userid>>24)};
}
inline bool DecodeCharacter(std::span<const std::uint8_t> b,CharacterState& s)
{
    if(b.size()!=CharacterMessageBytes || b[0]!=1 || b[1]>32 || b[2]>1 || b[3])return false;
    s={b[1],b[2],std::uint16_t(b[4]|unsigned(b[5])<<8),std::uint32_t(b[6]|unsigned(b[7])<<8|unsigned(b[8])<<16|unsigned(b[9])<<24)};
    return s.slot ? (!s.active || (s.id && s.userid)) : (!s.active && !s.id && !s.userid);
}
}
