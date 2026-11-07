// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_oberon_blackhole.h"
#include "../monsters/scenario_monster_factory.h"
#include "../core/scenario_session.h"
#include "../core/game_rules_gate.h"

#include <algorithm>
#include <cmath>

#pragma clang fp contract(off)

LINK_ENTITY_TO_CLASS(blackhole, CCSOLOberonBlackHole, CCSEntity)

struct CSOLOberonBlackHoleState
{
    std::weak_ptr<CCSOLScenarioSession> session;
    std::shared_ptr<const CSOLScenarioEntityAccess> access;
    EHANDLE entity;
    const CCSOLOberonBlackHole* identity{};
    std::uint64_t round{};
    float deadline{}, strength{};
    bool ready{}, busy{}, retired{}, spawned{};

    CCSOLOberonBlackHole* Attached() const
    {
        if (retired || !access || !access->Allowed()) return nullptr;
        auto* current = dynamic_cast<CCSOLOberonBlackHole*>(entity.Get<CBaseEntity>());
        return current && current == identity && current->m_state.get() == this &&
            !(current->pev->flags & FL_KILLME) ? current : nullptr;
    }
    CCSOLOberonBlackHole* Current() const
    {
        const auto owner = session.lock();
        if (!owner || owner->State() != CSOLScenarioSessionState::Active ||
            owner->Map().EntityAccess() != access || owner->Map().RoundGeneration() != round)
            return nullptr;
        return Attached();
    }
    void Remove()
    {
        // Round cancellation still permits retiring this exact old entity.
        // Map revocation permits no more edict access at all.
        if (auto* current = Attached())
        {
            retired = true;
            current->SetThink(nullptr);
            current->pev->nextthink = 0;
            UTIL_Remove(current);
        }
    }
};

namespace
{
bool Finite(const Vector& value)
{ return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }
struct ThinkScope
{
    std::shared_ptr<CSOLOberonBlackHoleState> state;
    explicit ThinkScope(std::shared_ptr<CSOLOberonBlackHoleState> value) : state(std::move(value))
    { state->busy = true; }
    ~ThinkScope() { state->busy = false; }
};
}

bool CCSOLOberonBlackHole::Create(const std::shared_ptr<CCSOLScenarioSession>& session,
    CSOLMonsterSpawnCall& creator, float delay, float strength, csol::scenario::ConfigError& error)
{
    if (!session || !creator.BelongsTo(session->Map()) || !creator.Current()) return false;
    CSOLScenarioSdkScope sdk(CSOL_ScenarioOwnerRules(session.get()));
    const auto access = session->Map().EntityAccess();
    const auto valid = [&]
    {
        if (!access->Allowed() || !sdk.Current()) return false;
        const auto* current = creator.Current();
        return current && current->pev->deadflag != DEAD_DEAD;
    };
    if (!valid()) return false;
    if (!gpGlobals || !std::isfinite(delay) || !std::isfinite(strength) ||
        !std::isfinite(gpGlobals->time + delay) || !session->Models().OberonBlackHoleModelIndex())
    { error.message = "Oberon blackhole requires finite parameters and its precached model"; return false; }
    const Vector origin = creator.Current()->pev->origin;
    if (!Finite(origin)) { error.message = "Oberon blackhole origin is not finite"; return false; }
    const auto name = ALLOC_STRING("blackhole");
    if (!valid()) return false;
    const auto edict = CREATE_NAMED_ENTITY(name);
    if (!access->Allowed()) return false;
    EHANDLE created(edict);
    const auto cancelAllocation = [&]
    {
        // An allocation callback may retire the creator without revoking the
        // map. Retire only the unclaimed object from this exact allocation.
        if (!access->Allowed()) return;
        auto* pending = dynamic_cast<CCSOLOberonBlackHole*>(created.Get<CBaseEntity>());
        if (pending && !pending->m_state && !(pending->pev->flags & FL_KILLME)) UTIL_Remove(pending);
    };
    if (!valid()) { cancelAllocation(); return false; }
    const bool absent = FNullEnt(edict);
    if (!valid()) { cancelAllocation(); return false; }
    // MP113D67D9 permits allocation exhaustion: the Boss start still finishes
    // and its existing timers remain valid, without a temporary blackhole.
    if (absent) return true;
    auto* entity = absent ? nullptr : dynamic_cast<CCSOLOberonBlackHole*>(created.Get<CBaseEntity>());
    if (!entity || entity->m_state || (entity->pev->flags & FL_KILLME))
    { error.message = "Oberon blackhole entity allocation failed"; return false; }
    const auto state = std::make_shared<CSOLOberonBlackHoleState>();
    state->session = session;
    state->access = access;
    state->round = session->Map().RoundGeneration();
    state->entity = entity;
    state->identity = entity;
    entity->m_state = state;
    entity->pev->angles = g_vecZero;
    UTIL_SetOrigin(entity->pev, origin);
    if (!valid() || !state->Current()) { state->Remove(); return false; }
    const int result = DispatchSpawn(edict);
    if (!valid() || !state->Current()) { state->Remove(); return false; }
    if (result < 0 || !state->spawned)
    { state->Remove(); error.message = "Oberon blackhole Spawn failed"; return false; }
    // MP writes these only after Create/Spawn returns, with the then-current
    // engine clock; a nested Think during allocation cannot use partial data.
    const float deadline = gpGlobals->time + delay;
    if (!std::isfinite(deadline))
    { state->Remove(); error.message = "Oberon blackhole deadline is not finite"; return false; }
    state->deadline = deadline;
    state->strength = strength;
    state->ready = true;
    return true;
}

