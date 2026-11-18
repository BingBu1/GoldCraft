// SPDX-License-Identifier: GPL-3.0-or-later
#include "famas_reload.hpp"

#include <cmath>

namespace guest::cof
{
ReloadCommit CommitFamasReload(MagazineReload& state, int capacity,
    std::uint32_t itemFlags, float weaponTime) noexcept
{
    if (capacity <= 0 || state.clip < 0 || !std::isfinite(state.nextAttack) || !std::isfinite(weaponTime))
        return {ReloadCommitStatus::Invalid, 0};
    if (!state.loading || weaponTime < state.nextAttack || (itemFlags & 0x20U)) return {};
    // An external skill can fill beyond capacity while the reload is pending.
    const int rounds = state.clip < capacity ? capacity - state.clip : 0;
    if (rounds) state.clip = capacity;
    state.loading = false;
    return {ReloadCommitStatus::Committed, rounds};
}
}
