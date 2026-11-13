// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "scenario_damage.hpp"
#include <charconv>
#include <cstdint>
#include <string_view>

namespace guest::cof
{
constexpr std::uint32_t PistolCapability(Pistol pistol) noexcept
{
    const auto index=static_cast<unsigned>(pistol);
    return index<static_cast<unsigned>(Pistol::Count)?1u<<index:0;
}
inline constexpr auto PistolCapabilityMask=(1u<<static_cast<unsigned>(Pistol::Count))-1u;
struct PistolAdmission
{
    std::uint32_t weapons{};
    bool operator==(const PistolAdmission&) const=default;
};
// Host protocol only. Older CoF clients publish version1 without a weapon
// mask; they retain the already supported Glock and cannot acquire new guns.
inline PistolAdmission ParsePistolAdmission(std::string_view protocol,std::string_view weapons) noexcept
{
    if(protocol!="1")return {};
    if(weapons.empty())return {PistolCapability(Pistol::Glock)};
    std::uint32_t value{};
    const auto parsed=std::from_chars(weapons.data(),weapons.data()+weapons.size(),value);
    if(parsed.ec!=std::errc{}||parsed.ptr!=weapons.data()+weapons.size()||(value&~PistolCapabilityMask))return {};
    return {value};
}
}
