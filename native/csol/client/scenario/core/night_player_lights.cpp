// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../runtime.hpp"
#include "../../user_message_chain.hpp"
#include "night_player_lights.hpp"
#include "../../../scenario/custom/night_flashlight.hpp"
#include "../../../scenario/custom/night_lighting.hpp"
#include <IMetaRendererMovingLights.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <event_api.h>
#include <ostream>

namespace csol::client
{
namespace
{
namespace night = scenario::custom;
class NightScene final : public IMetaRendererSceneCallbacks
{
public:
    void DrawOpaqueScene(std::uint32_t) override {}
} nightScene;
night::NightLightingPresentation nightState;
IMetaRendererScene* nightRenderer{};
IMetaRendererMovingLights* nightLights{};
DWORD nightThread{};
pfnUserMsgHook nightPrevious{};
bool nightEnabled{}, nightPrepared{}, flashlightReady{};
unsigned nightActive{};
const cl_entity_s* flashlightView{};
std::array<float,3> flashlightDirection{};
std::uint32_t nightColor{};
std::uint64_t nightAccepted{}, nightRejected{}, nightUpdates{}, nightPlayers{};
bool NightOwner() noexcept { return nightThread && nightThread == GetCurrentThreadId(); }
void ClearNight() noexcept
{
    if (nightActive && nightLights) nightLights->ClearLights(&nightScene);
    nightActive = 0;
}
int NightMessage(const char* name, int size, void* data)
{
    if (NightOwner() && nightEnabled && data && size == int(night::NightLightingMessageSize))
    {
        const auto bytes = std::span(static_cast<const std::byte*>(data), std::size_t(size));
        if (night::DecodeNightLighting(bytes))
        {
            SynchronizeSession();
            const auto* previous = nightState.View();
            const bool wasNight = previous && previous->phase == night::Phase::Night;
            const auto previousOwner = previous ? previous->owner : 0;
            if (nightState.Receive(bytes))
            {
                ++nightAccepted;
                const auto* next = nightState.View();
                if (next->phase != night::Phase::Night) { flashlightReady = false; flashlightView = nullptr; }
                else if (!wasNight || previousOwner != next->owner)
                {
                    auto* events = engine.pEventAPI;
                    flashlightReady = events && events->EV_FindModelIndex &&
                        events->EV_FindModelIndex(night::NightFlashlightView) > 0 &&
                        events->EV_FindModelIndex(night::NightFlashlightPlayer) > 0;
                }
                const auto& config = nightState.View()->config;
                nightColor = 0;
                for (unsigned axis = 0; axis < 3; ++axis)
                    nightColor |= ((((config.color >> (16 - 8 * axis)) & 255) * config.brightness / 255) << (8 * axis));
                // Recheck cold capacity after the map's lights have loaded.
                // Frame updates never resize pools or look up plugin APIs.
                if (nightState.Active() && nightLights)
                    nightPrepared = nightLights->PrepareLights(&nightScene, META_MOVING_LIGHTS_MAX_COUNT);
                if (!nightState.Active() || !nightPrepared) ClearNight();
            }
            else ++nightRejected;
        }
        else ++nightRejected;
    }
    return nightPrevious ? nightPrevious(name, size, data) : 1;
}
}
void InitializeNightPlayerLights()
{
    if (nightThread && !NightOwner()) return;
    nightThread = GetCurrentThreadId(); nightEnabled = true;
    InstallChainedUserMessage(night::NightLightingMessage, NightMessage, nightPrevious);
    if (nightPrepared || !api || !api->GetPluginModuleHandleByBaseFileName) return;
    auto module = api->GetPluginModuleHandleByBaseFileName("Renderer");
    if (!module) module = api->GetPluginModuleHandleByBaseFileName("Renderer_AVX2");
    const auto factory = module ? reinterpret_cast<CreateInterfaceFn>(GetProcAddress(HMODULE(module), "CreateInterface")) : nullptr;
    if (!factory) return;
    nightRenderer = static_cast<IMetaRendererScene*>(factory(METARENDERER_SCENE_INTERFACE_VERSION, nullptr));
    nightLights = static_cast<IMetaRendererMovingLights*>(factory(METARENDERER_MOVING_LIGHTS_INTERFACE_VERSION, nullptr));
    if (!nightRenderer || !nightLights) { nightRenderer = nullptr; nightLights = nullptr; return; }
    nightRenderer->RegisterSceneCallbacks(&nightScene);
    nightPrepared = nightLights->PrepareLights(&nightScene, META_MOVING_LIGHTS_MAX_COUNT);
    if (!nightPrepared)
    { nightRenderer->UnregisterSceneCallbacks(&nightScene); nightRenderer = nullptr; nightLights = nullptr; }
}
void NightFlashlightStudioLight(const cl_entity_s* entity, alight_s* light)
{
    if (!flashlightView || entity != flashlightView || !light || !light->plightvec) return;
    // A muzzle-mounted cone points away from the gun. Apply its near spill to
    // this Studio sample only. The world cone and its shadow map stay independent.
    constexpr int ambient = 48, shade = 80;
    const int addAmbient = std::max(0, ambient-light->ambientlight);
    const int addShade = std::max(0, std::min(shade-light->shadelight,
        192-light->ambientlight-addAmbient-light->shadelight));
    if (!addAmbient && !addShade) return;
    const float weight = float(addAmbient+addShade) /
        float(std::max(0,light->ambientlight)+std::max(0,light->shadelight)+addAmbient+addShade);
    for (unsigned i=0;i<3;++i) light->color[i] += (1.f-light->color[i])*weight;
    if (addShade)
    {
        std::array<float,3> blendedDirection{};
        float lengthSq{};
        for (unsigned i=0;i<3;++i)
        {
            blendedDirection[i] = light->plightvec[i]*float(light->shadelight) +
                flashlightDirection[i]*float(addShade);
            lengthSq += blendedDirection[i]*blendedDirection[i];
        }
        if (lengthSq>0.f)
        {
            const float scale=1.f/std::sqrt(lengthSq);
            for (unsigned i=0;i<3;++i) light->plightvec[i]=blendedDirection[i]*scale;
        }
    }
    light->ambientlight += addAmbient;
    light->shadelight += addShade;
}

void NightPlayerLightsView(const ref_params_s& params)
{
    if (!NightOwner() || !nightEnabled || params.nextView) return;
    const bool moving = nightPrepared && nightState.Active() && engine.GetEntityByIndex;
    flashlightView = nullptr;
    if (!flashlightReady && !moving) { ClearNight(); return; }
    const auto* local = engine.GetLocalPlayer ? engine.GetLocalPlayer() : nullptr;
    if (flashlightReady && local && (local->curstate.effects & EF_DIMLIGHT) &&
        !params.spectator && !params.intermission && params.health > 0 && params.viewentity == local->index &&
        engine.GetViewModel)
    {
        flashlightView = engine.GetViewModel();
        for (unsigned i=0;i<3;++i)
            flashlightDirection[i]=.8f*params.forward[i]-.6f*params.up[i];
    }
    if (!moving) { ClearNight(); return; }
    if (!local || local->index < 1 || local->index > 32 || local->curstate.messagenum <= 0 || params.maxclients < 1)
    { ClearNight(); return; }
    std::array<MetaRendererMovingLight, META_MOVING_LIGHTS_MAX_COUNT> lights;
    unsigned count{};
    const auto radius = nightState.View()->config.radius;
    for (int slot = 1, maximum = std::min(params.maxclients, 32); slot <= maximum; ++slot)
    {
        const auto* entity = slot == local->index ? local : engine.GetEntityByIndex(slot);
        // Remote health is not part of CS's player-entity delta. Use its actual
        // solid/movetype/spectator lifecycle instead of an unsent health field.
        if (!entity || !entity->player || !entity->model || entity->index != slot ||
            entity->curstate.messagenum != local->curstate.messagenum || entity->curstate.spectator ||
            (entity->curstate.effects & EF_NODRAW)) continue;
        const bool predicted = entity == local;
        if (predicted ? params.spectator || params.health <= 0 :
            entity->curstate.solid == SOLID_NOT ||
            (entity->curstate.movetype != MOVETYPE_WALK && entity->curstate.movetype != MOVETYPE_FLY &&
                entity->curstate.movetype != MOVETYPE_NOCLIP)) continue;
        const auto* origin = predicted ? params.simorg : entity->origin;
        if (!std::isfinite(origin[0]) || !std::isfinite(origin[1]) || !std::isfinite(origin[2])) continue;
        auto& light = lights[count++];
        for (unsigned axis = 0; axis < 3; ++axis) light.light.origin[axis] = origin[axis];
        light.light.radius = radius; light.light.color = nightColor;
        light.slot = unsigned(slot - 1); light.sourceEntityIndex = unsigned(slot);
    }
    if (!count) { ClearNight(); return; }
    if (nightLights->UpdateLights(&nightScene, lights.data(), count))
    { nightActive = count; ++nightUpdates; nightPlayers += count; }
    else ClearNight();
}
bool NightFlashlightSlotAvailable() noexcept
{
    return NightOwner() && nightEnabled && flashlightReady;
}

bool NightFlashlightHeld() noexcept
{
    if (!NightFlashlightSlotAvailable()) return false;
    // EF_DIMLIGHT can stay set after holstering. Only the held model owns impulse 100.
    const auto* model = engine.GetViewModel ? engine.GetViewModel() : nullptr;
    return model && model->model && !std::strcmp(model->model->name, night::NightFlashlightView);
}

bool TrySelectNightFlashlight()
{
    if (!NightFlashlightSlotAvailable() || !engine.pfnServerCmd) return false;
    if (!NightFlashlightHeld())
        engine.pfnServerCmd(night::NightFlashlightSelect);
    return true;
}

void ResetNightPlayerLights()
{
    if (!NightOwner()) return;
    ClearNight(); nightState.Reset(); nightColor = 0; flashlightReady = false; flashlightView = nullptr;
}
void ShutdownNightPlayerLights()
{
    if (!NightOwner()) return;
    ResetNightPlayerLights(); nightEnabled = false;
    if (nightRenderer) nightRenderer->UnregisterSceneCallbacks(&nightScene);
    nightRenderer = nullptr; nightLights = nullptr; nightPrepared = false;
}
void NightPlayerLightsDiagnostics(std::ostream& output)
{
    output << "nightPlayerLights prepared=" << nightPrepared << " active=" << nightActive
        << " accepted=" << nightAccepted << " rejected=" << nightRejected
        << " updates=" << nightUpdates << " players=" << nightPlayers << '\n';
}
}
