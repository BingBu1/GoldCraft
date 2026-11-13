// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "guest_cof_p345_factory.hpp"
#include "native/guest/cof/server/native_factory.hpp"

namespace csol::server::cof
{
namespace
{
guest::cof::server::NativeWeaponFactory<guest::cof::server::P345Weapon,
    guest::cof::server::P345ColdHost> factory;
}
bool InstallP345Factory(guest::cof::server::P345ColdHost host) noexcept{return factory.Install(host);}
void ResetP345Factory() noexcept{factory.Reset();}
guest::cof::server::P345Weapon* CreateP345(entvars_t& vars) noexcept{return factory.Create(vars);}
}

C_DLLEXPORT void EXT_FUNC weapon_cof_p345(entvars_t* vars);
void weapon_cof_p345(entvars_t* vars)
{
    if(vars)csol::server::cof::CreateP345(*vars);
}
