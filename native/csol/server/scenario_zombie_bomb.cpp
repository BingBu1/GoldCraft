// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_zombie_bomb.h"
#include "scenario_monster_bomb.h"
#include "scenario_grenade_blockers.h"
#include "scenario_runtime.h"
#include "scenario_session.h"
#include "game_rules_gate.h"
#include "func_break.h"
#include "soundent.h"
#include <algorithm>
#include <cmath>

#pragma clang fp contract(off)
LINK_ENTITY_TO_CLASS(csol_zombiebomb, CCSOLZombieBomb, CCSEntity)

struct CSOLZombieBombState
{
    std::weak_ptr<CCSOLScenarioRuntime> runtime;
    std::shared_ptr<CCSOLScenarioSession> session;
    std::shared_ptr<const CSOLScenarioEntityAccess> access;
    EHANDLE entity, creator;
    const CCSOLZombieBomb* identity{};
    std::uint64_t round{}, creatorLease{};
    bool ready{}, spawned{}, retired{}, busy{}, dangerRegistered{};

    CCSOLZombieBomb* Attached() const
    {
        if (retired || !access || !access->Allowed()) return nullptr;
        auto* bomb = dynamic_cast<CCSOLZombieBomb*>(entity.Get<CBaseEntity>());
        return bomb && bomb == identity && bomb->m_state.get() == this &&
            !(bomb->pev->flags & FL_KILLME) ? bomb : nullptr;
    }
    CCSOLZombieBomb* Current() const
    {
        const auto owner = runtime.lock();
        if (!owner || owner->Closing() || !session || owner->session != session ||
            session->State() != CSOLScenarioSessionState::Active ||
            session->Map().EntityAccess() != access || session->Map().RoundGeneration() != round) return nullptr;
        return Attached();
    }
    CBaseEntity* Creator() const
    {
        if (!Current()) return nullptr;
        auto* monster = dynamic_cast<CCSOLScenarioMonster*>(creator.Get<CBaseEntity>());
        return monster && !(monster->pev->flags & FL_KILLME) &&
            monster->Pool() == &session->Monsters() && session->Monsters().Borrowed(*monster) &&
            session->Monsters().Lease(*monster) == creatorLease ? monster : nullptr;
    }
    void Remove()
    {
        if (auto* bomb = Attached())
        {
            retired = true;
            bomb->SetThink(nullptr);
            bomb->SetTouch(nullptr);
            bomb->pev->nextthink = 0;
            UTIL_Remove(bomb);
        }
    }
};

namespace
{
struct Operation
{
    std::shared_ptr<CSOLZombieBombState> state;
    explicit Operation(std::shared_ptr<CSOLZombieBombState> value) : state(std::move(value))
    { state->busy = true; }
    ~Operation() { state->busy = false; }
};
bool Finite(const Vector& value)
{ return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }
float NextThink() { return static_cast<float>(static_cast<double>(gpGlobals->time) + 0.1); }
}

bool CSOL_CreateMonsterZombieBomb(CSOLMonsterSpawnCall& call,
    const std::shared_ptr<CCSOLScenarioRuntime>& runtime, const Vector& origin,
    const Vector& velocity, float damage, csol::scenario::ConfigError& error)
{
    return CCSOLZombieBomb::Create(call, runtime, origin, velocity, damage, error);
}

