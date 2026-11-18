// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "guest_cof_famas_factory.hpp"
#include "native/guest/cof/server/native_factory.hpp"

namespace csol::server::cof
{
namespace
{
guest::cof::server::NativeWeaponFactory<guest::cof::server::FamasWeapon,
    guest::cof::server::FamasColdHost> factory;
}
bool InstallFamasFactory(guest::cof::server::FamasColdHost host) noexcept { return factory.Install(host); }
void ResetFamasFactory() noexcept { factory.Reset(); }
guest::cof::server::FamasWeapon* CreateFamas(entvars_t& vars) noexcept { return factory.Create(vars); }
}

C_DLLEXPORT void EXT_FUNC weapon_cof_famas(entvars_t* vars);
void weapon_cof_famas(entvars_t* vars)
{
    if (vars) csol::server::cof::CreateFamas(*vars);
}
