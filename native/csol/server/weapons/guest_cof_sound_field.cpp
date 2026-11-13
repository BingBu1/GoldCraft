// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "guest_cof_sound_field.hpp"
#include <cmath>

namespace csol::server::cof
{
namespace
{
struct Busy final
{
    bool& value;
    explicit Busy(bool& v) noexcept : value(v) { value = true; }
    ~Busy() { value = false; }
};
bool ValidEntity(edict_t* entity, CBaseEntity* object) noexcept
{
    return entity && !entity->free && !(entity->v.flags & FL_KILLME) &&
        entity->pvPrivateData == object && object && object->pev == &entity->v &&
        entity->v.pContainingEntity == entity;
}
}
bool SoundField::Identity() const noexcept
{
    if (!active_) return false;
    auto* entity = sound_.Get();
    return active_ && identity_ && pSoundEnt == identity_ && ValidEntity(entity, identity_) &&
        identity_->m_pEntity && identity_->m_pEntity->m_pContainingEntity == identity_;
}
bool SoundField::Current() const noexcept
{
    const auto authority = authority_;
    return active_ && authority.generation && authority.current &&
        authority.current(authority.context) && active_ &&
        authority_.generation == authority.generation && authority_.context == authority.context &&
        authority_.current == authority.current && Identity();
}
bool SoundField::BeginMap(const ResourceAuthority& candidate) noexcept
{
    if (busy_ || active_ || identity_ || !candidate.generation ||
        candidate.generation <= lastGeneration_ || !candidate.current) return false;
    Busy busy(busy_);
    authority_ = candidate;
    lastGeneration_ = candidate.generation;
    active_ = true;
    const auto mapCurrent = [&] {
        return active_ && authority_.generation == candidate.generation &&
            candidate.current(candidate.context) && active_ &&
            authority_.generation == candidate.generation;
    };
    if (!mapCurrent() || !gpGlobals || gpGlobals->maxClients < 1 ||
        gpGlobals->maxClients > MAX_WORLD_SOUNDS || !std::isfinite(gpGlobals->time) ||
        !g_engfuncs.pfnCreateEntity || !g_engfuncs.pfnPvAllocEntPrivateData ||
        !g_engfuncs.pfnCVarGetFloat || pSoundEnt)
    { EndMap(); return false; }
    auto* entity = CREATE_ENTITY();
    if (!mapCurrent() || !entity || entity->free || entity->pvPrivateData ||
        entity->v.pContainingEntity != entity || (entity->v.flags & FL_KILLME))
    { EndMap(); return false; }
    sound_ = EHANDLE(entity);
    entity_ = entity;
    try
    {
        auto* created = GetClassPtr<CCSSoundEnt>(reinterpret_cast<CSoundEnt*>(&entity->v));
        if (!mapCurrent() || !ValidEntity(sound_.Get(), created) || pSoundEnt)
        { EndMap(); return false; }
        identity_ = created;
        pSoundEnt = created; // Actual Initialize allocates through this global.
        created->Spawn();
        if (!Current()) { EndMap(); return false; }
        return true;
    }
    catch (...) { EndMap(); return false; }
}
void SoundField::EndMap() noexcept
{
    active_ = false;
    if (identity_ && pSoundEnt == identity_) pSoundEnt = nullptr;
}
void SoundField::EntityRetiring(edict_t* entity) noexcept
{
    if (!entity || entity_ != entity) return;
    EndMap();
    sound_ = EHANDLE{};
    entity_ = nullptr;
    identity_ = nullptr;
}
bool SoundField::EngineEntitiesReleased(std::uint64_t generation) noexcept
{
    if (busy_ || active_ || !generation || generation != authority_.generation) return false;
    // The old edict hunk may already be gone. A created private entity must
    // have passed EntityRetiring before this acknowledgement; never dereference
    // an EHANDLE merely to inspect memory that the engine has just released.
    if (identity_) return false;
    sound_ = EHANDLE{};
    entity_ = nullptr;
    identity_ = nullptr;
    authority_ = {};
    return true;
}
bool SoundField::PlayerCurrent(CBasePlayer& player, const EHANDLE& owner,
    const guest::cof::PlaySfxCall& call, bool alive) const noexcept
{
    if (!call.current || !call.current(call.context) || !Current()) return false;
    auto* entity = owner.Get();
    return ValidEntity(entity, &player) && (!alive ||
        (player.pev->deadflag == DEAD_NO && player.pev->health > 0));
}
bool SoundField::ShotLatch(CBasePlayer& player, int types, float deadline,
    const guest::cof::PlaySfxCall& call) noexcept
{
    if (busy_ || types != bits_SOUND_COMBAT || !std::isfinite(deadline) ||
        !Current() || !gpGlobals || !player.pev || !g_engfuncs.pfnIndexOfEdict) return false;
    Busy busy(busy_);
    const EHANDLE owner(&player);
    if (!PlayerCurrent(player, owner, call, true)) return false;
    const int index = CSoundEnt::ClientSoundIndex(owner.Get());
    if (!PlayerCurrent(player, owner, call, true) || index < 0 || index >= gpGlobals->maxClients ||
        !CSoundEnt::SoundPointerForIndex(index)) return false;
    player.m_iExtraSoundTypes = types;
    player.m_flStopExtraSoundTime = deadline;
    return true;
}
bool SoundField::PostThink(CBasePlayer& player, const guest::cof::PlaySfxCall& call) noexcept
{
    if (busy_ || !Current() || !gpGlobals || !player.pev || !g_engfuncs.pfnIndexOfEdict ||
        !g_engfuncs.pfnMakeVectors || !gpGlobals || !std::isfinite(gpGlobals->time) ||
        !std::isfinite(gpGlobals->frametime) || gpGlobals->frametime < 0) return false;
    Busy busy(busy_);
    const EHANDLE owner(&player);
    if (!PlayerCurrent(player, owner, call, true)) return false;
    const int index = CSoundEnt::ClientSoundIndex(owner.Get());
    if (!PlayerCurrent(player, owner, call, true) || index < 0 || index >= gpGlobals->maxClients ||
        !CSoundEnt::SoundPointerForIndex(index)) return false;
    player.UpdatePlayerSound();
    return PlayerCurrent(player, owner, call, true);
}
bool SoundField::ResetPlayer(CBasePlayer& player, const guest::cof::PlaySfxCall& call) noexcept
{
    if (busy_ || !Current() || !gpGlobals || !player.pev || !g_engfuncs.pfnIndexOfEdict) return false;
    Busy busy(busy_);
    const EHANDLE owner(&player);
    if (!PlayerCurrent(player, owner, call, false)) return false;
    const int index = CSoundEnt::ClientSoundIndex(owner.Get());
    if (!PlayerCurrent(player, owner, call, false) || index < 0 || index >= gpGlobals->maxClients) return false;
    auto* sound = CSoundEnt::SoundPointerForIndex(index);
    if (!sound) return false;
    sound->Reset();
    player.m_iExtraSoundTypes = 0;
    player.m_flStopExtraSoundTime = 0;
    return true;
}
}