bool CCSOLZombieBomb::Create(CSOLMonsterSpawnCall& call,
    const std::shared_ptr<CCSOLScenarioRuntime>& runtime, const Vector& origin,
    const Vector& velocity, float damage, csol::scenario::ConfigError& error)
{
    const auto session = runtime ? runtime->session : nullptr;
    if (!session || !call.BelongsTo(session->Map()) || !call.Current()) return false;
    CSOLScenarioSdkScope sdk(CSOL_ScenarioOwnerRules(session.get()));
    const auto access = session->Map().EntityAccess();
    const auto current = [&]
    {
        if (!sdk.Current() || !access || !access->Allowed() || runtime->Closing() ||
            runtime->session != session) return false;
        const auto* monster = call.Current();
        return monster && monster->Pool() == &session->Monsters() && session->Monsters().Borrowed(*monster);
    };
    if (!current() || !gpGlobals) return false;
    if (!Finite(origin) || !Finite(velocity) || !std::isfinite(damage) ||
        !std::isfinite(gpGlobals->time) || !g_engfuncs.pfnRandomLong)
    { error.message = "scenario zombie bomb launch or random service is invalid"; return false; }
    const Vector angles = call.Current()->pev->angles;
    const int team = call.Current()->pev->team;
    const EHANDLE creator(call.Current());
    const auto lease = session->Monsters().Lease(*call.Current());
    const auto name = ALLOC_STRING("csol_zombiebomb");
    if (!current()) return false;
    auto* edict = CREATE_NAMED_ENTITY(name);
    if (!access->Allowed()) return false;
    const EHANDLE allocated(edict);
    const auto discard = [&]
    {
        if (!access->Allowed()) return;
        auto* bomb = dynamic_cast<CCSOLZombieBomb*>(allocated.Get<CBaseEntity>());
        if (bomb && !bomb->m_state && !(bomb->pev->flags & FL_KILLME)) UTIL_Remove(bomb);
    };
    if (!current()) { discard(); return false; }
    const bool absent = FNullEnt(edict);
    if (!current()) { discard(); return false; }
    if (absent) return true; // Exhaustion still consumes the throw's cooldown.
    auto* bomb = dynamic_cast<CCSOLZombieBomb*>(allocated.Get<CBaseEntity>());
    if (!bomb || bomb->m_state)
    { error.message = "scenario zombie bomb allocation has invalid private data"; return false; }
    const auto state = std::make_shared<CSOLZombieBombState>();
    state->runtime = runtime; state->session = session; state->access = access;
    state->round = session->Map().RoundGeneration(); state->entity = bomb; state->identity = bomb;
    state->creator = creator; state->creatorLease = lease;
    bomb->m_state = state;
    const int spawned = DispatchSpawn(edict);
    if (!current() || !state->Current()) { state->Remove(); return false; }
    if (spawned < 0 || !state->spawned)
    { state->Remove(); error.message = "scenario zombie bomb Spawn failed"; return false; }
    UTIL_SetOrigin(state->Current()->pev, origin);
    if (!current() || !state->Current()) { state->Remove(); return false; }
    bomb = state->Current();
    bomb->pev->velocity = velocity;
    bomb->pev->angles = angles;
    bomb->pev->owner = state->creator.Get();
    bomb->pev->team = team;
    bomb->pev->dmgtime = gpGlobals->time + 1.5f;
    bomb->pev->nextthink = NextThink();
    const int sequence = RANDOM_LONG(3, 6);
    if (!current() || !state->Current()) { state->Remove(); return false; }
    bomb = state->Current();
    bomb->pev->sequence = sequence;
    bomb->pev->framerate = 1.0f;
    bomb->pev->gravity = 0.55f;
    bomb->pev->friction = 0.7f;
    // MP114ADA10 replaces the ordinary grenade model after initialization.
    SET_MODEL(bomb->edict(), CSOLZombieBombModel);
    if (!current() || !state->Current()) { state->Remove(); return false; }
    bomb = state->Current();
    bomb->pev->dmg = 999.0f;
    bomb->pev->health = damage;
    state->ready = true;
    return true;
}

void CCSOLZombieBomb::Spawn()
{
    const auto state = m_state;
    if (!state || !state->Current()) return;
    CSOLScenarioSdkScope sdk(CSOL_ScenarioOwnerRules(state->session.get()));
    if (!sdk.Current()) return;
    pev->movetype = MOVETYPE_BOUNCE;
    pev->solid = SOLID_BBOX;
    SET_MODEL(edict(), "models/grenade.mdl");
    auto* bomb = sdk.Current() ? state->Current() : nullptr;
    if (!bomb) return;
    UTIL_SetSize(bomb->pev, g_vecZero, g_vecZero);
    bomb = sdk.Current() ? state->Current() : nullptr;
    if (!bomb) return;
    bomb->SetThink(&CCSOLZombieBomb::FlightThink);
    bomb->SetTouch(&CCSOLZombieBomb::BounceTouch);
    state->spawned = true;
}

void CCSOLZombieBomb::FlightThink()
{
    const auto state = m_state;
    if (!state || state->busy || state->retired) return;
    CSOLScenarioSdkScope sdk(CSOL_ScenarioOwnerRules(state->session.get()));
    if (!sdk.Current() || !state->Current()) { state->Remove(); return; }
    if (!state->ready || !gpGlobals) return;
    Operation operation(state);
    auto* bomb = state->Current();
    const float limit = g_psv_maxvelocity ? g_psv_maxvelocity->value : 0;
    if (!(limit > 0) || !Finite(bomb->pev->velocity) || !Finite(bomb->pev->origin))
    { state->Remove(); return; }
    // Original vB8 uses sv_maxvelocity and strict limits. Native CS's
    // IsInWorld also permits equality in REGAMEDLL_FIXES; CSOL does not.
    for (int axis = 0; axis != 3; ++axis)
        if (!(bomb->pev->origin[axis] > -4096.0f && bomb->pev->origin[axis] < 4096.0f &&
              bomb->pev->velocity[axis] > -limit && bomb->pev->velocity[axis] < limit))
        { state->Remove(); return; }
    auto* creator = state->Creator();
    bomb->pev->owner = creator ? creator->edict() : nullptr;
    const Vector velocity = bomb->pev->velocity;
    const float square = (velocity.x * velocity.x + velocity.y * velocity.y) + velocity.z * velocity.z;
    bomb->pev->framerate = std::min(100.0f,
        static_cast<float>(std::sqrt(static_cast<double>(square)))) * 0.04f;
    bomb->StudioFrameAdvance(0);
    bomb = sdk.Current() ? state->Current() : nullptr;
    if (!bomb) { state->Remove(); return; }
    bomb->pev->nextthink = NextThink();
    if (gpGlobals->time > bomb->pev->dmgtime - 1.0f)
    {
        const Vector predicted = bomb->pev->origin + bomb->pev->velocity * (bomb->pev->dmgtime - gpGlobals->time);
        CSoundEnt::InsertSound(bits_SOUND_DANGER, predicted, 400, 0.1f);
        bomb = sdk.Current() ? state->Current() : nullptr;
        if (!bomb) { state->Remove(); return; }
    }
    if (bomb->pev->waterlevel != 0)
    { bomb->pev->velocity *= 0.5f; bomb->pev->framerate = 0.2f; }
    // MP114AEDD7 only changes the next callback; detonation occurs at the
    // already scheduled +0.1 think, not in this flight callback.
    if (gpGlobals->time >= bomb->pev->dmgtime) bomb->SetThink(&CCSOLZombieBomb::Explode);
    // The common tail repeats the water reduction in mode15, including
    // the final flight tick. Preserve both multiplications and their order.
    if (bomb->pev->waterlevel != 0)
    { bomb->pev->velocity *= 0.5f; bomb->pev->framerate = 0.2f; }
}

