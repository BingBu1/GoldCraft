// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "guest_cof_revolver_factory.hpp"
#include "native/guest/cof/server/native_factory.hpp"

namespace csol::server::cof
{
namespace
{
guest::cof::server::NativeWeaponFactory<guest::cof::server::RevolverWeapon,
    guest::cof::server::RevolverColdHost> factory;
}
bool InstallRevolverFactory(guest::cof::server::RevolverColdHost host) noexcept{return factory.Install(host);}
void ResetRevolverFactory() noexcept{factory.Reset();}
guest::cof::server::RevolverWeapon* CreateRevolver(entvars_t& vars) noexcept{return factory.Create(vars);}
}

C_DLLEXPORT void EXT_FUNC weapon_cof_revolver(entvars_t* vars);
void weapon_cof_revolver(entvars_t* vars)
{
    if(vars)csol::server::cof::CreateRevolver(*vars);
}
