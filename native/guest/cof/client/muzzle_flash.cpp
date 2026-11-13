// SPDX-License-Identifier: GPL-3.0-or-later
#include "muzzle_flash.hpp"
#include <metahook.h>
#include <event_api.h>
#include <event_args.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <xmmintrin.h>
#pragma clang fp contract(off)
namespace guest::cof::client
{
PointLight& AllocatePointLight(PointLightState& state, int key, float now) noexcept
{
    auto* selected = &state.lights[0];
    bool matched = false;
    if (key != 0)
        for (auto& light : state.lights)
            if (light.key == key) { selected = &light; matched = true; break; }
    if (!matched)
        for (auto& light : state.lights)
            if (now > light.die) { selected = &light; break; }
    std::memset(selected, 0, sizeof(*selected));
    selected->key = key;
    return *selected;
}
void AdvancePointLights(PointLightState& state, float now) noexcept
{
    const float elapsed = now - state.lastTime;
    state.lastTime = now;
    const float dt = _mm_cvtss_f32(_mm_max_ss(_mm_setzero_ps(),
        _mm_min_ss(_mm_set_ss(1.0F), _mm_set_ss(elapsed))));
    for (auto& light : state.lights)
        if (!(now > light.die) && light.radius != 0.0F)
        {
            light.radius -= light.decay * dt;
            if (light.radius < 0.0F) light.radius = 0.0F;
        }
}
bool HasPointLights(const PointLightState& state, float now) noexcept
{
    for (const auto& light : state.lights)
        if (light.die >= now && light.radius != 0.0F) return true;
    return false;
}
void ClearPointLights(PointLightState& state) noexcept
{
    std::memset(state.lights.data(), 0, sizeof(state.lights));
}
namespace
{
bool Finite(const float* p) noexcept
{
    return std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]);
}
float Deadline(cl_enginefuncs_s& engine) noexcept
{
    return static_cast<float>(static_cast<double>(engine.GetClientTime()) + 0.06);
}
}
bool P345Muzzle::Ready(bool cheapSmoke) const noexcept
{
    const auto* e = host_.engine;
    return e && e->GetLocalPlayer && e->CL_LoadModel && e->pfnRandomFloat &&
        e->CL_CreateVisibleEntity && e->GetClientTime && e->pEfxAPI &&
        e->pEfxAPI->CL_AllocDlight && host_.customPointLightsReady &&
        host_.allocatePointLight && host_.customPointLightsReady(host_.context) &&
        (!cheapSmoke || (e->pEventAPI && e->pEventAPI->EV_FindModelIndex && e->pEfxAPI->R_TempSprite));
}
bool P345Muzzle::Current() const noexcept {return !host_.current||host_.current(host_.context);}
MuzzleResult P345Muzzle::Lights(const float* origin) const noexcept
{
    if(!Current())return MuzzleResult::Cancelled;
    auto& e = *host_.engine;
    auto* engineLight = e.pEfxAPI->CL_AllocDlight(0);
    if(!Current())return MuzzleResult::Cancelled;
    if (!engineLight) return MuzzleResult::BackendFailed;
    std::memcpy(engineLight->origin, origin, 3 * sizeof(float));
    engineLight->radius = 128.0F;
    engineLight->color.r = 225; engineLight->color.g = 170; engineLight->color.b = 105;
    const float engineDie=Deadline(e);
    if(!Current())return MuzzleResult::Cancelled;
    engineLight->die = engineDie;
    auto* custom = host_.allocatePointLight(host_.context, 0);
    if(!Current())return MuzzleResult::Cancelled;
    if (!custom) return MuzzleResult::BackendFailed;
    std::memcpy(custom->origin.data(), origin, 3 * sizeof(float));
    custom->radius = 128.0F;
    custom->color = {std::bit_cast<float>(0x3F333333U), std::bit_cast<float>(0x3EDC28F6U),
                     std::bit_cast<float>(0x3E0F5C29U)};
    const float customDie=Deadline(e);
    if(!Current())return MuzzleResult::Cancelled;
    custom->die = customDie;
    return MuzzleResult::RequestsIssued;
}
MuzzleResult P345Muzzle::Local(const cl_entity_s& view, unsigned attachment,
                             float scale, float cheapSmoke) const noexcept
{
    if(!Current())return MuzzleResult::Cancelled;
    if (attachment >= 4 || !view.model || !Finite(view.attachment[attachment]) ||
        !Finite(view.angles) || !std::isfinite(scale) || !std::isfinite(cheapSmoke))
        return MuzzleResult::InvalidInput;
    const bool smoke = cheapSmoke != 0.0F;
    const std::array<float,3> position{view.attachment[attachment][0],view.attachment[attachment][1],view.attachment[attachment][2]};
    const std::array<float,3> smokePosition{view.attachment[0][0],view.attachment[0][1],view.attachment[0][2]};
    const std::array<float,3> viewAngles{view.angles[0],view.angles[1],view.angles[2]};
    if (!host_.localVisibleEntity || !Ready(smoke)) return MuzzleResult::MissingBackend;
    if(!Current())return MuzzleResult::Cancelled;
    auto& e = *host_.engine;
    const auto* local = e.GetLocalPlayer();
    if(!Current())return MuzzleResult::Cancelled;
    if (!local) return MuzzleResult::InvalidInput;
    const cl_entity_t source=*local;
    int modelIndex{};
    auto* model = e.CL_LoadModel("models/flash1.mdl", &modelIndex);
    if(!Current())return MuzzleResult::Cancelled;
    if (!model) return MuzzleResult::BackendFailed;
    const float size = static_cast<float>(static_cast<double>(e.pfnRandomFloat(-1.0F, 1.0F)) + 4.0);
    if(!Current())return MuzzleResult::Cancelled;
    auto& entity = *host_.localVisibleEntity;
    std::memcpy(&entity, &source, sizeof(entity));
    entity.player = 0; entity.model = model; entity.curstate.modelindex = modelIndex;
    entity.curstate.scale = size * scale;
    std::memcpy(entity.origin, position.data(), sizeof(entity.origin));
    std::memcpy(entity.angles, viewAngles.data(), sizeof(entity.angles));
    entity.angles[2] = e.pfnRandomFloat(1.0F, 359.0F);
    if(!Current())return MuzzleResult::Cancelled;
    entity.curstate.rendermode = 5; entity.curstate.renderamt = 100;
    entity.curstate.renderfx = 161; entity.curstate.sequence = 0;
    entity.curstate.animtime = e.GetClientTime();
    if(!Current())return MuzzleResult::Cancelled;
    e.CL_CreateVisibleEntity(0, &entity);
    if(!Current())return MuzzleResult::Cancelled;
    if (smoke)
    {
        const int sprite = e.pEventAPI->EV_FindModelIndex("sprites/gunsmoke.spr");
        if(!Current())return MuzzleResult::Cancelled;
        float direction[]{0.0F, 0.0F, 10.0F};
        // Original smoke always uses attachment0, including other flash indexes.
        e.pEfxAPI->R_TempSprite(const_cast<float*>(smokePosition.data()), direction,
                              0.05F, sprite, 5, 6, 255.0F, 1.0F, 0x300100);
        if(!Current())return MuzzleResult::Cancelled;
    }
    return Lights(position.data());
}
MuzzleResult P345Muzzle::World(const std::array<float, 3>& attachment, float scale) const noexcept
{
    if(!Current())return MuzzleResult::Cancelled;
    if (!Finite(attachment.data()) || !std::isfinite(scale)) return MuzzleResult::InvalidInput;
    if (!host_.worldVisibleEntity || !Ready(false) || !host_.engine->pfnAngleVectors) return MuzzleResult::MissingBackend;
    if(!Current())return MuzzleResult::Cancelled;
    auto& e = *host_.engine;
    const auto* local = e.GetLocalPlayer();
    if(!Current())return MuzzleResult::Cancelled;
    if (!local) return MuzzleResult::InvalidInput;
    const cl_entity_t source=*local;
    int modelIndex{};
    auto* model = e.CL_LoadModel("models/flash1.mdl", &modelIndex);
    if(!Current())return MuzzleResult::Cancelled;
    if (!model) return MuzzleResult::BackendFailed;
    const float size = static_cast<float>(static_cast<double>(e.pfnRandomFloat(-1.0F, 1.0F)) + 3.0);
    if(!Current())return MuzzleResult::Cancelled;
    auto& entity = *host_.worldVisibleEntity;
    std::memcpy(&entity, &source, sizeof(entity));
    entity.player = 0; entity.model = model; entity.curstate.modelindex = modelIndex;
    entity.curstate.scale = size * scale;
    std::memcpy(entity.origin, attachment.data(), sizeof(entity.origin));
    entity.angles[2] = e.pfnRandomFloat(1.0F, 359.0F);
    if(!Current())return MuzzleResult::Cancelled;
    entity.curstate.rendermode = 5; entity.curstate.renderamt = 100;
    e.CL_CreateVisibleEntity(0, &entity);
    if(!Current())return MuzzleResult::Cancelled;
    const auto lights=Lights(attachment.data());
    if(lights!=MuzzleResult::RequestsIssued)return lights;
    float forward[3],right[3],up[3];
    e.pfnAngleVectors(source.angles,forward,right,up);
    return Current()?MuzzleResult::RequestsIssued:MuzzleResult::Cancelled;
}
MuzzleResult P345Muzzle::Latch(MuzzleLatch& state, const event_args_s& event) const noexcept
{
    if (event.iparam1 != 7) return MuzzleResult::Unsupported;
    if (!host_.engine || !host_.engine->GetEntityByIndex) return MuzzleResult::MissingBackend;
    const auto* owner = host_.engine->GetEntityByIndex(event.entindex);
    if (!owner || owner->curstate.weaponmodel == 0 ||
        !host_.engine->GetEntityByIndex(owner->curstate.weaponmodel)) return MuzzleResult::Ignored;
    state.pending = true; state.type = event.iparam1; state.owner = event.entindex;
    std::memcpy(state.eventVector.data(), event.angles, sizeof(event.angles));
    return MuzzleResult::RequestsIssued;
}
bool P345Muzzle::Take(MuzzleLatch& state, int currentEntityIndex) noexcept
{
    if (!state.pending || state.owner != currentEntityIndex) return false;
    state.pending = false;
    return true;
}
MuzzleResult GlockMuzzle::Local(const cl_entity_s& view, unsigned attachment, float scale, float cheapSmoke) const noexcept
{
    return base_.Local(view,attachment,scale,cheapSmoke);
}
MuzzleResult GlockMuzzle::World(const std::array<float,3>& attachment,
    const std::array<float,3>& shellAttachment,const std::array<float,3>& eventVector,float scale) const noexcept
{
    if(!base_.Current())return MuzzleResult::Cancelled;
    if(!Finite(attachment.data())||!Finite(shellAttachment.data())||!Finite(eventVector.data())||!std::isfinite(scale))
        return MuzzleResult::InvalidInput;
    if(!host_.worldVisibleEntity||!base_.Ready(false)||!host_.engine->pfnAngleVectors||
        !host_.engine->pEventAPI||!host_.engine->pEventAPI->EV_FindModelIndex||!host_.engine->pEfxAPI->R_TempModel)
        return MuzzleResult::MissingBackend;
    if(!base_.Current())return MuzzleResult::Cancelled;
    auto& e=*host_.engine;const auto* local=e.GetLocalPlayer();
    if(!base_.Current())return MuzzleResult::Cancelled;
    if(!local)return MuzzleResult::InvalidInput;
    const cl_entity_t source=*local;
    int modelIndex{};auto* model=e.CL_LoadModel("models/flash1.mdl",&modelIndex);
    if(!base_.Current())return MuzzleResult::Cancelled;
    if(!model)return MuzzleResult::BackendFailed;
    const float size=static_cast<float>(static_cast<double>(e.pfnRandomFloat(-1.0F,1.0F))+3.0);
    if(!base_.Current())return MuzzleResult::Cancelled;
    auto& entity=*host_.worldVisibleEntity;std::memcpy(&entity,&source,sizeof(entity));
    entity.player=0;entity.model=model;entity.curstate.modelindex=modelIndex;entity.curstate.scale=size*scale;
    std::memcpy(entity.origin,attachment.data(),sizeof(entity.origin));
    entity.angles[2]=e.pfnRandomFloat(1.0F,359.0F);
    if(!base_.Current())return MuzzleResult::Cancelled;
    entity.curstate.rendermode=5;entity.curstate.renderamt=100;e.CL_CreateVisibleEntity(0,&entity);
    if(!base_.Current())return MuzzleResult::Cancelled;
    const auto lights=base_.Lights(attachment.data());if(lights!=MuzzleResult::RequestsIssued)return lights;
    float forward[3],right[3],up[3];e.pfnAngleVectors(const_cast<float*>(source.angles),forward,right,up);
    if(!base_.Current())return MuzzleResult::Cancelled;
    const int shell=e.pEventAPI->EV_FindModelIndex("models/shell.mdl");
    if(!base_.Current())return MuzzleResult::Cancelled;
    float velocity[]{eventVector[0]/20.0F,eventVector[1]/20.0F,eventVector[2]/20.0F};
    float angles[]{0.0F,0.0F,0.0F};
    e.pEfxAPI->R_TempModel(const_cast<float*>(shellAttachment.data()),velocity,angles,2.5F,shell,1);
    if(!base_.Current())return MuzzleResult::Cancelled;
    return MuzzleResult::RequestsIssued;
}
MuzzleResult GlockMuzzle::Latch(MuzzleLatch& state,const event_args_s& event) const noexcept
{
    if(event.iparam1!=3)return MuzzleResult::Unsupported;
    if(!host_.engine||!host_.engine->GetEntityByIndex)return MuzzleResult::MissingBackend;
    const auto* owner=host_.engine->GetEntityByIndex(event.entindex);
    if(!owner||owner->curstate.weaponmodel==0||!host_.engine->GetEntityByIndex(owner->curstate.weaponmodel))return MuzzleResult::Ignored;
    state.pending=true;state.type=event.iparam1;state.owner=event.entindex;
    std::memcpy(state.eventVector.data(),event.angles,sizeof(event.angles));return MuzzleResult::RequestsIssued;
}

}
