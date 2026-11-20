// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace guest::cof
{
// Verified original rendering/message identities. This does not grant host
// inventory, cold resources, purchase admission or an original object layout.
enum class OriginalWeaponId : std::uint8_t
{
    None=0, Glock=3, Shotgun=4, P345=7, M16=9, Tmp=14, Vp70=19, Revolver=21, Famas=23, G43=26, Mp5=27
};
constexpr bool KnownOriginalWeaponId(unsigned id) noexcept
{
    switch(id)
    {
    case unsigned(OriginalWeaponId::Glock): case unsigned(OriginalWeaponId::Shotgun):
    case unsigned(OriginalWeaponId::P345):
    case unsigned(OriginalWeaponId::M16): case unsigned(OriginalWeaponId::Tmp):
    case unsigned(OriginalWeaponId::Vp70): case unsigned(OriginalWeaponId::Revolver):
    case unsigned(OriginalWeaponId::Famas): case unsigned(OriginalWeaponId::G43): case unsigned(OriginalWeaponId::Mp5): return true;
    default:return false;
    }
}
}
