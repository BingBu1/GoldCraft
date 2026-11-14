// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "guest_cof_owner_runtime.hpp"

namespace csol::server::cof
{
namespace
{
bool EntityMatches(const CBaseEntity* object, const edict_t* entity, int serial) noexcept
{
    return object && entity && !entity->free && entity->serialnumber == serial &&
        entity->pvPrivateData == object && object->pev == &entity->v;
}
// Native inventory is a set of linked lists, not one weapon per visible HUD
// bucket. Bound by the engine entity limit to reject corrupted/cyclic chains.
int InventoryLimit() noexcept { return gpGlobals && gpGlobals->maxEntities > 0 ? gpGlobals->maxEntities : 0; }
}

GlockOwnerRuntime::GlockOwnerRuntime() noexcept
{
    for (unsigned i = 0; i < SlotCount; ++i)
    {
        slots_[i].runtime = this;
        slots_[i].index = i;
    }
}

OwnerBinding GlockOwnerRuntime::Binding(const Slot& slot) const noexcept
{
    return {const_cast<GlockOwnerRuntime*>(this), slot.generation, slot.map,
        slot.life, slot.model, slot.index, slot.serial};
}

bool GlockOwnerRuntime::Matches(const OwnerBinding& binding) const noexcept
{
    if (binding.runtime != this || binding.slot == 0 || binding.slot >= SlotCount) return false;
    const auto& slot = slots_[binding.slot];
    return slot.ready && binding.generation == slot.generation && binding.map == map_ &&
        binding.map == slot.map && binding.life == slot.life && binding.model == slot.model &&
        binding.serial == slot.serial;
}

bool GlockOwnerRuntime::Identity(const Slot& slot) const noexcept
{
    return slot.ready && slot.map == map_ && slot.map && slot.life && slot.model &&
        EntityMatches(slot.player, slot.entity, slot.serial) &&
        slot.player->pev->modelindex == slot.modelIndex &&
        static_cast<int>(slot.player->pev->model) == slot.modelString &&
        slot.player->pev->deadflag == DEAD_NO && slot.player->pev->health > 0;
}

bool GlockOwnerRuntime::AuthorityCurrent(const Slot& slot) const noexcept
{
    if (!Identity(slot) || !slot.authority.current) return false;
    const auto binding = Binding(slot);
    const auto authority = slot.authority;
    auto* player = slot.player;
    const bool accepted = authority.current(authority.context, *player,
        slot.map, slot.life, slot.model, slot.mode);
    return accepted && Matches(binding) && Identity(slot) && slot.player == player &&
        slot.authority.context == authority.context && slot.authority.current == authority.current;
}

bool GlockOwnerRuntime::OwnerCurrent(void* context, CBasePlayer& player,
    std::uint64_t map, std::uint64_t life) noexcept
{
    const auto& slot = *static_cast<Slot*>(context);
    return slot.player == &player && slot.map == map && slot.life == life &&
        slot.runtime->AuthorityCurrent(slot);
}

bool GlockOwnerRuntime::Current(const OwnerBinding& binding) const noexcept
{
    return Matches(binding) && slots_[binding.slot].owner.Current() && Matches(binding);
}

void GlockOwnerRuntime::Clear(Slot& slot, bool retireInventory) noexcept
{
    slot.ready = false;
    ++slot.generation;
    slot.owner.Invalidate();
    slot.animations.Reset();
    // Invalidate only an actually matching owner. A disconnected/reused edict
    // must never cause traversal of the new occupant's native inventory.
    if (retireInventory && EntityMatches(slot.player, slot.entity, slot.serial))
    {
        int remaining = InventoryLimit();
        for (int bucket = 0; bucket < MAX_ITEM_TYPES && remaining > 0; ++bucket)
            for (auto* item = slot.player->m_rgpPlayerItems[bucket]; item && remaining-- > 0;)
            {
                auto* entity = item->edict();
                if (!entity || !EntityMatches(item, entity, entity->serialnumber)) break;
                auto* next = item->m_pNext;
                if (auto* weapon = dynamic_cast<guest::cof::server::OwnerFrameWeapon*>(item);
                    weapon && item->m_pPlayer == slot.player) weapon->Invalidate();
                item = next;
            }
    }
    slot.player = nullptr;
    slot.entity = nullptr;
    slot.serial = slot.modelIndex = slot.modelString = 0;
    slot.map = slot.life = slot.model = 0;
    slot.authority = {};
}

void GlockOwnerRuntime::Invalidate(unsigned index, const CBasePlayer* expected) noexcept
{
    if (index == 0 || index >= SlotCount) return;
    auto& slot = slots_[index];
    if (expected && slot.player != expected) return;
    Clear(slot);
}

void GlockOwnerRuntime::ModelChanging(unsigned index, CBasePlayer& player) noexcept
{
    auto* slot = Ready(index, player);
    if (!slot) { Invalidate(index, &player); return; }
    const auto binding = Binding(*slot);
    int remaining = InventoryLimit();
    for (int bucket = 0; bucket < MAX_ITEM_TYPES; ++bucket)
        for (auto* item = player.m_rgpPlayerItems[bucket]; item;)
        {
            if (remaining-- <= 0 || !Current(binding))
            { if (Matches(binding)) Clear(*slot); return; }
            auto* entity = item->edict();
            if (!entity || !EntityMatches(item, entity, entity->serialnumber))
            { Clear(*slot); return; }
            const EHANDLE next(item->m_pNext);
            const bool hadNext = item->m_pNext != nullptr;
            if (auto* weapon = dynamic_cast<guest::cof::server::OwnerFrameWeapon*>(item);
                weapon && item->m_pPlayer == &player) weapon->OwnerModelChanging();
            if (!Current(binding)) { if (Matches(binding)) Clear(*slot); return; }
            item = next.Get<CBasePlayerItem>();
            if (hadNext && !item) { Clear(*slot); return; }
        }
    // The items already retired through their explicit model boundary. A second
    // generic invalidation here would discard the just-authenticated capture.
    Clear(*slot, false);
}

bool GlockOwnerRuntime::BeginMap(std::uint64_t epoch) noexcept
{
    if (!epoch) { EndMap(); return false; }
    if (map_ == epoch) return true;
    EndMap();
    map_ = epoch;
    return true;
}

void GlockOwnerRuntime::EndMap() noexcept
{
    map_ = 0;
    for (unsigned i = 1; i < SlotCount; ++i) Clear(slots_[i]);
}

bool GlockOwnerRuntime::Prepare(CBasePlayer& player, const OwnerPreparation& request,
    OwnerBackendView& output, std::string& error)
{
    if (request.slot == 0 || request.slot >= SlotCount)
    { error = "Glock owner requires a native player slot1..32"; return false; }
    auto& slot = slots_[request.slot];
    Clear(slot);
    if (!request.map || request.map != map_ || !request.life || !request.modelGeneration ||
        !request.authority.current || !request.compare || !player.pev ||
        player.pev->modelindex <= 0 || request.validatedModel.empty())
    { error = "Glock owner requires current map/life/model authority and a validated model"; return false; }
    auto* entity = player.edict();
    if (!entity || !EntityMatches(&player, entity, entity->serialnumber) ||
        ENTINDEX(entity) != static_cast<int>(request.slot) ||
        player.pev->deadflag != DEAD_NO || player.pev->health <= 0)
    { error = "Glock owner player identity or native slot is not current"; return false; }
    slot.player = &player; slot.entity = entity; slot.serial = entity->serialnumber;
    slot.modelIndex = player.pev->modelindex; slot.modelString = static_cast<int>(player.pev->model);
    slot.map = request.map; slot.life = request.life; slot.model = request.modelGeneration;
    slot.mode = request.mode; slot.authority = request.authority;
    const auto binding = Binding(slot);
    const auto plan = guest::cof::server::RecommendedPistolAnimationPlan(guest::cof::Pistol::Glock, request.rig);
    if (!plan || !request.authority.current(request.authority.context, player,
        request.map, request.life, request.modelGeneration, request.mode) ||
        slot.generation != binding.generation || slot.map != map_ ||
        !EntityMatches(&player, entity, binding.serial))
    { Clear(slot); error = "Glock owner mode or preparation authority was revoked"; return false; }
    if (!slot.animations.Prepare(player, request.validatedModel,
        {request.map, request.modelGeneration}, guest::cof::Pistol::Glock, *plan, request.compare, error, request.extent))
    { Clear(slot); return false; }
    if (slot.generation != binding.generation || slot.map != map_ ||
        !EntityMatches(&player, entity, binding.serial) || player.pev->modelindex != slot.modelIndex ||
        static_cast<int>(player.pev->model) != slot.modelString)
    { Clear(slot); error = "Glock owner changed while preparing its actual model"; return false; }
    slot.ready = true;
    if (!slot.owner.Bind(player, {&slot, OwnerCurrent, slot.map, slot.life},
        request.mode, request.rules, request.persistent) || !Current(binding))
    { Clear(slot); error = "Glock owner binding or supported native mode was rejected"; return false; }
    output = {binding, &slot.animations, {slot.map, slot.model}};
    return true;
}

bool GlockOwnerRuntime::Backend(unsigned index, CBasePlayer& player, OwnerBackendView& output) noexcept
{
    auto* slot = Ready(index, player);
    if (!slot) return false;
    output = {Binding(*slot), &slot->animations, {slot->map, slot->model}};
    return true;
}

GlockOwnerRuntime::Slot* GlockOwnerRuntime::Ready(unsigned index, CBasePlayer& player) noexcept
{
    if (index == 0 || index >= SlotCount) return nullptr;
    auto& slot = slots_[index];
    if (slot.player != &player) return nullptr;
    if (!Current(Binding(slot))) { Clear(slot); return nullptr; }
    return &slot;
}

bool GlockOwnerRuntime::Read(const OwnerBinding& binding, guest::cof::server::GlockOwnerFrame& frame) const noexcept
{ return Current(binding) && slots_[binding.slot].owner.Read(frame); }

bool GlockOwnerRuntime::FirstDraw(const OwnerBinding& binding, bool value) noexcept
{ return Current(binding) && slots_[binding.slot].owner.SetFirstDrawn(value); }

bool GlockOwnerRuntime::Light(const OwnerBinding& binding, bool value) noexcept
{ return Current(binding) && slots_[binding.slot].owner.SetLight(value); }

bool GlockOwnerRuntime::PersistentState(const OwnerBinding& binding, NativeOwnerPersistentState& state) const noexcept
{ return Current(binding) && slots_[binding.slot].owner.PersistentState(state); }

bool GlockOwnerRuntime::AdvanceInventory(Slot& slot) noexcept
{
    const auto binding = Binding(slot);
    int remaining = InventoryLimit();
    if (!remaining) return false;
    for (int bucket = 0; bucket < MAX_ITEM_TYPES; ++bucket)
        for (auto* item = slot.player->m_rgpPlayerItems[bucket]; item;)
        {
            if (remaining-- <= 0 || !Current(binding)) return false;
            auto* entity = item->edict();
            if (!entity || !EntityMatches(item, entity, entity->serialnumber)) return false;
            const EHANDLE next(item->m_pNext);
            const bool hadNext = item->m_pNext != nullptr;
            if (auto* weapon = dynamic_cast<guest::cof::server::OwnerFrameWeapon*>(item);
                weapon && item->m_pPlayer == slot.player)
                weapon->AdvanceOwnerFrame();
            if (!Current(binding)) return false;
            item = next.Get<CBasePlayerItem>();
            if (hadNext && !item) return false;
        }
    return Current(binding);
}

bool GlockOwnerRuntime::BeginPreThink(unsigned index, CBasePlayer& player) noexcept
{
    auto* slot = Ready(index, player);
    if (!slot || !slot->owner.BeginPreThink()) return false;
    const auto binding = Binding(*slot);
    if (AdvanceInventory(*slot)) return true;
    if (Matches(binding)) Clear(*slot);
    return false;
}

bool GlockOwnerRuntime::BeginInput(unsigned index, CBasePlayer& player) noexcept
{ auto* slot = Ready(index, player); return slot && slot->owner.BeginInput(); }

bool GlockOwnerRuntime::AcceptedJump(unsigned index, CBasePlayer& player) noexcept
{ auto* slot = Ready(index, player); return slot && slot->owner.AcceptedNativeJump(); }

bool GlockOwnerRuntime::AfterMovement(unsigned index, CBasePlayer& player) noexcept
{ auto* slot = Ready(index, player); return slot && slot->owner.AfterMovement(); }

bool GlockOwnerRuntime::EndPostThink(unsigned index, CBasePlayer& player) noexcept
{ auto* slot = Ready(index, player); return slot && slot->owner.EndPostThink(); }

bool GlockOwnerRuntime::CancelFrame(unsigned index, CBasePlayer& player) noexcept
{ auto* slot = Ready(index, player); return slot && slot->owner.CancelFrame(); }

bool GlockOwnerRuntime::BackendCurrent(void* context) noexcept
{
    if (!context) return false;
    const auto binding = *static_cast<const OwnerBinding*>(context);
    return binding.runtime && binding.runtime->Current(binding);
}

bool GlockOwnerRuntime::BackendFrame(void* context, guest::cof::server::GlockOwnerFrame& output) noexcept
{
    if (!context) return false;
    const auto binding = *static_cast<const OwnerBinding*>(context);
    return binding.runtime && binding.runtime->Read(binding, output);
}
}
