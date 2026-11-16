// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "guest_cof_g43_factory.hpp"
#include "native/guest/cof/server/native_factory.hpp"

namespace csol::server::cof
{
namespace
{
guest::cof::server::NativeWeaponFactory<guest::cof::server::G43Weapon,
    guest::cof::server::G43ColdHost> factory;
}
bool InstallG43Factory(guest::cof::server::G43ColdHost host) noexcept { return factory.Install(host); }
void ResetG43Factory() noexcept { factory.Reset(); }
guest::cof::server::G43Weapon* CreateG43(entvars_t& vars) noexcept { return factory.Create(vars); }
}

C_DLLEXPORT void EXT_FUNC weapon_cof_g43(entvars_t* vars);
void weapon_cof_g43(entvars_t* vars)
{
    if (vars) csol::server::cof::CreateG43(*vars);
}
