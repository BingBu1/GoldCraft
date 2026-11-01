// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_oberon_projectile.h"
#include "scenario_oberon_resources.h"
#include "scenario_oberon_attacks.h"
#include "scenario_grenade_blockers.h"
#include "scenario_runtime.h"
#include "scenario_session.h"
#include "game_rules_gate.h"
#include "func_break.h"
#include <cmath>
#include <cstdlib>

#pragma clang fp contract(off)
LINK_ENTITY_TO_CLASS(oberongren, CCSOLOberonProjectile, CCSEntity)

struct CSOLOberonProjectileState
{
    std::weak_ptr<CCSOLScenarioRuntime> runtime;
    std::shared_ptr<CCSOLScenarioSession> session;
    std::shared_ptr<const CSOLScenarioEntityAccess> access;
    EHANDLE entity, creator;
    const CCSOLOberonProjectile* identity{};
    std::uint64_t round{}, creatorLease{};
    float deadline{}, damage{}, radius{}, kickSide{}, kickUp{};
    int explosion{};
    bool ready{}, spawned{}, retired{}, busy{};
    CCSOLOberonProjectile* Attached() const
    {
        if (retired || !access || !access->Allowed()) return nullptr;
        auto* current = dynamic_cast<CCSOLOberonProjectile*>(entity.Get<CBaseEntity>());
        return current == identity && current && current->m_state.get() == this &&
            !(current->pev->flags & FL_KILLME) ? current : nullptr;
    }
    CCSOLOberonProjectile* Current() const
    {
        const auto owner = runtime.lock();
        if (!owner || owner->Closing() || owner->session != session ||
            session->State() != CSOLScenarioSessionState::Active ||
            session->Map().EntityAccess() != access || session->Map().RoundGeneration() != round) return nullptr;
        return Attached();
    }
    CBaseEntity* Creator() const
    {
        if (!Current()) return nullptr;
        auto* monster = dynamic_cast<CCSOLScenarioMonster*>(creator.Get<CBaseEntity>());
        return monster && monster->Pool() == &session->Monsters() &&
            session->Monsters().Lease(*monster) == creatorLease &&
            session->Monsters().Borrowed(*monster) ? monster : nullptr;
    }
    void Remove()
    {
        if (auto* current = Attached())
        {
            retired = true;
            current->SetThink(nullptr);
            current->SetTouch(nullptr);
            current->pev->nextthink = 0;
            UTIL_Remove(current);
        }
    }
};
namespace
{
struct Operation
{
    std::shared_ptr<CSOLOberonProjectileState> state;
    explicit Operation(std::shared_ptr<CSOLOberonProjectileState> value) : state(std::move(value)) { state->busy = true; }
    ~Operation() { state->busy = false; }
};
bool Finite(const Vector& vector)
{ return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z); }
}
bool CCSOLOberonProjectile::CreateBatch(const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    CSOLMonsterSpawnCall& call, const csol::scenario::BossSkillRow& row, csol::scenario::ConfigError& error)
{
    for (int slot = 1; slot <= 10; ++slot)
        if (!Create(runtime, call, row, slot, slot * 20, error)) return false;
    const auto* current = call.Current();
    return current && current->pev->deadflag != DEAD_DEAD;
}
bool CCSOLOberonProjectile::Create(const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    CSOLMonsterSpawnCall& call, const csol::scenario::BossSkillRow& row,
    int targetSlot, int offset, csol::scenario::ConfigError& error)
{
    const auto session = runtime ? runtime->session : nullptr;
    if (!session || !call.BelongsTo(session->Map()) || !call.Current()) return false;
    CSOLScenarioSdkScope sdk(CSOL_ScenarioOwnerRules(session.get()));
    const auto access = session->Map().EntityAccess();
    const auto valid = [&]
    {
        if (!sdk.Current() || !access->Allowed() || runtime->Closing() || runtime->session != session) return false;
        const auto* current = call.Current();
        return current && current->pev->deadflag != DEAD_DEAD;
    };
    if (!valid() || !gpGlobals) return false;
    std::optional<std::array<float, 2>> destination;
    if (targetSlot <= gpGlobals->maxClients)
    {
        const auto edict = INDEXENT(targetSlot);
        if (!valid()) return false;
        if (edict && !edict->free)
        {
            const EHANDLE target(edict);
            auto* player = dynamic_cast<CBasePlayer*>(target.Get<CBaseEntity>());
            if (player && static_cast<std::uint32_t>(player->pev->flags) != 0x80000000u)
            {
                const bool alive = player->IsAlive() != FALSE;
                if (!valid()) return false;
                if (alive && target.Get<CBaseEntity>() == player && !player->has_disconnected && !(player->pev->flags & FL_KILLME))
                    destination = {player->pev->origin.x, player->pev->origin.y};
            }
        }
    }
    auto* monster = call.Current();
    if (!destination)
    {
        // 113D6FF2 generates Y first, then X, using the CRT stream. Each
        // signed magnitude is doubled before adding the original coordinate.
        float y = float(std::rand() % 10 + 1) * 39.369998931884766f;
        y *= float(2 * (std::rand() % 2) - 1);
        float x = float(std::rand() % 10 + 1) * 39.369998931884766f;
        x *= float(2 * (std::rand() % 2) - 1);
        destination = {(x + x) + monster->pev->origin.x, (y + y) + monster->pev->origin.y};
    }
    const auto launch = csol::scenario::oberon::Launch(
        {monster->pev->origin.x, monster->pev->origin.y, monster->pev->origin.z},
        monster->pev->angles.y, offset, *destination);
    const auto name = ALLOC_STRING("oberongren");
    if (!valid()) return false;
    const auto edict = CREATE_NAMED_ENTITY(name);
    if (!access->Allowed()) return false;
    const EHANDLE allocated(edict);
    const auto discard = [&]
    {
        if (!access->Allowed()) return;
        auto* value = dynamic_cast<CCSOLOberonProjectile*>(allocated.Get<CBaseEntity>());
        if (value && !value->m_state && !(value->pev->flags & FL_KILLME)) UTIL_Remove(value);
    };
    if (!valid()) { discard(); return false; }
    const bool absent = FNullEnt(edict);
    if (!valid()) { discard(); return false; }
    if (absent) return true;
    auto* entity = dynamic_cast<CCSOLOberonProjectile*>(allocated.Get<CBaseEntity>());
    if (!entity || entity->m_state) { error.message = "Oberon grenade allocation has invalid private data"; return false; }
    const auto state = std::make_shared<CSOLOberonProjectileState>();
    state->runtime = runtime; state->session = session; state->access = access;
    state->round = session->Map().RoundGeneration(); state->entity = entity; state->identity = entity;
    state->creator = call.Current();
    state->creatorLease = call.Current()->Pool()->Lease(*call.Current());
    entity->m_state = state;
    entity->pev->owner = state->creator.Get();
    entity->pev->angles = g_vecZero;
    UTIL_SetOrigin(entity->pev, Vector(launch.origin.data()));
    if (!valid() || !state->Current()) { state->Remove(); return false; }
    const int spawned = DispatchSpawn(edict);
    if (!valid() || !state->Current()) { state->Remove(); return false; }
    if (spawned < 0 || !state->spawned)
    { state->Remove(); error.message = "Oberon grenade Spawn failed"; return false; }
    const int explosion = MODEL_INDEX(CSOLOberonExplosionSprite);
    if (!valid() || !state->Current()) { state->Remove(); return false; }
    if (explosion <= 0) { state->Remove(); error.message = "Oberon explosion is not precached"; return false; }
    static_assert(RAND_MAX == 32767);
    const float randomLifetime = (float(std::rand()) * (1.0f / 32767.0f)) * (0.2f - -0.2f) + -0.2f;
    state->deadline = gpGlobals->time + (launch.flightTime + randomLifetime);
    state->damage = row.damage; state->radius = row.damageDistance;
    state->kickSide = row.kickSide; state->kickUp = row.kickUp; state->explosion = explosion;
    entity = state->Current();
    entity->pev->velocity = Vector(launch.velocity.data());
    entity->pev->dmg = row.damage;
    entity->pev->health = row.damageDistance;
    entity->pev->dmgtime = state->deadline;
    entity->pev->gravity = 0.55f; entity->pev->friction = 0.7f; entity->pev->framerate = 4;
    state->ready = true;
    return true;
}
void CCSOLOberonProjectile::Spawn()
{
    const auto state = m_state;
    if (!state || !state->Current() || !gpGlobals) return;
    CSOLScenarioSdkScope sdk(CSOL_ScenarioOwnerRules(state->session.get()));
    if (!sdk.Current()) return;
    pev->movetype = MOVETYPE_BOUNCE;
    pev->solid = SOLID_BBOX;
    SET_MODEL(edict(), CSOLOberonBombModel);
    auto* current = sdk.Current() ? state->Current() : nullptr;
    if (!current) return;
    UTIL_SetSize(current->pev, g_vecZero, g_vecZero);
    current = sdk.Current() ? state->Current() : nullptr;
    if (!current) return;
    current->pev->sequence = 0;
    current->pev->frame = 0;
    if (!CSOL_ResetScenarioSequenceInfo(*current, [](const void* value) -> CBaseAnimating*
        { return static_cast<const CSOLOberonProjectileState*>(value)->Current(); }, state.get())) return;
    current = sdk.Current() ? state->Current() : nullptr;
    if (!current) return;
    current->SetThink(&CCSOLOberonProjectile::FlightThink);
    current->SetTouch(&CCSOLOberonProjectile::BounceTouch);
    current->pev->nextthink = gpGlobals->time + 0.1f;
    state->spawned = true;
}
void CCSOLOberonProjectile::FlightThink()
{
    const auto state = m_state;
    if (!state || state->busy || state->retired) return;
    CSOLScenarioSdkScope sdk(CSOL_ScenarioOwnerRules(state->session.get()));
    if (!sdk.Current() || !state->Current()) { state->Remove(); return; }
    if (!state->ready || !gpGlobals) return;
    Operation operation(state);
    const float limit = g_psv_maxvelocity ? g_psv_maxvelocity->value : 0;
    if (!(limit > 0) || !Finite(pev->origin) || !Finite(pev->velocity)) { state->Remove(); return; }
    for (int axis = 0; axis != 3; ++axis)
        if (!(pev->origin[axis] > -4096.0f && pev->origin[axis] < 4096.0f &&
              pev->velocity[axis] > -limit && pev->velocity[axis] < limit)) { state->Remove(); return; }
    if (gpGlobals->time > state->deadline)
    {
        Vector visual = pev->origin;
        for (int axis = 0; axis != 3; ++axis)
        {
            const float offset = RANDOM_FLOAT(axis == 2 ? 10.0f : -32.0f, axis == 2 ? 25.0f : 32.0f);
            if (!sdk.Current() || !state->Current()) { state->Remove(); return; }
            visual[axis] += offset;
        }
        const Vector origin = state->Current()->pev->origin;
        const bool sent = state->session->Messages().SendOberonExplosion(origin, visual, state->explosion);
        if (!sdk.Current() || !state->Current()) { state->Remove(); return; }
        if (!sent) { state->Remove(); CSOL_RejectGameRules("Oberon explosion message failed"); return; }
        const CSOLOberonAttackGuard guard{[](const void* value) -> CBaseEntity*
            { return static_cast<const CSOLOberonProjectileState*>(value)->Current(); }, state.get(),
            [](const void* value) -> CBaseEntity*
            { return static_cast<const CSOLOberonProjectileState*>(value)->Creator(); }};
        csol::scenario::ConfigError error;
        const bool damaged = CSOL_OberonBombDamage(state->runtime.lock(), guard,
            state->damage, state->radius, state->kickSide, state->kickUp, error);
        const bool current = sdk.Current() && state->Current();
        state->Remove();
        if (current && !damaged && !error.message.empty()) CSOL_RejectGameRules(error.message.c_str());
        return;
    }
    StudioFrameAdvance(0);
    if (!sdk.Current() || !state->Current()) { state->Remove(); return; }
    pev->nextthink = gpGlobals->time + 0.1f;
    if (pev->waterlevel != 0) { pev->velocity *= 0.5f; pev->framerate = 0.2f; }
}
void CCSOLOberonProjectile::BounceTouch(CBaseEntity* other)
{
    const auto state = m_state;
    if (!state || state->busy || state->retired || !state->ready || !other) return;
    CSOLScenarioSdkScope sdk(CSOL_ScenarioOwnerRules(state->session.get()));
    if (!sdk.Current() || !state->Current()) { state->Remove(); return; }
    Operation operation(state);
    auto* creator = state->Creator();
    pev->owner = creator ? creator->edict() : nullptr;
    if (other == creator) return;
    const EHANDLE touched(other);
    const bool breakable = FClassnameIs(other->pev, "func_breakable");
    if (!sdk.Current() || !state->Current() || touched.Get<CBaseEntity>() != other) return;
    if (breakable && dynamic_cast<CBreakable*>(other) && other->pev->takedamage != DAMAGE_NO &&
        !CSOL_BreakableBlocksGrenade(*other))
    { pev->velocity *= -2.0f; return; }
    if (pev->flags & FL_ONGROUND)
    {
        pev->velocity *= 0.8f;
        const int sequence = RANDOM_LONG(1, 1);
        if (!sdk.Current() || !state->Current()) return;
        // Original writes sequence1 without ResetSequenceInfo. This model's
        // single-sequence rendering compatibility still needs live checking.
        pev->sequence = sequence;
    }
    else EMIT_SOUND_DYN(edict(), CHAN_VOICE, CSOLOberonBombSounds[std::rand() % 2], 0.25f, 0.8f, 0, 100);
}
void CCSOLOberonProjectile::OnDestroy()
{
    const auto state = std::move(m_state);
    if (state) state->retired = true;
    CBaseAnimating::OnDestroy();
}
