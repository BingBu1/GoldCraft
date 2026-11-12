// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <span>
namespace guest::cof::client
{
// Original BSS1055959C, explicitly cleared by VidInit and WpnDeploy. No
// unsupported positive producer, timer or substitute recoil is invented.
struct ForwardDisplacementState {float value{};};
void ResetForwardDisplacement(ForwardDisplacementState&) noexcept;
void LimitForwardDisplacement(ForwardDisplacementState&) noexcept;
// Original forward is AngleVectors(ref.cl_viewangles), not the final weapon
// angles or the camera's cached forward. The caller owns that SDK boundary.
void ApplyForwardDisplacement(const ForwardDisplacementState&,std::span<float,3> viewModelOrigin,
                              const std::array<float,3>& forward) noexcept;
}
