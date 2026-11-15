// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "guest_cof_tmp_factory.hpp"
#include "native/guest/cof/server/native_factory.hpp"

namespace csol::server::cof
{
namespace
{
guest::cof::server::NativeWeaponFactory<guest::cof::server::TMPWeapon,
    guest::cof::server::TMPColdHost> factory;
}
bool InstallTMPFactory(guest::cof::server::TMPColdHost host) noexcept{return factory.Install(host);}
void ResetTMPFactory() noexcept{factory.Reset();}
guest::cof::server::TMPWeapon* CreateTMP(entvars_t& vars) noexcept{return factory.Create(vars);}
}

C_DLLEXPORT void EXT_FUNC weapon_cof_tmp(entvars_t* vars);
void weapon_cof_tmp(entvars_t* vars)
{
    if(vars)csol::server::cof::CreateTMP(*vars);
}
