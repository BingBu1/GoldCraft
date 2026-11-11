// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../../character_skill_protocol.hpp"
#include "../../cso_tent.hpp"
#include <optional>

namespace csol::scenario::teddy
{
enum class AttackModelVisual { Attack2, Blackhole2 };

// MP10997910/10997980 ->115302F0, HW3785D920. Only these normal
// zero-velocity TE145 calls; this is not a generic moving-effect adapter.
inline std::optional<skillfx::WorldEffect> AttackModelEffect(AttackModelVisual visual,
    const std::array<float, 3>& origin, const std::array<float, 3>& angles, int model)
{
    if (model <= 0 || model > 32767 ||
        (visual != AttackModelVisual::Attack2 && visual != AttackModelVisual::Blackhole2)) return std::nullopt;
    skillfx::WorldEffect effect;
    for (unsigned axis = 0; axis != 3; ++axis)
    {
        std::int16_t coordinate{};
        std::uint8_t angle{};
        if (!cso_tent::detail::Coord(origin[axis], coordinate) ||
            !cso_tent::detail::Angle(angles[axis], angle)) return std::nullopt;
        effect.model.origin[axis] = static_cast<float>(coordinate) * .125f;
        effect.angles[axis] = static_cast<float>(std::bit_cast<std::int8_t>(angle)) * (360.0f / 256.0f);
    }
    effect.model.model = static_cast<std::uint16_t>(model);
    effect.model.owner = effect.model.excludedOwner = 0;
    effect.model.sequence = 1;
    effect.model.frameMax = 256;
    effect.model.life = .2f;
    effect.model.frameRate = 1;
    effect.model.alpha = 255;
    effect.model.fadeSpeed = visual == AttackModelVisual::Attack2 ? 1.0f : 3.0f;
    effect.model.scale = effect.model.fadeIn = 0;
    effect.model.renderMode = 5;
    effect.flags = 0;
    return effect;
}
}