void CCSOLZombieBomb::BounceTouch(CBaseEntity* other)
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
    if (breakable && dynamic_cast<CBreakable*>(other) && other->pev->rendermode != kRenderNormal &&
        !CSOL_BreakableBlocksGrenade(*other))
    { pev->velocity *= -2.0f; return; }
    if (!state->dangerRegistered)
    {
        const Vector velocity = pev->velocity;
        const float z = static_cast<float>(static_cast<double>(velocity.z) * 0.7);
        if ((velocity.y * velocity.y + velocity.x * velocity.x) + z * z <= 3600.0f)
        {
            CSoundEnt::InsertSound(bits_SOUND_DANGER, pev->origin,
                static_cast<int>(static_cast<double>(pev->dmg) / 0.4), 0.3f);
            if (!sdk.Current() || !state->Current()) return;
            state->dangerRegistered = true;
        }
    }
    if (pev->flags & FL_ONGROUND)
    {
        pev->velocity *= 0.8f;
        const int sequence = RANDOM_LONG(1, 1);
        if (!sdk.Current() || !state->Current()) return;
        pev->sequence = sequence;
    }
    else
    {
        const int sound = RANDOM_LONG(0, 1);
        if (!sdk.Current() || !state->Current()) return;
        EMIT_SOUND_DYN(edict(), CHAN_VOICE, CSOLZombieBombBounceSounds[sound], 0.25f, 0.8f, 0, 100);
    }
}

void CCSOLZombieBomb::Explode()
{
    const auto state = m_state;
    if (!state || state->busy || state->retired || !state->ready) return;
    CSOLScenarioSdkScope sdk(CSOL_ScenarioOwnerRules(state->session.get()));
    if (!sdk.Current() || !state->Current()) { state->Remove(); return; }
    Operation operation(state);
    pev->model = 0;
    pev->solid = SOLID_NOT;
    pev->takedamage = DAMAGE_NO;
    const bool sent = state->session->Messages().SendMonsterZombieBomb(pev->origin);
    auto* bomb = sdk.Current() ? state->Current() : nullptr;
    if (!bomb) { state->Remove(); return; }
    if (!sent) { state->Remove(); CSOL_RejectGameRules("scenario zombie bomb message failed"); return; }
    bomb->pev->owner = nullptr;
    const CSOLZombieBombHitSource source{
        [](const void* value) -> CBaseEntity* { return static_cast<const CSOLZombieBombState*>(value)->Current(); },
        [](const void* value) -> CBaseEntity* { return static_cast<const CSOLZombieBombState*>(value)->Creator(); },
        state.get()};
    csol::scenario::ConfigError error;
    const bool damaged = CSOL_ApplyZombieBombDamage(state->runtime.lock(), source, bomb->pev->health, error);
    bomb = sdk.Current() ? state->Current() : nullptr;
    if (bomb)
    {
        bomb->pev->effects |= EF_NODRAW;
        bomb->pev->velocity = g_vecZero;
    }
    state->Remove();
    if (bomb && !damaged && !error.message.empty()) CSOL_RejectGameRules(error.message.c_str());
}

void CCSOLZombieBomb::OnDestroy()
{
    const auto state = std::move(m_state);
    if (state) state->retired = true;
    CBaseAnimating::OnDestroy();
}

bool CCSOLZombieBomb::IsDamageSource() const
{
    const auto state = m_state;
    return state && state->ready && state->Current() == this && pev->dmg == 999.0f;
}

CBaseEntity* CCSOLZombieBomb::DamageCreator() const
{
    const auto state = m_state;
    return state ? state->Creator() : nullptr;
}
