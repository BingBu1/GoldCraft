// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace guest::cof
{
// Stable host resource/transport indices, independent of original CoF IDs and
// inventory categories. Adding an automatic never turns it into a pistol.
enum class Firearm : unsigned char { Glock, P345, Revolver, VP70, TMP, MP5, M16, G43, Shotgun, Famas, Count };
inline constexpr std::array<std::string_view,static_cast<unsigned>(Firearm::Count)> FirearmKeys{
    "glock","p345","revolver","vp70","tmp","mp5","m16","g43","shotgun","famas"};
constexpr std::uint32_t FirearmCapability(Firearm value) noexcept
{
    const auto index=static_cast<unsigned>(value);
    return index<FirearmKeys.size()?1u<<index:0;
}
inline constexpr auto FirearmCapabilityMask=(1u<<FirearmKeys.size())-1u;
}
