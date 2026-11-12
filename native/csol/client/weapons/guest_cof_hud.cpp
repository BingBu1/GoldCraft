// SPDX-License-Identifier: GPL-3.0-or-later
#include "weapon_module.hpp"
#include "../../../guest/cof/catalog.hpp"

namespace csol::client
{
namespace
{
constexpr int ids[]{guest::cof::Pistols[0].hostId};
// Authored CSO HUD placement; original CoF shooting remains server-driven.
// Do not borrow a CS pistol's dynamic crosshair or prediction implementation.
constinit const WeaponModule module{
    .ids=ids, .order=90, .ammoKind=hud_style::AmmoKind::Magazine,
    .ammoSprite="sprites/guest/cof/glock_ammo.spr", .ammoRect={0,32,0,32},
    .dynamicCrosshair=false};
const WeaponModuleRegistration registration{module};
}
}
