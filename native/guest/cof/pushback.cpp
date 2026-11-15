// SPDX-License-Identifier: GPL-3.0-or-later
#include "pushback.hpp"

#include <cmath>

namespace guest::cof
{
namespace
{
bool Finite(const PushbackVector& value) noexcept
{
    return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
}
PushbackResult Failure(const PushbackHost& host) noexcept
{
    return host.current(host.context) ? PushbackResult::MissingBackend : PushbackResult::Cancelled;
}
}

PushbackResult LongGunPushback(const PushbackHost& host) noexcept
{
    if (!host.owner || !host.current || !host.stamina || !host.infiniteStamina ||
        !host.setStamina || !host.sound || !host.makeVectors || !host.gunPosition ||
        !host.recoil || !host.next || !host.monster || !host.alive || !host.damage)
        return PushbackResult::MissingBackend;
    if (!host.current(host.context)) return PushbackResult::Cancelled;
    float stamina{};
    if (!host.stamina(host.context, stamina) || !host.current(host.context)) return Failure(host);
    if (!std::isfinite(stamina)) return PushbackResult::InvalidInput;
    if (!(stamina > 0)) return PushbackResult::Ineligible;

    if (!host.sound(host.context, {host.owner, PushbackSample::Swing, 1, 1, .8F, 0, 100}) ||
        !host.current(host.context)) return Failure(host);
    if (!host.makeVectors(host.context) || !host.current(host.context)) return Failure(host);
    PushbackVector source{};
    if (!host.gunPosition(host.context, source) || !host.current(host.context)) return Failure(host);
    if (!Finite(source)) return PushbackResult::InvalidInput;
    bool infinite{};
    if (!host.infiniteStamina(host.context, infinite) || !host.current(host.context)) return Failure(host);
    if (!infinite)
    {
        // Read after the original sound/vector/position callbacks. A snapshot
        // from the entry gate would overwrite legitimate callback changes.
        if (!host.stamina(host.context, stamina) || !host.current(host.context)) return Failure(host);
        if (!std::isfinite(stamina)) return PushbackResult::InvalidInput;
        if (!host.setStamina(host.context, stamina - 15.F) || !host.current(host.context)) return Failure(host);
    }
    if (!host.recoil(host.context, {1, 1, -8}) || !host.current(host.context)) return Failure(host);

    PushbackTarget previous{};
    for (;;)
    {
        PushbackTarget target{};
        if (!host.next(host.context, previous, source, 40.F, target) || !host.current(host.context))
            return Failure(host);
        if (!target) return PushbackResult::Applied;
        if (target == previous) return PushbackResult::InvalidInput;
        previous = target;
        if (target == host.owner) continue;
        PushbackTarget monster{};
        if (!host.monster(host.context, target, monster) || !host.current(host.context)) return Failure(host);
        if (!monster) continue;
        bool alive{};
        if (!host.alive(host.context, monster, alive) || !host.current(host.context)) return Failure(host);
        if (!alive) continue;
        if (!host.damage(host.context, monster, {3.F, 0x05001000U}) || !host.current(host.context))
            return Failure(host);
        if (!host.sound(host.context, {monster, PushbackSample::Hit, 0, 1, .8F, 0, 100}) ||
            !host.current(host.context)) return Failure(host);
    }
}
}
