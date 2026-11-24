// SPDX-License-Identifier: GPL-3.0-or-later
#include "../runtime.hpp"
#include "../resource_refresh.hpp"
#include "../hud_style.hpp"
#include "../buy_menu.hpp"
#include "guest_cof_admission.hpp"
#include "guest_cof_resources.hpp"
#include "guest_cof_effects.hpp"
#include "guest_cof_muzzle.hpp"
#include "guest_cof_scene.hpp"
#include "guest_cof_control.hpp"
#include "guest_cof_input.hpp"
#include "guest_cof_viewmodel.hpp"
#include "guest_cof_ironsight.hpp"
#include "guest_cof_mouse.hpp"
#include "guest_cof_crosshair.hpp"
#include "../../../guest/cof/catalog.hpp"
#include "../../../guest/cof/admission.hpp"
#include "../../../guest/cof/presentation_resources.hpp"
#include "../../../include/goldcraft/connection_api.h"
#include <ostream>

namespace csol::client
{
namespace
{
const GoldCraftConnectionAPI* connection{};
cvar_t *protocol{},*weaponMask{},*extendedMask{};
DWORD ownerThread{};
std::uint64_t generation{}, resources{};
std::uint32_t admittedWeapons{};
std::array<bool,guest::cof::Firearms.size()> menuAttempted{},menuPrepared{};
bool Owner() noexcept { return ownerThread && ownerThread == GetCurrentThreadId(); }
void Publish(std::uint32_t weapons) noexcept
{
    if (weapons == admittedWeapons) return;
    const auto changed = weapons ^ admittedWeapons;
    admittedWeapons = weapons;
    // Host compatibility only. Server validates entities, damage and ownership.
    if (weaponMask && (changed & guest::cof::LegacyFirearmMask))
        engine.Cvar_SetValue("cof_weapons", float(weapons & guest::cof::LegacyFirearmMask));
    if (extendedMask && (changed & guest::cof::FirearmCapability(guest::cof::Firearm::Famas)))
        engine.Cvar_SetValue("cof_w2", (weapons & guest::cof::FirearmCapability(guest::cof::Firearm::Famas)) ? 1.F : 0.F);
    if (protocol && engine.Cvar_SetValue) engine.Cvar_SetValue("cof_protocol",weapons?1.F:0.F);
}
}
void InitializeGuestCofAdmission() { InitializeGuestCofAdmission(GoldCraftConnectionInterface()); }
void InitializeGuestCofAdmission(const GoldCraftConnectionAPI* provider)
{
    if (ownerThread && !Owner()) return;
    ownerThread = GetCurrentThreadId(); connection = nullptr; protocol = weaponMask = extendedMask = nullptr;
    generation = resources = 0; admittedWeapons=0;menuAttempted=menuPrepared={};
    if (!provider || provider->size < sizeof(*provider) || provider->version != 1 || !provider->generation ||
        !engine.pfnGetCvarPointer || !engine.pfnRegisterVariable || !engine.Cvar_SetValue) return;
    protocol = engine.pfnGetCvarPointer("cof_protocol");
    if (!protocol) protocol = engine.pfnRegisterVariable("cof_protocol", "0", FCVAR_USERINFO);
    if (!protocol || !(protocol->flags & FCVAR_USERINFO)) { protocol = nullptr; return; }
    weaponMask=engine.pfnGetCvarPointer("cof_weapons");
    if(!weaponMask)weaponMask=engine.pfnRegisterVariable("cof_weapons","0",FCVAR_USERINFO);
    if(!weaponMask||!(weaponMask->flags&FCVAR_USERINFO)){protocol=weaponMask=nullptr;return;}
    extendedMask=engine.pfnGetCvarPointer("cof_w2");
    if (!extendedMask) extendedMask=engine.pfnRegisterVariable("cof_w2","0",FCVAR_USERINFO);
    if (extendedMask && !(extendedMask->flags & FCVAR_USERINFO)) extendedMask=nullptr;
    if (extendedMask) engine.Cvar_SetValue("cof_w2",0);
    connection = provider;
    engine.Cvar_SetValue("cof_protocol", 0);
    engine.Cvar_SetValue("cof_weapons",0);
}
void InvalidateGuestCofAdmission() noexcept
{
    if (!Owner()) return;
    Publish(0); generation = resources = 0; menuAttempted=menuPrepared={};
    ResetGuestCofInput();
}
void ShutdownGuestCofAdmission() noexcept
{
    if (!Owner()) return;
    InvalidateGuestCofAdmission(); connection = nullptr; protocol = weaponMask = extendedMask = nullptr; ownerThread = 0;
}
void GuestCofAdmissionFrame() noexcept
{
    GuestCofMouseFrame();
    GuestCofCrosshairFrame();
    if (!Owner() || !connection || !protocol || !weaponMask) return;
    const auto current = connection->generation();
    if (ResourceReadsAllowed()) ApplyGuestCofAimDefault(current);
    const auto effect = GuestCofEffectsResourceSerial();
    if (current != generation || effect != resources)
    {
        InvalidateGuestCofAdmission(); generation = current; resources = effect;
    }
    if (!current || !effect || !ResourceReadsAllowed()) { Publish(0); return; }
    const auto state = GuestCofResourcesState();
    const bool ready = state.status == GuestCofResourceStatus::Prepared && state.generation == current &&
        state.studioAndRecoilPrepared && state.decalsPrepared && GuestCofParticlesReady() &&
        GuestCofEffectsReady() && GuestCofMuzzleReady() && GuestCofPointLightsReady() &&
        GuestCofControlReady() && GuestCofIronsightReady() && GuestCofMouseReady() && GuestCofCrosshairReady() && GuestCofInputReady() && GuestCofViewmodelReady();
    if (!ready) { Publish(0); return; }
    std::uint32_t prepared{};
    for(unsigned i=0;i<menuAttempted.size();++i)
    {
        const auto& weapon=guest::cof::Firearms[i];
        const auto bit=guest::cof::FirearmCapability(weapon.identity);
        if((weapon.identity==guest::cof::Firearm::Famas && !extendedMask) || !(state.weapons&bit)||
            (weapon.identity != guest::cof::Firearm::Famas && !GuestCofMuzzleReady(unsigned(weapon.originalId)))||
            !HudStyleWeaponResourcesReady(weapon.hostId,guest::cof::FirearmHudAmmoFrames(weapon.identity)))continue;
        if(!menuAttempted[i])
        {menuAttempted[i]=true;menuPrepared[i]=PrepareBuyMenuItem(weapon.hostId);}
        if(connection->generation()!=current||GuestCofEffectsResourceSerial()!=effect)
        {InvalidateGuestCofAdmission();return;}
        if(menuPrepared[i])prepared|=bit;
    }
    // A cold VFS/renderer callback may replace this connection or its handles.
    if (connection->generation() != current || GuestCofEffectsResourceSerial() != effect)
    { InvalidateGuestCofAdmission(); return; }
    Publish(prepared);
}
bool GuestCofAdmitted() noexcept
{
    return Owner() && ResourceReadsAllowed() && admittedWeapons && connection && generation && connection->generation() == generation &&
        resources && GuestCofEffectsResourceSerial() == resources;
}
bool GuestCofAdmitted(int id) noexcept
{
    const auto* weapon=guest::cof::FindFirearm(id);
    return weapon&&GuestCofAdmitted()&&(admittedWeapons&guest::cof::FirearmCapability(weapon->identity));
}
void GuestCofAdmissionDiagnostics(std::ostream& out)
{
    const auto state = GuestCofResourcesState();
    out << ",\"cofAdmission\":{\"ready\":" << GuestCofAdmitted() << ",\"generation\":" << generation
        << ",\"resources\":" << resources << ",\"menuPrepared\":" << menuPrepared[0]
        << ",\"p345MenuPrepared\":" << menuPrepared[1] << ",\"weapons\":" << admittedWeapons
        << ",\"mouseInitialization\":\"" << GuestCofMouseInitialization() << "\""
        << ",\"resourceStatus\":" << int(state.status)
        << ",\"resourceGeneration\":" << state.generation << ",\"readsAllowed\":" << ResourceReadsAllowed()
        << ",\"studioRecoil\":" << state.studioAndRecoilPrepared << ",\"decals\":" << state.decalsPrepared
        << ",\"particles\":" << GuestCofParticlesReady() << ",\"effects\":" << GuestCofEffectsReady()
        << ",\"muzzle\":" << GuestCofMuzzleReady() << ",\"pointLights\":" << GuestCofPointLightsReady()
        << ",\"control\":" << GuestCofControlReady() << ",\"ironsight\":" << GuestCofIronsightReady()
        << ",\"mouse\":" << GuestCofMouseReady() << ",\"crosshair\":" << GuestCofCrosshairReady()
        << ",\"input\":" << GuestCofInputReady() << ",\"viewmodelContext\":" << GuestCofViewmodelReady()
        << ",\"hud\":" << HudStyleWeaponResourcesReady(guest::cof::Firearms[0].hostId) << '}';
}
}
