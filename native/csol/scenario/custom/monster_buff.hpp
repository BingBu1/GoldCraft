// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>

namespace csol::scenario::custom
{
struct MonsterPhaseBuff
{
    float speedMultiplier{1.0f}, damageMultiplier{1.0f}, regenerationPerSecond{};
    float regenerationSince{};

    bool Active() const noexcept
    { return speedMultiplier != 1.0f || damageMultiplier != 1.0f || regenerationPerSecond > 0; }
    float ScaleDamage(float damage) const noexcept
    { return damageMultiplier == 1.0f ? damage : damage * damageMultiplier; }
};
inline constexpr MonsterPhaseBuff UnmodifiedMonsterBuff;

class MonsterPhaseRegeneration
{
public:
    void Reset(float now) noexcept { sampledAt_ = now; remainder_ = 0; }

    void Tick(float now, const MonsterPhaseBuff& buff, bool damageable,
        float& health, float maximum) noexcept
    {
        if (!std::isfinite(now) || now < sampledAt_) return;
        if (!damageable || !(health > 0 && health < maximum) || !std::isfinite(maximum))
        {
            Reset(now);
            return;
        }
        if (sampledAt_ <= buff.regenerationSince) remainder_ = 0;
        const float from = std::max(sampledAt_, buff.regenerationSince);
        sampledAt_ = now;
        if (now <= from) return;
        // Keep sub-float-unit healing across frames, even at high max_health.
        const double amount = (static_cast<double>(now) - from) * buff.regenerationPerSecond + remainder_;
        if (amount <= 0)
        {
            remainder_ = amount;
            return;
        }
        const float next = static_cast<float>(std::min(static_cast<double>(maximum), health + amount));
        remainder_ = next >= maximum ? 0.0 : amount - (static_cast<double>(next) - health);
        health = next;
    }

private:
    float sampledAt_{};
    double remainder_{};
};
}
