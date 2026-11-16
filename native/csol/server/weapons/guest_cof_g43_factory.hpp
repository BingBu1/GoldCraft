// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "native/guest/cof/server/g43_weapon.hpp"

namespace csol::server::cof
{
bool InstallG43Factory(guest::cof::server::G43ColdHost) noexcept;
void ResetG43Factory() noexcept;
guest::cof::server::G43Weapon* CreateG43(entvars_t&) noexcept;
}
