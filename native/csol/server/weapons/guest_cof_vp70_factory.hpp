// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "native/guest/cof/server/vp70_weapon.hpp"

namespace csol::server::cof
{
bool InstallVP70Factory(guest::cof::server::VP70ColdHost) noexcept;
void ResetVP70Factory() noexcept;
guest::cof::server::VP70Weapon* CreateVP70(entvars_t&) noexcept;
}