void CCSOLOberonBlackHole::Spawn()
{
    const auto state = m_state;
    if (!state || !state->Current()) return;
    const auto session = state->session.lock();
    CSOLScenarioSdkScope sdk(CSOL_ScenarioOwnerRules(session.get()));
    if (!sdk.Current() || !gpGlobals) return;
    pev->movetype = MOVETYPE_TOSS;
    pev->solid = SOLID_TRIGGER;
    SET_MODEL(edict(), CSOLOberonBlackHoleModel);
    auto* current = sdk.Current() ? state->Current() : nullptr;
    if (!current) return;
    current->SetThink(&CCSOLOberonBlackHole::PullThink);
    current->pev->nextthink = gpGlobals->time + 0.1f;
    current->pev->sequence = 0;
    const bool reset = CSOL_ResetScenarioSequenceInfo(*current, [](const void* value) -> CBaseAnimating*
    { return static_cast<const CSOLOberonBlackHoleState*>(value)->Current(); }, state.get());
    if (sdk.Current() && state->Current()) state->spawned = reset;
}

void CCSOLOberonBlackHole::PullThink()
{
    const auto state = m_state;
    if (!state || state->busy || state->retired) return;
    const auto session = state->session.lock();
    CSOLScenarioSdkScope sdk(CSOL_ScenarioOwnerRules(session.get()));
    if (!sdk.Current() || !state->Current()) { state->Remove(); return; }
    if (!state->ready || !gpGlobals) return;
    ThinkScope busy(state);
    const float now = gpGlobals->time;
    state->Current()->pev->nextthink = now + 0.2f;
    for (int slot = 1; slot <= gpGlobals->maxClients; ++slot)
    {
        const auto edict = INDEXENT(slot);
        if (!sdk.Current() || !state->Current()) { state->Remove(); return; }
        if (!edict || edict->free) continue;
        const EHANDLE playerHandle(edict);
        auto* player = dynamic_cast<CBasePlayer*>(playerHandle.Get<CBaseEntity>());
        if (!player || player->has_disconnected || (player->pev->flags & FL_KILLME) ||
            static_cast<std::uint32_t>(player->pev->flags) == 0x80000000u) continue;
        const bool alive = player->IsAlive() != FALSE;
        auto* current = sdk.Current() ? state->Current() : nullptr;
        if (!current) { state->Remove(); return; }
        if (!alive || playerHandle.Get<CBaseEntity>() != player || player->has_disconnected ||
            (player->pev->flags & FL_KILLME)) continue;
        const Vector delta = current->pev->origin - player->pev->origin;
        const float square = (delta.y * delta.y + delta.x * delta.x) + delta.z * delta.z;
        const float distance = static_cast<float>(std::sqrt(static_cast<double>(square)));
        // The original zero-distance divide yields NaN. Keep that undefined
        // point from corrupting native movement; preserve all finite impulses.
        if (!(distance > 0) || !std::isfinite(distance)) continue;
        const float attenuation = 1.0f - std::min(1.0f, std::max(0.0f, distance * 0.0005644456250593066f));
        const float force = (attenuation * state->strength) / distance;
        const Vector velocity(delta.x * force + player->pev->velocity.x,
            delta.y * force + player->pev->velocity.y, delta.z * force + player->pev->velocity.z);
        if (Finite(velocity)) player->pev->velocity = velocity;
    }
    // Deliberately last and strict: the first overdue tick still attracts.
    if (now > state->deadline) state->Remove();
}

void CCSOLOberonBlackHole::OnDestroy()
{
    if (m_state) m_state->retired = true;
    m_state.reset();
    CBaseAnimating::OnDestroy();
}
