// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "native/guest/cof/server/m16_weapon.hpp"

namespace csol::server::cof
{
bool InstallM16Factory(guest::cof::server::M16ColdHost) noexcept;
void ResetM16Factory() noexcept;
guest::cof::server::M16Weapon* CreateM16(entvars_t&) noexcept;
}
