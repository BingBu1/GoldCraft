// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "guest_cof_vp70_factory.hpp"
#include "native/guest/cof/server/native_factory.hpp"

namespace csol::server::cof
{
namespace
{
guest::cof::server::NativeWeaponFactory<guest::cof::server::VP70Weapon,
    guest::cof::server::VP70ColdHost> factory;
}
bool InstallVP70Factory(guest::cof::server::VP70ColdHost host) noexcept{return factory.Install(host);}
void ResetVP70Factory() noexcept{factory.Reset();}
guest::cof::server::VP70Weapon* CreateVP70(entvars_t& vars) noexcept{return factory.Create(vars);}
}

C_DLLEXPORT void EXT_FUNC weapon_cof_vp70(entvars_t* vars);
void weapon_cof_vp70(entvars_t* vars)
{
    if(vars)csol::server::cof::CreateVP70(*vars);
}
