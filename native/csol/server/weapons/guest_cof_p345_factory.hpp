// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "native/guest/cof/server/p345_weapon.hpp"

namespace csol::server::cof
{
bool InstallP345Factory(guest::cof::server::P345ColdHost) noexcept;
void ResetP345Factory() noexcept;
guest::cof::server::P345Weapon* CreateP345(entvars_t&) noexcept;
}
