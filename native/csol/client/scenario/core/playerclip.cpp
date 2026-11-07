// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../runtime.hpp"
#include "playerclip.hpp"
#include <pm_defs.h>
#include "../../../scenario/core/playerclip_movement.hpp"

namespace csol::client
{
namespace
{
std::array<playerclip::Descriptor, playerclip::Capacity> clips{};
playerclip::Movement movement;
pfnUserMsgHook previous{};
bool installed{};

int Message(const char*, int size, void* data)
{
    // Sign-on messages can arrive before the first HUD frame on the new map.
    // Clear the previous session before storing this map's collision list.
    SynchronizeSession();
    if (!data || size < 0) return 0;
    const auto packet = playerclip::Read({static_cast<const std::uint8_t*>(data), std::size_t(size)});
    if (!packet) return 0;
    if (packet->operation == playerclip::Operation::Reset) { clips = {}; return 1; }
    auto* found = static_cast<playerclip::Descriptor*>(nullptr);
    auto* empty = static_cast<playerclip::Descriptor*>(nullptr);
    for (auto& clip : clips)
    {
        if (clip.entity == packet->clip.entity) found = &clip;
        if (!clip.entity && !empty) empty = &clip;
    }
    if (packet->operation == playerclip::Operation::Remove)
    { if (found) *found = {}; return 1; }
    if (!found) found = empty;
    if (!found) return 0;
    *found = packet->clip;
    return 1;
}
}

void InitializePlayerClips()
{
    if (installed || !api || !api->HookUserMsg || !engine.pfnHookUserMsg) return;
    const auto old = api->HookUserMsg(playerclip::Message, Message);
    if (old != Message) previous = old;
    // The stock engine registers a message and returns an integer status;
    // MetaHook's existing-message hook returns the previous callback.
    if (!old) engine.pfnHookUserMsg(playerclip::Message, Message);
    installed = true;
}
void ResetPlayerClips()
{
    clips = {};
    // Keep the verified tail ledger until the next PM entry, where an empty
    // snapshot removes it. ResetHUD need not rebuild the engine list first.
}
void ShutdownPlayerClips()
{
    if (installed && api && api->HookUserMsg) api->HookUserMsg(playerclip::Message, previous);
    previous = nullptr; installed = false; ResetPlayerClips(); movement.Reset();
}
bool InjectPlayerClips(playermove_s& move)
{
    std::array<playerclip::CollisionModel, playerclip::Capacity> models;
    std::size_t count{};
    for (const auto& clip : clips)
    {
        if (!clip.entity) continue;
        auto* model = studio.GetModelByIndex ? studio.GetModelByIndex(clip.model) : nullptr;
        if (!model || model->type != mod_brush || model->name[0] != '*') return false;
        models[count++] = {clip, model};
    }
    return movement.Inject(move, std::span(models).first(count));
}
}
