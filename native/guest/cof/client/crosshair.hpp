// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
namespace guest::cof::client
{
struct CrosshairFrame
{
    int width{},height{},originalWeapon{},hidden{};
    bool hudActive{},hasWeapon{},aiming{},telescope{};
    float enabled{};
    std::uint8_t scalar{};
};
struct CrosshairRect
{
    int x{},y{},width{},height{};
    bool operator==(const CrosshairRect&) const noexcept=default;
};
// Ordinary magazine-pistol branch only. Original1001952D..100196C2. No
// interpolation, alternative color, or stock-CS dynamic-gap calculation.
bool PistolCrosshair(const CrosshairFrame&,std::array<CrosshairRect,3>&) noexcept;
}
