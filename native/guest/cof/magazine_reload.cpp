// SPDX-License-Identifier: GPL-3.0-or-later
#include "magazine_reload.hpp"

#include <cmath>

namespace guest::cof
{
ReloadCommit CommitMagazineReload(MagazineReload& state, int capacity,
    std::uint32_t itemFlags, float weaponTime,
    ReloadObserver observer, void* context) noexcept
{
    // Host adaptation: reject corrupt or mismatched inputs before arithmetic.
    // These checks do not replace any of the weapon's original reload gates.
    if (capacity <= 0 || state.clip < 0 || state.clip > capacity ||
        state.reserve < 0 || !std::isfinite(state.nextAttack) || !std::isfinite(weaponTime))
        return {ReloadCommitStatus::Invalid, 0};

    if (!state.loading || weaponTime < state.nextAttack || (itemFlags & 0x20U))
        return {};

    const int missing = capacity - state.clip;
    const int rounds = missing < state.reserve ? missing : state.reserve;
    state.clip += rounds;
    state.reserve -= rounds;
    if (observer) observer(context, state);
    state.loading = false;
    return {ReloadCommitStatus::Committed, rounds};
}
}
