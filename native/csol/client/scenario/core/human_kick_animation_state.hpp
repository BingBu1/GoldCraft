// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cmath>
#include <cstdint>
#include <optional>

namespace csol::client::human_kick_detail
{
struct AnimationOwner
{
    int weapon{};
    std::uintptr_t model{};
    int sequence{};
    float started{};
};
// A single presentation lease; never an inventory, ammo or weapon-clock copy.
class AnimationOverlay final
{
public:
    void Reset() { owner_ = {}; life_ = 0; active_ = resumed_ = false; }
    bool Begin(std::uint32_t life, AnimationOwner owner, float now)
    {
        if (active_ && life == life_) return true;
        Reset();
        if (!life || owner.weapon <= 0 || !owner.model || owner.sequence < 0 || owner.sequence > 255 ||
            !std::isfinite(now) || !std::isfinite(owner.started) || owner.started < 0 || owner.started > now) return false;
        if (owner.started == 0) owner.started = now;
        owner_ = owner; life_ = life; active_ = true;
        return true;
    }
    std::optional<float> End(std::uint32_t life, bool resumed, int weapon, int sequence)
    {
        if (!active_ || life != life_ || !resumed || weapon != owner_.weapon || sequence != owner_.sequence)
        { Reset(); return {}; }
        active_ = false; resumed_ = true;
        return owner_.started;
    }
    std::optional<float> Reconcile(int weapon, std::uintptr_t model, int sequence) const
    {
        if (!resumed_ || weapon != owner_.weapon || model != owner_.model || sequence != owner_.sequence) return {};
        return owner_.started;
    }
    void Predicted(int weapon, std::uintptr_t model)
    {
        if (resumed_ && weapon > 0 && model) Reset();
    }
    bool Pending() const { return active_ || resumed_; }
    const AnimationOwner& Owner() const { return owner_; }
private:
    AnimationOwner owner_;
    std::uint32_t life_{};
    bool active_{}, resumed_{};
};
}
