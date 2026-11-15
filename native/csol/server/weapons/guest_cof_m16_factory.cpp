// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "guest_cof_m16_factory.hpp"
#include "native/guest/cof/server/native_factory.hpp"

namespace csol::server::cof
{
namespace
{
guest::cof::server::NativeWeaponFactory<guest::cof::server::M16Weapon,
    guest::cof::server::M16ColdHost> factory;
}
bool InstallM16Factory(guest::cof::server::M16ColdHost host) noexcept { return factory.Install(host); }
void ResetM16Factory() noexcept { factory.Reset(); }
guest::cof::server::M16Weapon* CreateM16(entvars_t& vars) noexcept { return factory.Create(vars); }
}

C_DLLEXPORT void EXT_FUNC weapon_cof_m16(entvars_t* vars);
void weapon_cof_m16(entvars_t* vars)
{
    if (vars) csol::server::cof::CreateM16(*vars);
}
