// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "magazine_reload.hpp"

namespace guest::cof
{
// MP1011D964: original FAMAS ID23 refills the registered capacity without
// consuming reserve ammunition or invoking the ordinary reload observer.
// Preserve authoritative CSOL skill grants above the physical capacity.
ReloadCommit CommitFamasReload(MagazineReload&, int capacity,
    std::uint32_t itemFlags, float weaponTime) noexcept;
}
