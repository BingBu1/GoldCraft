// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "native/guest/cof/server/famas_weapon.hpp"

namespace csol::server::cof
{
bool InstallFamasFactory(guest::cof::server::FamasColdHost) noexcept;
void ResetFamasFactory() noexcept;
guest::cof::server::FamasWeapon* CreateFamas(entvars_t&) noexcept;
}
