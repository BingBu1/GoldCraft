// SPDX-License-Identifier: GPL-3.0-or-later
#include "projectiles.hpp"
#include "../projectile_effects.hpp"
#include <r_efx.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <ostream>

namespace csol::client
{
namespace
{
struct PendingTrail
{
    int sprite{};
    double expires{}, firstSeen{};
    std::array<float, 3> origin{};
    bool observed{};
};
std::array<PendingTrail, 2048> pending;
int messages{}, rejected{}, attached{}, allocationFailures{}, expired{};
float lastWidth{}, lastLife{}, lastBrightness{};
double lastStartDelay{};

int TrailMessage(const char*, int size, void* data)
{
    // Network parsing precedes the frame's interpolated client time. Do not
    // run session rollback detection here: it would clear weapon registration
    // on ordinary incoming packets. HUD_Frame owns that transition.
    if (size != KnifeTrailMessageBytes || !data) { ++rejected; return 0; }
    const auto* bytes = static_cast<const unsigned char*>(data);
    const int entity = bytes[1] | (bytes[2] << 8);
    const int sprite = bytes[3] | (bytes[4] << 8);
    if (bytes[0] != KnifeEffectVersion || entity < 1 || entity >= pending.size() || sprite < 1)
    { ++rejected; return 0; }
    pending[entity] = {sprite, engine.GetClientTime() + 1.0};
    ++messages;
    return 1;
}
}

void InitializeProjectiles()
{
    engine.pfnHookUserMsg(KnifeTrailMessage, TrailMessage);
}

void ResetProjectiles()
{
    pending = {};
    messages = rejected = attached = allocationFailures = expired = 0;
    lastWidth = lastLife = lastBrightness = 0;
    lastStartDelay = 0;
}

int ProjectileAddEntity(int type, cl_entity_t* entity, const char* modelName)
{
    const int accepted = downstream.HUD_AddEntity ? downstream.HUD_AddEntity(type, entity, modelName) : 1;
    if (!accepted || !entity || entity->index < 1 || entity->index >= pending.size()) return accepted;
    auto& request = pending[entity->index];
    if (!request.sprite) return accepted;
    const double now = engine.GetClientTime();
    if (now > request.expires) { ++expired; request = {}; return accepted; }
    if (!entity->model || std::strcmp(entity->model->name, "models/tknifeb.mdl")) return accepted;
    // A newly received entity can stay at its initial rendered position for
    // ex_interp. Starting the 100 ms follow effect there lets it expire before
    // the second sample. Wait for actual interpolated movement, not just a model.
    if (!request.observed)
    {
        std::copy_n(entity->origin, 3, request.origin.begin());
        request.firstSeen = now;
        request.observed = true;
        return accepted;
    }
    float distanceSquared = 0;
    for (int i = 0; i < 3; ++i)
    {
        const float delta = entity->origin[i] - request.origin[i];
        distanceSquared += delta * delta;
    }
    if (distanceSquared < 1) return accepted;
    // Resolve the known resource by name; never index the model table using an
    // unchecked value from a user message (including extended precache tables).
    int spriteIndex = 0;
    auto* sprite = engine.CL_LoadModel ? engine.CL_LoadModel("sprites/smoke.spr", &spriteIndex) : nullptr;
    if (!sprite || sprite->type != mod_sprite || spriteIndex != request.sprite || !engine.pEfxAPI)
    { ++rejected; request = {}; return accepted; }
    auto* beam = engine.pEfxAPI->R_BeamFollow(entity->index, request.sprite,
        KnifeTrailLife, KnifeTrailWidth, 1, 1, 1, KnifeTrailBrightness);
    if (beam)
    {
        ++attached;
        lastWidth = beam->width;
        lastLife = beam->amplitude;
        lastBrightness = beam->brightness;
        lastStartDelay = now - request.firstSeen;
    }
    else ++allocationFailures;
    request = {};
    return accepted;
}

void ProjectileDiagnostics(std::ostream& output)
{
    output << ",\"projectiles\":{\"trailMessages\":" << messages << ",\"rejected\":" << rejected
        << ",\"attached\":" << attached << ",\"allocationFailures\":" << allocationFailures
        << ",\"expired\":" << expired << ",\"startDelay\":" << lastStartDelay
        << ",\"width\":" << lastWidth << ",\"life\":" << lastLife << ",\"brightness\":" << lastBrightness << '}';
}
}
