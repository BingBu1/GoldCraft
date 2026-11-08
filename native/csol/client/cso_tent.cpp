// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "cso_tent.hpp"
#include "../cso_tent.hpp"
#include <r_efx.h>
#include <event_api.h>
#include <cstdint>
#include <span>

#pragma clang fp contract(off)

namespace csol::client
{
namespace
{
// HW 378546ED (TE133) / 37854871 (TE138). Both use the ordinary allocator
// 37855CA0 (efx CL_TempEntAlloc), never the high-priority variant.
void ModelTent(const cso_tent::Model& tent)
{
    auto* fx = engine.pEfxAPI;
    if (!fx || !fx->CL_TempEntAlloc || !engine.hudGetModelByIndex || !engine.GetClientTime) return;
    auto* model = engine.hudGetModelByIndex(std::bit_cast<std::int16_t>(tent.model));
    if (!model) return;
    float origin[3]{tent.origin[0], tent.origin[1], tent.origin[2]};
    auto* temp = fx->CL_TempEntAlloc(origin, model);
    if (!temp) return;
    const double now = engine.GetClientTime();
    for (int i = 0; i < 3; ++i) temp->entity.angles[i] = tent.angles[i];
    temp->entity.curstate.rendermode = tent.rendermode;
    temp->flags |= FTENT_SPRANIMATE;
    // TE138 scales its rate and fade bytes by 0.1f; TE133 uses the raw rate
    // and a fixed fade speed of 1.
    temp->entity.curstate.framerate = tent.extended ? float(tent.framerate) * .1f : float(tent.framerate);
    temp->frameMax = 256.0f;
    temp->entity.curstate.renderamt = tent.renderamt;
    if (tent.extended)
    {
        temp->entity.curstate.frame = 0;
        temp->entity.curstate.animtime = float(now);
    }
    temp->die = float(double(tent.life) * .1 + now);
    if (tent.fade)
    {
        temp->flags |= FTENT_FADEOUT;
        temp->entity.baseline.renderamt = tent.renderamt;
        temp->fadeSpeed = tent.extended ? float(tent.fadeSpeed) * .1f : 1.0f;
    }
    if (tent.follow <= 0) return;
    temp->flags |= tent.extended ? FTENT_PLYRATTACHMENT | FTENT_PERSIST : FTENT_PLYRATTACHMENT;
    temp->clientIndex = tent.follow;
    for (int i = 0; i < 3; ++i) temp->tentOffset[i] = origin[i];
}

// HW 3785226F -> 37858860: the extra byte selects the sound independently
// of the ordinary explosion flags. Stock R_Explosion has no sixth argument.
void ExplosionTent(const cso_tent::Explosion& tent)
{
    auto* fx = engine.pEfxAPI;
    auto* events = engine.pEventAPI;
    float origin[3]{tent.origin[0], tent.origin[1], tent.origin[2]};
    if (fx && fx->R_Explosion)
        fx->R_Explosion(origin, tent.model, float(double(tent.scale) * .1), float(tent.framerate),
            tent.flags | TE_EXPLFLAG_NOSOUND);
    // Original emits sound even for scale zero (sound-only explosion).
    if (!(tent.flags & TE_EXPLFLAG_NOSOUND) && events && events->EV_PlaySound)
        events->EV_PlaySound(-1, origin, CHAN_AUTO, cso_tent::MooncakeSoundPath, 1.0f, .3f, 0, 100);
}

// HW 3785289D (TE137): R_TempSprite with zero velocity, TransAdd,
// FTENT_SPRANIMATE and life 0; the CSO variant 3785C5F0 also takes the rate.
void SpriteTent(const cso_tent::Sprite& tent)
{
    auto* fx = engine.pEfxAPI;
    if (!fx || !fx->R_TempSprite) return;
    float origin[3]{tent.origin[0], tent.origin[1], tent.origin[2]};
    float direction[3]{};
    const float scale = float(double(tent.scale) * .1);
    const float alpha = float(double(float(tent.brightness)) * (1.0 / 255.0));
    auto* temp = fx->R_TempSprite(origin, direction, scale, std::bit_cast<std::int16_t>(tent.model), kRenderTransAdd,
        kRenderFxNone, alpha, 0.0f, FTENT_SPRANIMATE);
    if (temp) temp->entity.curstate.framerate = float(tent.framerate);
}
}

bool ConsumeCsoTent(int size, const void* data)
{
    if (!data || size <= 0) return false;
    const std::span bytes(static_cast<const std::uint8_t*>(data), static_cast<std::size_t>(size));
    const auto type = bytes[0];
    if (type != cso_tent::ModelTent && type != cso_tent::SpriteTent && type != cso_tent::ModelTentEx &&
        type != cso_tent::ExplosionTent)
        return false;
    if (const auto tent = cso_tent::ReadModel(bytes)) ModelTent(*tent);
    else if (const auto tent = cso_tent::ReadSprite(bytes)) SpriteTent(*tent);
    else if (const auto tent = cso_tent::ReadExplosion(bytes)) ExplosionTent(*tent);
    return true;
}
}
