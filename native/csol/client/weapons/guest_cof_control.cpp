// SPDX-License-Identifier: GPL-3.0-or-later
#include "../runtime.hpp"
#include "guest_cof_control.hpp"
#include "guest_cof_particles.hpp"
#include "guest_cof_effects.hpp"
#include "../../../guest/cof/control_protocol.hpp"
#include "../../../include/goldcraft/connection_api.h"
namespace csol::client
{
namespace
{
const GoldCraftConnectionAPI* connection{};
DWORD ownerThread{};
std::uint64_t observedGeneration{}, serial{};
bool active{};
GuestCofControlCamera camera{GuestCofControlCamera::OrdinaryHost};
GuestCofControlResult result{GuestCofControlResult::Inactive};
bool Owner() noexcept { return ownerThread && ownerThread == GetCurrentThreadId(); }
std::uint64_t Generation() noexcept { return connection ? connection->generation() : 0; }
bool Synchronize() noexcept
{
    if (!Owner() || !active) return false;
    const auto generation = Generation();
    if (generation != observedGeneration)
    {
        observedGeneration = generation;
        result = GuestCofControlResult::Inactive;
    }
    return generation != 0;
}
int Receive(const char*, int size, void* data)
{
    if (!Synchronize()) return 1;
    guest::cof::ControlData value{};
    if (size < 0 || !data || !guest::cof::DecodeControl(
        {static_cast<const std::byte*>(data), std::size_t(size)}, value))
    { result = GuestCofControlResult::InvalidPacket; return 1; }
    const auto generation = observedGeneration;
    const auto callSerial = serial;
    if (value.operation == guest::cof::ControlOperation::WeaponDeploy)
    {
        // Original 1001B4B0: local hard-kill before forward displacement clear.
        // No resource/audio/punch/aim/bob-phase/lag reset belongs to this event.
        const bool killed=KillGuestCofLocalParticles(true);
        if(callSerial!=serial||!Synchronize()||generation!=observedGeneration)
        {result=GuestCofControlResult::Cancelled;return 1;}
        const bool reset=ResetGuestCofForwardDisplacement();
        if(callSerial!=serial||!Synchronize()||generation!=observedGeneration)
        {result=GuestCofControlResult::Cancelled;return 1;}
        result=!killed?GuestCofControlResult::MissingParticleBackend:
            reset?GuestCofControlResult::DeployApplied:GuestCofControlResult::MissingForwardDisplacementBackend;
    }
    else if (value.operation == guest::cof::ControlOperation::ShotState)
    {
        if (camera != GuestCofControlCamera::OrdinaryHost)
        { result = GuestCofControlResult::UnsupportedCamera; return 1; }
        if (!engine.GetViewAngles || !engine.SetViewAngles)
        { result = GuestCofControlResult::MissingCameraBackend; return 1; }
        float angles[3];
        engine.GetViewAngles(angles);
        if (callSerial != serial || !Synchronize() || generation != observedGeneration)
        { result = GuestCofControlResult::Cancelled; return 1; }
        // Original ReadByte is unsigned; only pitch changes. The separate
        // wheelchair10542BB8/105594FC consumer has no host implementation.
        angles[0] -= static_cast<float>(value.body);
        engine.SetViewAngles(angles);
        result = callSerial == serial && Synchronize() && generation == observedGeneration ?
            GuestCofControlResult::ShotStateApplied : GuestCofControlResult::Cancelled;
    }
    else
    {
        auto* view = engine.GetViewModel ? engine.GetViewModel() : nullptr;
        if (callSerial != serial || !Synchronize() || generation != observedGeneration)
        { result = GuestCofControlResult::Cancelled; return 1; }
        // Original 10071C50 has no retained/replayed body if GetViewModel is null.
        if (view) { view->curstate.body = value.body; result = GuestCofControlResult::BodyApplied; }
        else result = GuestCofControlResult::NoViewModel;
    }
    return 1;
}
}
void InitializeGuestCofControl() { InitializeGuestCofControl(GoldCraftConnectionInterface()); }
void InitializeGuestCofControl(const GoldCraftConnectionAPI* provider)
{
    if (ownerThread && !Owner()) throw std::runtime_error("CoF control initialization is on the wrong thread");
    ownerThread = GetCurrentThreadId();
    active = false;
    ++serial;
    result = GuestCofControlResult::Inactive;
    camera = GuestCofControlCamera::OrdinaryHost;
    connection = provider && provider->size >= sizeof(GoldCraftConnectionAPI) &&
        provider->version == 1 && provider->generation ? provider : nullptr;
    observedGeneration = Generation();
    // Dedicated namespace: never replace or forward unrelated native messages.
    const auto old = api->HookUserMsg(guest::cof::ControlMessage, Receive);
    if (!old) engine.pfnHookUserMsg(guest::cof::ControlMessage, Receive);
    else if (old != Receive)
    {
        api->HookUserMsg(guest::cof::ControlMessage, old);
        throw std::runtime_error("CoF control message already has a consumer");
    }
    active = true;
}
void SetGuestCofControlCamera(GuestCofControlCamera value) noexcept
{ if (Owner() && active) camera = value; }
void ShutdownGuestCofControl() noexcept
{
    if (ownerThread && !Owner()) return;
    active = false;
    ++serial;
    connection = nullptr;
    observedGeneration = 0;
    result = GuestCofControlResult::Inactive;
    ownerThread = 0;
}
GuestCofControlResult GuestCofLastControl() noexcept { return result; }
bool GuestCofControlReady() noexcept { return Synchronize(); }
}
