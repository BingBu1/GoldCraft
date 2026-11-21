// SPDX-License-Identifier: GPL-3.0-or-later
#include "weapon_module.hpp"
#include "../../../guest/cof/arsenal.hpp"
#include "guest_cof_crosshair.hpp"
#include "guest_cof.hpp"
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
    .dynamicCrosshair=false};
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
constexpr int vp70Ids[]{guest::cof::FindFirearm(guest::cof::Firearm::VP70)->hostId};
constinit const WeaponModule vp70Module{
    .ids=vp70Ids,.order=93,
    .ammoKind=hud_style::AmmoKind::Magazine,
    .ammoSprite="sprites/guest/cof/vp70_ammo.spr",.ammoRect={0,32,0,32},
    .dynamicCrosshair=false};
const WeaponModuleRegistration vp70Registration{vp70Module};
constexpr int tmpIds[]{guest::cof::FindFirearm(guest::cof::Firearm::TMP)->hostId};
constinit const WeaponModule tmpModule{
    .ids=tmpIds,.order=94,
    .ammoKind=hud_style::AmmoKind::Magazine,
    .ammoSprite="sprites/guest/cof/tmp_ammo.spr",.ammoRect={0,32,0,32},
    .dynamicCrosshair=false};
const WeaponModuleRegistration tmpRegistration{tmpModule};
constexpr int mp5Ids[]{guest::cof::FindFirearm(guest::cof::Firearm::MP5)->hostId};
constinit const WeaponModule mp5Module{
    .ids=mp5Ids,.order=95,
    .ammoKind=hud_style::AmmoKind::Magazine,
    .ammoSprite="sprites/guest/cof/mp5_ammo.spr",.ammoRect={0,32,0,32},
    .dynamicCrosshair=false};
const WeaponModuleRegistration mp5Registration{mp5Module};
constexpr int m16Ids[]{guest::cof::FindFirearm(guest::cof::Firearm::M16)->hostId};
void M16HudState(hud_style::WeaponState& state)
{
    unsigned frame{};
    if(GuestCofM16AmmoFrame(frame))state.ammoFrame=int(frame);
    else {state.ammoFrame=hud_style::HiddenAmmoFrame;state.kind=hud_style::AmmoKind::None;}
}
constinit const WeaponModule m16Module{
    .ids=m16Ids,.order=96,
    .ammoKind=hud_style::AmmoKind::Magazine,
    .ammoSprite="sprites/guest/cof/m16_ammo.spr",.ammoRect={0,32,0,32},
    .dynamicCrosshair=false,.hudState=M16HudState};
const WeaponModuleRegistration m16Registration{m16Module};
constexpr int g43Ids[]{guest::cof::FindFirearm(guest::cof::Firearm::G43)->hostId};
constinit const WeaponModule g43Module{
    .ids=g43Ids,.order=97,
    .ammoKind=hud_style::AmmoKind::Magazine,
    .ammoSprite="sprites/guest/cof/g43_ammo.spr",.ammoRect={0,32,0,32},
    .dynamicCrosshair=false};
const WeaponModuleRegistration g43Registration{g43Module};
constexpr int shotgunIds[]{guest::cof::FindFirearm(guest::cof::Firearm::Shotgun)->hostId};
constinit const WeaponModule shotgunModule{
    .ids=shotgunIds,.order=98,
    .ammoKind=hud_style::AmmoKind::Magazine,
    .ammoSprite="sprites/guest/cof/shotgun_ammo.spr",.ammoRect={0,32,0,32},
    .dynamicCrosshair=false};
const WeaponModuleRegistration shotgunRegistration{shotgunModule};
constexpr int famasIds[]{guest::cof::FindFirearm(guest::cof::Firearm::Famas)->hostId};
constinit const WeaponModule famasModule{
    .ids=famasIds,.order=99,
    .ammoKind=hud_style::AmmoKind::Magazine,
    .ammoSprite="sprites/guest/cof/famas_ammo.spr",.ammoRect={0,32,0,32},
    .dynamicCrosshair=false};
const WeaponModuleRegistration famasRegistration{famasModule};
namespace arsenal = guest::cof::arsenal;
constexpr int browningIds[]{arsenal::Browning}, rifleIds[]{arsenal::Rifle};
constexpr int meleeIds[]{arsenal::Axe,arsenal::Branch,arsenal::Nightstick,arsenal::Pickaxe,arsenal::Switchblade};
constinit const WeaponModule browning{
    .ids=browningIds,.order=100,.ammoKind=hud_style::AmmoKind::Magazine,
    .ammoSprite="sprites/guest/cof/browning_ammo.spr",.ammoRect={0,32,0,32},.dynamicCrosshair=false};
constinit const WeaponModule hunting{
    .ids=rifleIds,.order=101,.ammoKind=hud_style::AmmoKind::Magazine,
    .ammoSprite="sprites/guest/cof/rifle_ammo.spr",.ammoRect={0,32,0,32},.dynamicCrosshair=false};
constinit const WeaponModule handWeapons{.ids=meleeIds,.order=102,.dynamicCrosshair=false};
const WeaponModuleRegistration browningRegistration{browning},huntingRegistration{hunting},meleeRegistration{handWeapons};
}
}
