// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "guest_cof_shotgun_factory.hpp"
#include "native/guest/cof/server/native_factory.hpp"

namespace csol::server::cof
{
namespace
{
guest::cof::server::NativeWeaponFactory<guest::cof::server::ShotgunWeapon,
    guest::cof::server::ShotgunColdHost> factory;
}
bool InstallShotgunFactory(guest::cof::server::ShotgunColdHost host) noexcept { return factory.Install(host); }
void ResetShotgunFactory() noexcept { factory.Reset(); }
guest::cof::server::ShotgunWeapon* CreateShotgun(entvars_t& vars) noexcept { return factory.Create(vars); }
}

C_DLLEXPORT void EXT_FUNC weapon_cof_shotgun(entvars_t* vars);
void weapon_cof_shotgun(entvars_t* vars)
{
    if (vars) csol::server::cof::CreateShotgun(*vars);
}
