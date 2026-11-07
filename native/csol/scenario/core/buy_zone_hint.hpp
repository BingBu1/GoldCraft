// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <span>

namespace csol::buy_hint
{
// Original BuyZoneKeyPanel (028442E0/02852620), not StatusIcon/buyzone.
inline constexpr std::array<const char*, 3> Images{
    "resource/ZBSVER2/ingame_f1key.tga",
    "resource/ZBSVER2/ingame_f2key.tga",
    "resource/ShelterTeam/ingame_housingkey.tga"};
inline constexpr char Font[] = "resource/font/df_gb_y9.ttf";
inline constexpr std::array<const wchar_t*, 3> Labels{L"武器推荐", L"重复购买", L"购买菜单"};
enum class Result { Ignored, Accepted, Malformed };
class State
{
public:
    void Reset() { visible_ = false; until_ = 0; mode_ = 1; }
    Result Receive(std::span<const std::uint8_t> bytes, double now)
    {
        if (bytes.empty() || (bytes[0] != 16 && bytes[0] != 17)) return Result::Ignored;
        if (bytes.size() != 1 || !std::isfinite(now)) return Result::Malformed;
        visible_ = bytes[0] == 16;
        if (visible_) until_ = now + 30.0;
        return Result::Accepted;
    }
    void Advance(double now)
    {
        if (std::isfinite(now) && mode_ != 2 && now > until_) visible_ = false;
    }
    void SetMode(unsigned mode) { if (mode <= 2) mode_ = mode; }
    bool Visible() const { return visible_; }
    unsigned Mode() const { return mode_; }
private:
    double until_{};
    unsigned mode_{1};
    bool visible_{};
};
}
