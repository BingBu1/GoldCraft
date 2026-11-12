// SPDX-License-Identifier: GPL-3.0-or-later
#include "../runtime.hpp"
#include "guest_cof.hpp"
#include "guest_cof_ironsight.hpp"
#include "guest_cof_mouse.hpp"
#include "guest_cof_crosshair.hpp"
#include "guest_cof_effects.hpp"
#include "../../../guest/cof/client/presentation.hpp"
#include "../../../guest/cof/presentation_protocol.hpp"
#include "../../../include/goldcraft/connection_api.h"

#include <cdll_int.h>
#include <cmath>

namespace csol::client
{
namespace
{
guest::cof::PresentationReceiver receiver;
guest::cof::client::Presentation presentation{{}};
guest::cof::PresentationState pending{};
bool received{}, applied{}, active{};
std::uint32_t revision{};
guest::cof::AuthorityKey key{};
cvar_t *defaultFov{}, *zoomSpeed{}, *sensitivity{}, *zoomRatio{}, *multiplier{};
const GoldCraftConnectionAPI* connection{};
std::uint64_t connectionGeneration{};
bool reportedMissingConnection{};

bool SynchronizeConnection() noexcept
{
    const auto current = connection ? connection->generation() : 0;
    if (current != connectionGeneration)
    {
        ResetGuestCof();
        connectionGeneration = current;
    }
    return current != 0;
}

int Receive(const char*, int size, void* bytes)
{
    SynchronizeSession();
    if (!bytes || size != guest::cof::StateBytes) return 1;
    if (!SynchronizeConnection())
    {
        if (!reportedMissingConnection)
        {
            reportedMissingConnection = true;
            engine.Con_Printf("CoF presentation requires the GoldCraft connection lifecycle API\n");
        }
        return 1;
    }
    guest::cof::PresentationState next{};
    if (receiver.Receive({static_cast<const std::byte*>(bytes), std::size_t(size)}, next) !=
        guest::cof::StateAcceptance::Accepted) return 1;
    pending = next;
    received = true;
    GuestCofCrosshairPresentation(next);
    return 1;
}
bool SettingsValid() noexcept
{
    return defaultFov && sensitivity && zoomRatio && zoomSpeed && multiplier &&
        std::isfinite(defaultFov->value) && defaultFov->value >= 1 && defaultFov->value <= 179 &&
        std::isfinite(sensitivity->value) && std::isfinite(zoomRatio->value) &&
        std::isfinite(zoomSpeed->value) && std::isfinite(multiplier->value);
}
}

void InitializeGuestCof()
{
    InitializeGuestCof(GoldCraftConnectionInterface());
}

void InitializeGuestCof(const GoldCraftConnectionAPI* provider)
{
    connection = provider && provider->size >= sizeof(GoldCraftConnectionAPI) &&
        provider->version == 1 && provider->generation ? provider : nullptr;
    connectionGeneration = connection ? connection->generation() : 0;
    reportedMissingConnection = false;
    defaultFov = engine.pfnGetCvarPointer("default_fov");
    sensitivity = engine.pfnGetCvarPointer("sensitivity");
    zoomRatio = engine.pfnGetCvarPointer("zoom_sensitivity_ratio");
    zoomSpeed = engine.pfnGetCvarPointer("cl_zoomspeed");
    if (!zoomSpeed) zoomSpeed = engine.pfnRegisterVariable("cl_zoomspeed", "100", 0);
    multiplier = engine.pfnGetCvarPointer("cl_fovmultiplier");
    if (!multiplier) multiplier = engine.pfnRegisterVariable("cl_fovmultiplier", "1", FCVAR_ARCHIVE);
    // Dedicated message: no other weapon's handler or input binding is replaced.
    const auto old = api->HookUserMsg(guest::cof::StateMessage, Receive);
    if (!old) engine.pfnHookUserMsg(guest::cof::StateMessage, Receive);
    else if (old != Receive)
    {
        api->HookUserMsg(guest::cof::StateMessage, old);
        throw std::runtime_error("CoF state message already has a consumer");
    }
    ResetGuestCof();
    // A missing matched host patch leaves this partial adapter inactive. It
    // never substitutes a guessed aim transform or changes ordinary CS views.
    InitializeGuestCofIronsight();
    InitializeGuestCofMouse(provider);
}

bool GuestCofConnectionReady() noexcept { return connection && connection->generation() != 0; }

void ShutdownGuestCof()
{
    ResetGuestCof();
    ShutdownGuestCofIronsight();
    ShutdownGuestCofMouse();
    connection = nullptr;
    connectionGeneration = 0;
}

void ResetGuestCof()
{
    ResetGuestCofCrosshair();
    ResetGuestCofIronsight();
    ResetGuestCofMouse();
    SetGuestCofEffectsWeapon(guest::cof::client::Weapon::None);
    receiver.Reset();
    presentation.EndAuthority();
    received = applied = active = false;
    pending = {};
    key = {};
    revision = 0;
}

void GuestCofVideoInit() { presentation.VideoInit(); ResetGuestCofMouse(); }

namespace
{
void UpdateFov(client_data_s* data)
{
    if (!data || !received) return;
    if (!applied || !(pending.key == key) || pending.revision != revision)
    {
        if (!pending.originalWeapon)
        {
            // Explicit server release returns ownership to the native client,
            // including its sniper scopes. Never infer this from CurWeapon.
            presentation.EndAuthority();
            active = false;
            SetGuestCofIronsight(false, false);
            ResetGuestCofMouse();
            SetGuestCofEffectsWeapon(guest::cof::client::Weapon::None);
        }
        else
        {
            const float now = engine.GetClientTime();
            if (!SettingsValid() || !std::isfinite(data->fov) || !std::isfinite(now)) return;
            if (!active || !(pending.key == key))
            {
                const bool sameLife = active && key.sessionEpoch == pending.key.sessionEpoch &&
                    key.lifeEpoch == pending.key.lifeEpoch;
                const auto seed = sameLife ? presentation.FovState() :
                    guest::cof::AimFovState{data->fov, now, data->fov};
                presentation.BeginAuthority(pending.key, seed);
            }
            const guest::cof::client::DecodedState state{pending.key,
                static_cast<guest::cof::client::Weapon>(pending.originalWeapon), pending.aimEnabled,
                pending.fovMode, pending.zoomSpeedEighths * 0.125f};
            if (presentation.Receive(state) != guest::cof::client::Result::Applied) return;
            active = true;
            SetGuestCofIronsight(true, pending.aimEnabled);
            SetGuestCofEffectsWeapon(static_cast<guest::cof::client::Weapon>(pending.originalWeapon));
        }
        applied = true;
        key = pending.key;
        revision = pending.revision;
    }
    if (!active || !SettingsValid()) { ResetGuestCofMouse(); return; }
    guest::cof::AimFovOutput output{};
    if (presentation.UpdateClientData({int(defaultFov->value), engine.GetClientTime(), zoomSpeed->value,
        sensitivity->value, zoomRatio->value, multiplier->value}, output))
    {
        data->fov = output.clientFov;
        if (!SetGuestCofMouseFrame(key, output.wholeFov, int(defaultFov->value), output.sensitivity))
            ResetGuestCofMouse();
    }
}
}

int GuestCofUpdateClientData(client_data_s* data, float time)
{
    SynchronizeConnection();
    SynchronizeSession();
    const int result = downstream.HUD_UpdateClientData(data, time);
    UpdateFov(data);
    return result;
}
}
