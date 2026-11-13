// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "native/guest/cof/server/glock_weapon.hpp"

namespace csol::server::cof
{
// The authenticated map owner supplies a context with static/stable storage.
// It must outlive temporary precache entities and all later world/inventory
// instances, including callback completion after map authority retires.
bool InstallGlockFactory(guest::cof::server::GlockColdHost) noexcept;
void ResetGlockFactory() noexcept;
// Normal ReGameDLL GetClassPtr<CCSPlayerWeapon> ownership and OnCreate. Cold
// binding happens before the caller's Precache/Spawn. Reacquiring an existing
// exact Glock never clears its attack/reload/ownership state.
guest::cof::server::GlockWeapon* CreateGlock(entvars_t&) noexcept;
}
