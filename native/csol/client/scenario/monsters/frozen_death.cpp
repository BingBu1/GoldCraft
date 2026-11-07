// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../runtime.hpp"
#include "frozen_death.hpp"
#include "../../../scenario/monsters/frozen_death.hpp"
#include <r_efx.h>
#include <algorithm>
#include <climits>

#pragma clang fp contract(off)

namespace csol::client
{
namespace
{
struct Owned { TEMPENTITY* entity{}; int token{}; };
std::array<Owned, 500> fragments;
std::uint64_t epoch{};
int nextToken{};
model_t* iceModel{};
bool playing{};

void Fragment(TEMPENTITY* temp, float, float)
{
    // Keep the engine callback tied to this live owner slot. An empty marker
    // can be folded with another effect's callback by the release linker.
    const int index = temp->entity.curstate.iuser3;
    if (index > 0 && index <= static_cast<int>(fragments.size()))
    {
        const auto& slot = fragments[index - 1];
        if (slot.entity == temp && slot.token == temp->entity.curstate.iuser4) return;
    }
    temp->die = -1;
    temp->flags |= FTENT_NOMODEL;
}
bool Owns(const Owned& slot)
{
    return slot.entity && slot.entity->callback == Fragment &&
        slot.entity->entity.curstate.iuser4 == slot.token;
}
void Retire(Owned& slot)
{
    if (Owns(slot))
    {
        slot.entity->die = -1;
        slot.entity->flags |= FTENT_NOMODEL;
    }
    slot = {};
}
void Play(const frozen_death::IceEvent& event)
{
    const auto* fx = engine.pEfxAPI;
    if (playing || !fx || !fx->CL_TempEntAlloc || !engine.CL_LoadModel ||
        !engine.GetClientTime || !engine.pfnRandomFloat || !engine.pfnRandomLong) return;
    struct Scope { Scope() { playing = true; } ~Scope() { playing = false; } } scope;
    const auto generation = epoch;
    const double now = engine.GetClientTime();
    if (generation != epoch || !std::isfinite(now)) return;
    if (!iceModel)
    {
        int index{};
        auto* loaded = engine.CL_LoadModel(frozen_death::IceModel, &index);
        if (generation != epoch) return;
        if (!loaded || loaded->type != mod_studio) return;
        iceModel = loaded;
    }
    const int count = frozen_death::FragmentCount(event.size);
    for (int i = 0; i < count; ++i)
    {
        const auto free = std::find_if(fragments.begin(), fragments.end(), [now](const Owned& slot)
        {
            return !Owns(slot) || now > static_cast<double>(slot.entity->die) + 2.0;
        });
        if (free == fragments.end()) return;
        float position[3];
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            const float random = engine.pfnRandomFloat(-.5f, .5f);
            if (generation != epoch) return;
            position[axis] = event.center[axis] + random * event.size[axis];
            if (!std::isfinite(position[axis])) return;
        }
        // Original37855CA0 is the ordinary allocator (its high variant is
        //37855D20). Preserve normal exhaustion instead of stealing priority.
        auto* temp = fx->CL_TempEntAlloc(position, iceModel);
        if (!temp) return;
        // A callback may already have replaced the engine's whole temp pool.
        // The returned pointer belongs to that retired allocation attempt.
        if (generation != epoch) return;
        nextToken = nextToken == INT_MAX ? 1 : nextToken + 1;
        *free = {temp, nextToken};
        temp->callback = Fragment;
        temp->entity.curstate.iuser3 = static_cast<int>(free - fragments.begin()) + 1;
        temp->entity.curstate.iuser4 = nextToken;
        temp->flags = FTENT_CLIENTCUSTOM | FTENT_SLOWGRAVITY | FTENT_COLLIDEWORLD | FTENT_FADEOUT;
        temp->hitSound = 64;
        temp->fadeSpeed = .5f;
        temp->bounceFactor = 1;
        const auto current = [&] { return generation == epoch && Owns(*free); };
        const int body = engine.pfnRandomLong(8, 15);
        if (!current()) return;
        temp->entity.curstate.body = body;
        const int rotate = engine.pfnRandomLong(0, 255);
        if (!current()) return;
        if (rotate < 200)
        {
            temp->flags |= FTENT_ROTATE;
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                const float value = engine.pfnRandomFloat(-256, 255);
                if (!current()) return;
                temp->entity.baseline.angles[axis] = value;
            }
        }
        // The original consumes the smoke-trail chance even with flags64,
        // which never enables a trail. Keep the RNG order for later fragments.
        engine.pfnRandomLong(0, 255);
        if (!current()) return;
        temp->entity.curstate.rendermode = kRenderNormal;
        temp->entity.curstate.renderamt = temp->entity.baseline.renderamt = 255;
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            const float value = engine.pfnRandomFloat(axis == 2 ? 0.0f : -100.0f, 100.0f);
            if (!current()) return;
            temp->entity.baseline.origin[axis] = value;
        }
        const float extraLife = engine.pfnRandomFloat(0, 1);
        if (!current()) return;
        temp->die = static_cast<float>((now + 2.5) + static_cast<double>(extraLife));
    }
}
int Receive(const char*, int size, void* data)
{
    SynchronizeSession();
    if (data && size == frozen_death::IcePayloadSize)
        if (const auto event = frozen_death::ReadIce(
            {static_cast<const std::uint8_t*>(data), static_cast<std::size_t>(size)})) Play(*event);
    return 1;
}
}
void InitializeFrozenDeath()
{
    if (engine.pfnHookUserMsg) engine.pfnHookUserMsg(frozen_death::IceMessage, Receive);
}
void ResetFrozenDeath()
{
    ++epoch;
    for (auto& slot : fragments) Retire(slot);
    iceModel = nullptr;
}
}
