// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "oberon_hint.hpp"
#include "../oberon_hint.hpp"
#include <r_efx.h>
#include <algorithm>
#include <climits>

namespace csol::client
{
namespace
{
struct Attachment
{
    TEMPENTITY* temp{};
    const cl_entity_t* owner{};
    const model_t* ownerModel{};
    int token{}, baselineModel{}, message{};
};
std::array<Attachment, 64> attachments{};
int nextToken{};
std::uint64_t epoch{};
bool allocating{};
int allocatingOwner{};
bool allocationCancelled{};

void Follow(TEMPENTITY*, float, float);
bool Owns(const Attachment& value)
{
    return value.temp && value.temp->callback == Follow &&
        value.temp->entity.curstate.iuser4 == value.token;
}
void Retire(Attachment& value)
{
    if (Owns(value))
    {
        value.temp->die = -1;
        value.temp->flags |= FTENT_NOMODEL;
    }
    value = {};
}
void Remove(int index)
{
    if (allocating && allocatingOwner == index) allocationCancelled = true;
    for (auto& value : attachments)
        if (Owns(value) && value.temp->clientIndex == index) Retire(value);
}
void Follow(TEMPENTITY* temp, float, float)
{
    const auto found = std::find_if(attachments.begin(), attachments.end(),
        [&](const Attachment& value) { return value.temp == temp && Owns(value); });
    if (found == attachments.end()) return;
    auto& value = *found;
    const auto generation = epoch;
    const int token = value.token;
    const auto* owner = engine.GetEntityByIndex ? engine.GetEntityByIndex(temp->clientIndex) : nullptr;
    if (generation != epoch || value.token != token || !Owns(value)) return;
    if (!owner || owner != value.owner || !owner->model || owner->model != value.ownerModel ||
        owner->index != temp->clientIndex || owner->baseline.modelindex != value.baselineModel ||
        owner->curstate.messagenum < value.message)
    { Retire(value); return; }
    value.message = owner->curstate.messagenum;
    for (unsigned i = 0; i < 3; ++i)
        temp->entity.origin[i] = owner->origin[i] + temp->tentOffset[i];
}
void Play(const oberon_hint::Event& event)
{
    if (allocating || !engine.GetEntityByIndex || !engine.CL_LoadModel || !engine.GetClientTime ||
        !engine.pEfxAPI || !engine.pEfxAPI->CL_TempEntAllocHigh) return;
    struct Operation
    {
        explicit Operation(int owner)
        { allocating = true; allocatingOwner = owner; allocationCancelled = false; }
        ~Operation() { allocating = false; allocatingOwner = 0; }
    } operation(event.owner);
    const auto generation = epoch;
    const auto current = [&] { return generation == epoch && !allocationCancelled; };
    const auto* owner = engine.GetEntityByIndex(event.owner);
    if (!current() || !owner || !owner->model || owner->index != event.owner) return;
    const auto* ownerModel = owner->model;
    const int baselineModel = owner->baseline.modelindex;
    const int message = owner->curstate.messagenum;
    int index{};
    auto* model = engine.CL_LoadModel(oberon_hint::Sprite, &index);
    if (!current() || !model || model->type != mod_sprite || model->numframes != 1) return;
    const float now = static_cast<float>(engine.GetClientTime());
    if (!current() || !std::isfinite(now) || !std::isfinite(now + event.duration)) return;
    owner = engine.GetEntityByIndex(event.owner);
    if (!current() || !owner || owner->model != ownerModel ||
        owner->baseline.modelindex != baselineModel || owner->curstate.messagenum < message) return;
    auto slot = std::find_if(attachments.begin(), attachments.end(), [&](const Attachment& value)
    { return !Owns(value) || now > value.temp->die + 1.0f / oberon_hint::FadeSpeed; });
    if (slot == attachments.end()) return;
    float position[3];
    for (unsigned i = 0; i < 3; ++i) position[i] = owner->origin[i] + event.offset[i];
    auto* temp = engine.pEfxAPI->CL_TempEntAllocHigh(position, model);
    if (!temp) return;
    // A reset inside an engine callback can also replace its temp pool.
    // Do not dereference a pointer returned by that retired generation.
    if (generation != epoch) return;
    if (allocationCancelled)
    {
        temp->die = -1;
        temp->flags |= FTENT_NOMODEL;
        return;
    }
    nextToken = nextToken == INT_MAX ? 1 : nextToken + 1;
    *slot = {temp, owner, ownerModel, nextToken, baselineModel, owner->curstate.messagenum};
    temp->entity.curstate.iuser4 = nextToken;
    // CSO0x1A180 follows arbitrary entities. Stock PLYRATTACHMENT only
    // follows players, so retain the other bits and perform this offset in
    // a native TEMPENTITY callback. Never copy CSO's extended struct ABI.
    temp->flags = FTENT_FADEOUT | FTENT_SPRANIMATE | FTENT_PERSIST |
        FTENT_SPRANIMATELOOP | FTENT_CLIENTCUSTOM;
    temp->callback = Follow;
    temp->clientIndex = event.owner;
    for (unsigned i = 0; i < 3; ++i) temp->tentOffset[i] = event.offset[i];
    temp->die = now + event.duration;
    temp->fadeSpeed = oberon_hint::FadeSpeed;
    temp->frameMax = 1;
    temp->entity.curstate.frame = 0;
    temp->entity.curstate.framerate = 10;
    temp->entity.curstate.rendermode = kRenderTransAdd;
    temp->entity.curstate.renderfx = 14;
    temp->entity.curstate.renderamt = temp->entity.baseline.renderamt = 254;
    temp->entity.curstate.scale = event.scale;
    Follow(temp, 0, now);
}
}

bool ConsumeOberonHint(int size, const void* data)
{
    if (!data || size <= 0) return false;
    const std::span bytes(static_cast<const std::uint8_t*>(data), static_cast<std::size_t>(size));
    if (oberon_hint::Recognizes(bytes))
    {
        if (const auto event = oberon_hint::Read(bytes)) Play(*event);
        return true;
    }
    // Original monster death is reliable and precedes the pooled entity's
    // reuse. Keep it in the hook chain so the health/skill owners see it too.
    if (size == 3 && bytes[0] == 200)
        Remove(bytes[1] | (int(bytes[2]) << 8));
    if (size == 6 && bytes[0] == 0x61 && bytes[3] == oberon_hint::Resource &&
        bytes[4] == 0 && bytes[5] == 1)
        Remove(bytes[1] | (int(bytes[2]) << 8));
    return false;
}
void ResetOberonHints()
{
    ++epoch;
    for (auto& value : attachments) Retire(value);
}
}
