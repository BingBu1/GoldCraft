// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "native/guest/cof/server/shotgun_weapon.hpp"

namespace csol::server::cof
{
bool InstallShotgunFactory(guest::cof::server::ShotgunColdHost) noexcept;
void ResetShotgunFactory() noexcept;
guest::cof::server::ShotgunWeapon* CreateShotgun(entvars_t&) noexcept;
}
