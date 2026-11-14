// SPDX-License-Identifier: GPL-3.0-or-later
#include "weapon_module.hpp"
#include "guest_cof_crosshair.hpp"
#include "../../../guest/cof/catalog.hpp"
#include "../../../guest/cof/client/revolver_hud.hpp"

namespace csol::client
{
namespace
{
constexpr int ids[]{guest::cof::Pistols[0].hostId};
// Authored CSO HUD placement; original CoF shooting remains server-driven.
// Do not borrow a CS pistol's dynamic crosshair or prediction implementation.
constinit const WeaponModule module{
    .ids=ids, .order=90, .initialize=InitializeGuestCofCrosshair,.reset=ResetGuestCofCrosshair,
    .videoInit=GuestCofCrosshairVideoInit,.shutdown=ShutdownGuestCofCrosshair,
    .ammoKind=hud_style::AmmoKind::Magazine,
    .ammoSprite="sprites/guest/cof/glock_ammo.spr", .ammoRect={0,32,0,32},
    .dynamicCrosshair=false,.drawHud=DrawGuestCofCrosshair};
const WeaponModuleRegistration registration{module};
constexpr int p345Ids[]{guest::cof::Pistols[1].hostId};
constinit const WeaponModule p345Module{
    .ids=p345Ids,.order=91,
    .ammoKind=hud_style::AmmoKind::Magazine,
    .ammoSprite="sprites/guest/cof/p345_ammo.spr",.ammoRect={0,32,0,32},
    .dynamicCrosshair=false,.drawHud=DrawGuestCofCrosshair};
const WeaponModuleRegistration p345Registration{p345Module};
constexpr int revolverIds[]{guest::cof::Pistols[2].hostId};
void RevolverHudState(hud_style::WeaponState& state)
{ state.ammoFrame=guest::cof::client::RevolverAmmoFrame(state.clip); }
constinit const WeaponModule revolverModule{
    .ids=revolverIds,.order=92,
    .ammoKind=hud_style::AmmoKind::Magazine,
    .ammoSprite="sprites/guest/cof/revolver_ammo.spr",.ammoRect={0,32,0,32},
    .dynamicCrosshair=false,.hudState=RevolverHudState};
const WeaponModuleRegistration revolverRegistration{revolverModule};
}
}
