// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "guest_cof_mp5_factory.hpp"
#include "native/guest/cof/server/native_factory.hpp"

namespace csol::server::cof
{
namespace
{
guest::cof::server::NativeWeaponFactory<guest::cof::server::MP5Weapon,
    guest::cof::server::MP5ColdHost> factory;
}
bool InstallMP5Factory(guest::cof::server::MP5ColdHost host) noexcept { return factory.Install(host); }
void ResetMP5Factory() noexcept { factory.Reset(); }
guest::cof::server::MP5Weapon* CreateMP5(entvars_t& vars) noexcept { return factory.Create(vars); }
}

C_DLLEXPORT void EXT_FUNC weapon_cof_mp5(entvars_t* vars);
void weapon_cof_mp5(entvars_t* vars)
{
    if (vars) csol::server::cof::CreateMP5(*vars);
}
