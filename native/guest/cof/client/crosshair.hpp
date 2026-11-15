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
// Original common firearm tail1001952D..100196C2. No
// interpolation, alternative color, or stock-CS dynamic-gap calculation.
bool FirearmCrosshair(const CrosshairFrame&,std::array<CrosshairRect,3>&) noexcept;
// Family-specific compatibility entry point; never admits an automatic.
bool PistolCrosshair(const CrosshairFrame&,std::array<CrosshairRect,3>&) noexcept;
}
