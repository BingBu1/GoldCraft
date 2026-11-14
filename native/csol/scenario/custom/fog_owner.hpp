// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "environment_snapshot.hpp"

namespace csol::scenario::custom
{
struct FogParameters
{
    float density{};
    int sky{};
    bool operator==(const FogParameters&) const = default;
};
struct FogDraw
{
    std::array<float, 3> color{};
    float start{}, end{};
    int on{};
    bool operator==(const FogDraw&) const = default;
};
struct FogPair
{
    FogParameters parameters;
    FogDraw draw;
    bool operator==(const FogPair&) const = default;
};
class FogOwner final
{
public:
    void Observe(FogParameters parameters) noexcept { baseline_.parameters = parameters; parametersKnown_ = true; }
    void Observe(FogDraw draw) noexcept { baseline_.draw = draw; drawKnown_ = true; }
    bool Known() const noexcept { return parametersKnown_ && drawKnown_; }
    const FogPair& Baseline() const noexcept { return baseline_; }
    std::optional<FogPair> Desired(const EnvironmentSnapshot* value, int water, bool viewKnown) const noexcept
    {
        if (!Known()) return {};
        if (!value || !value->view.active || !value->view.fog || !viewKnown) return baseline_;
        const auto& profile = value->profile;
        return FogPair{{profile.density, profile.sky ? 1 : 0},
            {{float(profile.color[0]), float(profile.color[1]), float(profile.color[2])},
                profile.start, profile.end, water <= 2 && profile.density > 0 ? 1 : 0}};
    }
    void Reset() noexcept { baseline_ = {}; parametersKnown_ = drawKnown_ = false; }
private:
    FogPair baseline_;
    bool parametersKnown_{}, drawKnown_{};
};
}
