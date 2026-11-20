// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../runtime.hpp"
#include "../../resource_refresh.hpp"
#include "../../../scenario/core/human_kick.hpp"
#include "body_stains.hpp"
#include <IMetaRendererStudioStain.h>
#include <algorithm>

namespace csol::client
{
namespace
{
namespace custom = scenario::custom;
IMetaRendererStudioStain* renderer{};
std::array<unsigned, custom::BodyStainDecals.size()> textures{};
const cl_entity_t* viewEntity{};
const model_t* viewDrawBaseModel{};
const model_t* snapshotViewModel{};
int snapshotViewIndex{};
custom::BodyStain snapshot{};
bool snapshotPolicy{}, snapshotDrawsArms{};
HGLRC textureContext{};
int textureMap{};
bool queryInstalled{}, texturesReady{};

bool Query(const cl_entity_t* entity, const model_t* model, int material, const char* name,
    MetaRendererStudioStain& output) noexcept
{
    const auto surface = BodyStainForModel(entity, model);
    if (!surface || material < 0) return false;
    const bool isArm = surface.surface == custom::BodyStainSurface::ViewWeapon &&
        custom::IsBodyStainArmMaterial({name, strnlen(name, 64)});
    return BodyStainMaterial(surface, static_cast<unsigned>(material), isArm, output);
}

void SetActive(bool active) noexcept
{
    if (queryInstalled == active) return;
    renderer->SetQuery(active ? Query : nullptr);
    queryInstalled = active;
}
}

void InitializeBodyStains() noexcept
{
    if (renderer || !api || !api->GetPluginModuleHandleByBaseFileName) return;
    auto module = api->GetPluginModuleHandleByBaseFileName("Renderer");
    if (!module) module = api->GetPluginModuleHandleByBaseFileName("Renderer_AVX2");
    const auto factory = module ? Sys_GetFactory(module) : nullptr;
    if (factory)
        renderer = static_cast<IMetaRendererStudioStain*>(factory(METARENDERER_STUDIO_STAIN_INTERFACE_VERSION, nullptr));
}

void ResetBodyStains() noexcept
{
    SetActive(false);
    textureContext = {};
    texturesReady = false;
    textureMap = 0;
    textures = {};
    viewEntity = nullptr;
    viewDrawBaseModel = snapshotViewModel = nullptr;
    snapshotViewIndex = 0;
    snapshot = {};
    snapshotPolicy = snapshotDrawsArms = false;
}

void ShutdownBodyStains() noexcept
{
    ResetBodyStains();
    renderer = nullptr;
}

void ReadLocalBodyStain(const entity_state_t* state, const clientdata_t* client) noexcept
{
    // hw10210 passes both decoded snapshots before the stock client writes
    // local overrides into entity_state_t. Keep the native 32-bit view index.
    const auto stain = state ? custom::UnpackBodyStain(static_cast<std::uint32_t>(state->iuser3)) : custom::BodyStain{};
    snapshotPolicy = stain.enabled;
    const int viewIndex = client && client->health > 0 && !client->iuser1 &&
        (stain.body || stain.weapon) ? client->viewmodel : 0;
    if (snapshotViewIndex != viewIndex) snapshotViewModel = nullptr;
    snapshotViewIndex = viewIndex;
    snapshot = viewIndex > 0 ? stain : custom::BodyStain{};
}

void BodyStainsFrame() noexcept
{
    if (!renderer) return;
    if (!snapshotPolicy || !ResourceReadsAllowed())
    {
        SetActive(false);
        return;
    }

    const auto context = wglGetCurrentContext();
    if (!context)
    {
        SetActive(false);
        return;
    }

    const int map = renderer->DecalGeneration();
    if (textureContext != context || textureMap != map)
    {
        // Keep the renderer's borrowed names scoped to its active map and GL
        // context. The server advertises policy before the first stained model.
        SetActive(false);
        for (std::size_t index = 0; index < textures.size(); ++index)
            textures[index] = renderer->DecalTexture(custom::BodyStainDecals[index]);
        textureContext = context;
        textureMap = map;
        texturesReady = std::any_of(textures.begin(), textures.end(), [](unsigned texture) { return texture != 0; });
    }
    SetActive(texturesReady);

    viewEntity = engine.GetViewModel ? engine.GetViewModel() : nullptr;
    if (texturesReady && !snapshotViewModel && (snapshot.body || snapshot.weapon) && studio.GetModelByIndex)
    {
        snapshotViewModel = studio.GetModelByIndex(snapshotViewIndex);
        snapshotDrawsArms = snapshotViewModel && std::strcmp(snapshotViewModel->name, scenario::human_kick::FootModel) != 0;
    }
}

BodyStainViewScope::BodyStainViewScope(const cl_entity_t* entity) noexcept : previous_(viewDrawBaseModel)
{
    if (entity && entity == viewEntity) viewDrawBaseModel = entity->model;
}

BodyStainViewScope::~BodyStainViewScope() { viewDrawBaseModel = previous_; }

BodyStainSurface BodyStainForModel(const cl_entity_t* entity, const model_t* model) noexcept
{
    if (!queryInstalled || !snapshotPolicy || !entity || !model ||
        textureMap != renderer->DecalGeneration()) return {};

    using Surface = custom::BodyStainSurface;
    if (entity == viewEntity)
    {
        // Match the precached model before the studio call substitutes custom
        // hands. Both the stock and adapted models draw gun and arm materials.
        const auto* base = viewDrawBaseModel ? viewDrawBaseModel : entity->model;
        return snapshotDrawsArms && base == snapshotViewModel && model == entity->model ?
            BodyStainSurface{snapshot.body, snapshot.weapon, Surface::ViewWeapon} : BodyStainSurface{};
    }

    const auto stain = custom::UnpackBodyStain(static_cast<std::uint32_t>(entity->curstate.iuser3));
    if (!(stain.body || stain.weapon) || entity->index <= 0 ||
        (entity->curstate.effects & EF_NODRAW) || !engine.GetEntityByIndex ||
        engine.GetEntityByIndex(entity->index) != entity) return {};
    if (!entity->player)
    {
        if (model != entity->model) return {};
        return stain.body ? BodyStainSurface{stain.body, 0, Surface::Body} :
            BodyStainSurface{0, stain.weapon, Surface::HeldWeapon};
    }

    const int held = entity->curstate.weaponmodel;
    return stain.weapon && model != entity->model && held > 0 &&
        studio.GetModelByIndex && studio.GetModelByIndex(held) == model ?
        BodyStainSurface{0, stain.weapon, Surface::HeldWeapon} : BodyStainSurface{};
}

bool BodyStainMaterial(BodyStainSurface surface, unsigned material, bool isArm, MetaRendererStudioStain& output) noexcept
{
    using Surface = custom::BodyStainSurface;
    if (surface.surface == Surface::ViewWeapon && isArm) surface.surface = Surface::ViewArms;
    const bool body = surface.surface == Surface::Body || surface.surface == Surface::ViewArms;
    const unsigned seed = body ? surface.body : surface.weapon;
    if (!seed) return false;
    const auto placement = custom::PlaceBodyStain(seed, material, surface.surface);
    const auto texture = textures[placement.decal];
    if (!texture) return false;
    output = {texture, {placement.scale[0], placement.scale[1]}, {placement.offset[0], placement.offset[1]}};
    return true;
}
}
