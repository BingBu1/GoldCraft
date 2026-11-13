// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "native/guest/cof/server/revolver_weapon.hpp"

namespace csol::server::cof
{
bool InstallRevolverFactory(guest::cof::server::RevolverColdHost) noexcept;
void ResetRevolverFactory() noexcept;
guest::cof::server::RevolverWeapon* CreateRevolver(entvars_t&) noexcept;
}
