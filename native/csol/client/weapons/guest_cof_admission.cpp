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
#include "guest_cof_ironsight.hpp"
#include "guest_cof_mouse.hpp"
#include "../../../guest/cof/catalog.hpp"
#include "../../../include/goldcraft/connection_api.h"
#include <ostream>

namespace csol::client
{
namespace
{
const GoldCraftConnectionAPI* connection{};
cvar_t* protocol{};
DWORD ownerThread{};
std::uint64_t generation{}, resources{};
bool admitted{}, menuAttempted{}, menuPrepared{};
bool Owner() noexcept { return ownerThread && ownerThread == GetCurrentThreadId(); }
void Publish(bool ready) noexcept
{
    if (ready == admitted) return;
    admitted = ready;
    // Host compatibility only. Server validates entities, damage and ownership.
    if (protocol && engine.Cvar_SetValue) engine.Cvar_SetValue("cof_protocol", ready ? 1.0F : 0.0F);
}
}
void InitializeGuestCofAdmission() { InitializeGuestCofAdmission(GoldCraftConnectionInterface()); }
void InitializeGuestCofAdmission(const GoldCraftConnectionAPI* provider)
{
    if (ownerThread && !Owner()) return;
    ownerThread = GetCurrentThreadId(); connection = nullptr; protocol = nullptr;
    generation = resources = 0; admitted = menuAttempted = menuPrepared = false;
    if (!provider || provider->size < sizeof(*provider) || provider->version != 1 || !provider->generation ||
        !engine.pfnGetCvarPointer || !engine.pfnRegisterVariable || !engine.Cvar_SetValue) return;
    protocol = engine.pfnGetCvarPointer("cof_protocol");
    if (!protocol) protocol = engine.pfnRegisterVariable("cof_protocol", "0", FCVAR_USERINFO);
    if (!protocol || !(protocol->flags & FCVAR_USERINFO)) { protocol = nullptr; return; }
    connection = provider;
    engine.Cvar_SetValue("cof_protocol", 0);
}
void InvalidateGuestCofAdmission() noexcept
{
    if (!Owner()) return;
    Publish(false); generation = resources = 0; menuAttempted = menuPrepared = false;
    ResetGuestCofInput();
}
void ShutdownGuestCofAdmission() noexcept
{
    if (!Owner()) return;
    InvalidateGuestCofAdmission(); connection = nullptr; protocol = nullptr; ownerThread = 0;
}
void GuestCofAdmissionFrame() noexcept
{
    GuestCofMouseFrame();
    if (!Owner() || !connection || !protocol) return;
    const auto current = connection->generation();
    const auto effect = GuestCofEffectsResourceSerial();
    if (current != generation || effect != resources)
    {
        InvalidateGuestCofAdmission(); generation = current; resources = effect;
    }
    if (!current || !effect || !ResourceReadsAllowed()) { Publish(false); return; }
    const auto state = GuestCofResourcesState();
    const bool ready = state.status == GuestCofResourceStatus::Prepared && state.generation == current &&
        state.studioAndRecoilPrepared && state.decalsPrepared && GuestCofParticlesReady() &&
        GuestCofEffectsReady() && GuestCofMuzzleReady() && GuestCofPointLightsReady() &&
        GuestCofControlReady() && GuestCofIronsightReady() && GuestCofMouseReady() && GuestCofInputReady() &&
        HudStyleWeaponResourcesReady(guest::cof::Pistols[0].hostId);
    if (!ready) { Publish(false); return; }
    if (!menuAttempted)
    {
        menuAttempted = true;
        menuPrepared = PrepareBuyMenuItem(guest::cof::Pistols[0].hostId);
    }
    // A cold VFS/renderer callback may replace this connection or its handles.
    if (connection->generation() != current || GuestCofEffectsResourceSerial() != effect)
    { InvalidateGuestCofAdmission(); return; }
    Publish(menuPrepared);
}
bool GuestCofAdmitted() noexcept
{
    return Owner() && ResourceReadsAllowed() && admitted && connection && generation && connection->generation() == generation &&
        resources && GuestCofEffectsResourceSerial() == resources;
}
void GuestCofAdmissionDiagnostics(std::ostream& out)
{
    out << ",\"cofAdmission\":{\"ready\":" << GuestCofAdmitted() << ",\"generation\":" << generation
        << ",\"resources\":" << resources << ",\"menuPrepared\":" << menuPrepared << '}';
}
}
