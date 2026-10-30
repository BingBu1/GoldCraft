// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "scenario_effects.hpp"
#include "../scenario_healing.hpp"
#include <r_efx.h>
#include <algorithm>
#include <climits>

namespace csol::client
{
namespace
{
pfnUserMsgHook previousMP{}, previousInit{};
struct Owned { TEMPENTITY* entity{}; int token{}; };
std::array<Owned, 512> attached;
int nextToken{};

void Follow(TEMPENTITY* temp, float, float now)
{
    const auto* owner = engine.GetEntityByIndex ? engine.GetEntityByIndex(temp->clientIndex) : nullptr;
    if (!owner || !owner->model)
    {
        temp->die = now;
        temp->flags |= FTENT_NOMODEL;
        return;
    }
    for (int i = 0; i < 3; ++i) temp->entity.origin[i] = owner->origin[i] + temp->tentOffset[i];
}
bool Owns(const Owned& effect)
{
    return effect.entity && effect.entity->callback == Follow &&
        effect.entity->entity.curstate.iuser4 == effect.token;
}
void Heal(int ownerIndex, bool self)
{
    if (!engine.GetEntityByIndex || !engine.CL_LoadModel || !engine.GetClientTime ||
        !engine.pEfxAPI || !engine.pEfxAPI->CL_TempEntAlloc) return;
    const auto* owner = engine.GetEntityByIndex(ownerIndex);
    if (!owner || !owner->model) return;
    int index{};
    auto* model = engine.CL_LoadModel(healing::Sprites[self ? 1 : 0], &index);
    if (!model || model->type != mod_sprite || model->numframes <= 0) return;
    const double now = engine.GetClientTime();
    const auto slot = std::find_if(attached.begin(), attached.end(), [&](const Owned& effect)
    { return !Owns(effect) || now > effect.entity->die + 1.0; });
    if (slot == attached.end()) return;
    float position[]{owner->origin[0], owner->origin[1], owner->origin[2] + (self ? 25.0f : 40.0f)};
    auto* temp = engine.pEfxAPI->CL_TempEntAlloc(position, model);
    if (!temp) return;
    nextToken = nextToken == INT_MAX ? 1 : nextToken + 1;
    *slot = {temp, nextToken};
    temp->entity.curstate.iuser4 = nextToken;
    // CSO's attachment bit accepts arbitrary entities. The stock client only
    // follows player slots, so use its custom callback for the same offset.
    temp->flags = FTENT_FADEOUT | FTENT_SPRANIMATE | FTENT_PERSIST |
        FTENT_SPRANIMATELOOP | FTENT_CLIENTCUSTOM;
    temp->callback = Follow;
    temp->clientIndex = static_cast<short>(ownerIndex);
    temp->tentOffset[0] = temp->tentOffset[1] = 0;
    temp->tentOffset[2] = self ? 25.0f : 40.0f;
    temp->die = static_cast<float>(now + 1.0);
    temp->fadeSpeed = 1.0f;
    temp->frameMax = static_cast<float>(model->numframes);
    temp->entity.curstate.frame = 0;
    temp->entity.curstate.framerate = 10;
    temp->entity.curstate.rendermode = kRenderTransAdd;
    temp->entity.curstate.renderfx = 14;
    temp->entity.curstate.renderamt = temp->entity.baseline.renderamt = 254;
    temp->entity.curstate.scale = 1;
}
int Receive(const char* name, int size, void* data)
{
    SynchronizeSession();
    if (!data || size <= 0) return previousMP ? previousMP(name, size, data) : 1;
    const auto* bytes = static_cast<const unsigned char*>(data);
    if (bytes[0] != 4) return previousMP ? previousMP(name, size, data) : 1;
    if (size < 4) return 1;
    if (bytes[3] != 9 && bytes[3] != 13) return previousMP ? previousMP(name, size, data) : 1;
    if (size != 4) return 1;
    const int owner = bytes[1] | (int(bytes[2]) << 8);
    if (owner > 0 && owner <= SHRT_MAX) Heal(owner, bytes[3] == 13);
    return 1;
}
int InitHUD(const char* name, int size, void* data)
{
    ResetScenarioEffects();
    return previousInit ? previousInit(name, size, data) : 1;
}
void Install(const char* name, pfnUserMsgHook hook, pfnUserMsgHook& previous)
{
    const auto old = api->HookUserMsg(name, hook);
    if (old != hook) previous = old;
    if (!old) engine.pfnHookUserMsg(name, hook);
}
}
void InitializeScenarioEffects()
{
    Install("MPToCL", Receive, previousMP);
    Install("InitHUD", InitHUD, previousInit);
}
void ResetScenarioEffects()
{
    for (auto& effect : attached)
    {
        if (Owns(effect))
        {
            effect.entity->die = -1;
            effect.entity->flags |= FTENT_NOMODEL;
        }
        effect = {};
    }
}
void ShutdownScenarioEffects() { ResetScenarioEffects(); }
}
