// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "native/guest/cof/server/mp5_weapon.hpp"

namespace csol::server::cof
{
bool InstallMP5Factory(guest::cof::server::MP5ColdHost) noexcept;
void ResetMP5Factory() noexcept;
guest::cof::server::MP5Weapon* CreateMP5(entvars_t&) noexcept;
}
