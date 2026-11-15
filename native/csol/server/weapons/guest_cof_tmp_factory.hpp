// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "native/guest/cof/server/tmp_weapon.hpp"

namespace csol::server::cof
{
bool InstallTMPFactory(guest::cof::server::TMPColdHost) noexcept;
void ResetTMPFactory() noexcept;
guest::cof::server::TMPWeapon* CreateTMP(entvars_t&) noexcept;
}
