// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "slow_wall.hpp"
#include "host_notifications.hpp"
#include "../scenario_player_runtime.h"
#include "../scenario_session.h"
#include "../scenario_monster_pool.h"
#include "../game_rules_gate.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

#pragma clang fp contract(off)

class CCSOLCharacterSlowWall;
namespace
{
using csol::scenario::ConfigError;
constexpr char WallModel[] = "models/ef_y21s3buffclassct.mdl";
struct SlowWallState
{
    std::weak_ptr<CCSOLScenarioPlayerRuntime> players;
    std::weak_ptr<CCSOLScenarioSession> session;
    std::shared_ptr<const CSOLScenarioEntityAccess> access;
    EHANDLE entity;
    const CCSOLCharacterSlowWall* identity{};
    std::uint64_t round{};
    float expires{};
    // The skill must remain current until allocation/Spawn commits. Once
    // ready, the wall owns its original independent lifetime.
    std::function<bool()> creatingOwner;
    bool spawned{}, ready{}, retired{}, touching{};
    CCSOLCharacterSlowWall* Attached() const;
    CCSOLCharacterSlowWall* Current() const;
    void Remove();
};

bool Finite(const Vector& value)
{ return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }

bool NoHit(const TraceResult& trace)
{ return std::fabs(static_cast<double>(trace.flFraction) - 1.0) < std::numeric_limits<float>::epsilon(); }

// MP106E9040 and106DBD80 use ordinary line traces, not an eye trace or
// a hull placement. Keep the original three fallbacks and slope predicate.
template<class Current>
bool Placement(CBasePlayer& actor, Current current, Vector& position)
{
    UTIL_MakeVectors(actor.pev->v_angle);
    if (!current()) return false;
    const Vector forward = gpGlobals->v_forward;
    const float squared = (forward.x * forward.x + forward.y * forward.y) + 0.0f;
    const float length = static_cast<float>(std::sqrt(static_cast<double>(squared)));
    const Vector horizontal = length > 0 ? Vector(forward.x / length, forward.y / length, 0.0f / length) : Vector(0, 0, 1);
    TraceResult trace{};
    const auto cast = [&](const Vector& from, const Vector& to, IGNORE_MONSTERS monsters)
    {
        UTIL_TraceLine(from, to, monsters, actor.edict(), &trace);
        return current();
    };
    Vector endpoint = actor.pev->origin + forward * 400;
    if (!cast(actor.pev->origin, endpoint, dont_ignore_monsters)) return false;
    if (NoHit(trace))
    {
        endpoint = actor.pev->origin + horizontal * 400;
        if (!cast(actor.pev->origin, endpoint, dont_ignore_monsters)) return false;
        if (NoHit(trace))
        {
            endpoint = actor.pev->origin + horizontal * 400;
            if (!cast(endpoint, endpoint - Vector(0, 0, 100), dont_ignore_monsters)) return false;
            if (NoHit(trace)) { position = endpoint - Vector(0, 0, 35); return Finite(position); }
        }
    }
    position = Vector(0, 0, 0);
    if (!trace.pHit) return true; // Original leaves its zero-initialized position.
    auto* hit = CBaseEntity::Instance(trace.pHit);
    if (!current()) return false;
    if (!hit) return false;
    const bool bsp = hit->IsBSPModel();
    if (!current()) return false;
    const auto normal = trace.vecPlaneNormal;
    // The original really adds normal.y twice; it does not square that axis.
    const float slope = (normal.x * normal.x + normal.y) + normal.y;
    if (bsp && !(slope > normal.z) && !(0.0f > normal.z))
    { position = trace.vecEndPos; return Finite(position); }
    Vector candidate = trace.vecEndPos - horizontal * 30;
    const auto delta = candidate - actor.pev->origin;
    const float distance = (delta.x * delta.x + delta.y * delta.y) + delta.z * delta.z;
    if (50.0f * 50.0f > distance) candidate = actor.pev->origin + horizontal * 50;
    if (!cast(candidate, candidate - Vector(0, 0, 100), ignore_monsters)) return false;
    position = NoHit(trace) ? candidate - Vector(0, 0, 35) : trace.vecEndPos;
    return Finite(position);
}
}

class CCSOLCharacterSlowWall final : public CBaseAnimating
{
public:
    void Spawn() override;
    void Touch(CBaseEntity* other) override;
    void OnDestroy() override;
    void WallThink();
    static bool Create(const std::shared_ptr<CCSOLScenarioPlayerRuntime>&,
        const std::shared_ptr<CCSOLScenarioSession>&, const Vector&, const Vector&,
        float duration, std::function<bool()> ownerCurrent, ConfigError&);
private:
    friend struct SlowWallState;
    std::shared_ptr<SlowWallState> m_state;
};
LINK_ENTITY_TO_CLASS(slowwall, CCSOLCharacterSlowWall, CCSEntity)

CCSOLCharacterSlowWall* SlowWallState::Attached() const
{
    if (retired || !access || !access->Allowed()) return nullptr;
    auto* wall = dynamic_cast<CCSOLCharacterSlowWall*>(entity.Get<CBaseEntity>());
    return wall && wall == identity && wall->m_state.get() == this && wall->pev &&
        !(wall->pev->flags & FL_KILLME) ? wall : nullptr;
}
CCSOLCharacterSlowWall* SlowWallState::Current() const
{
    if (retired || (!ready && creatingOwner && !creatingOwner())) return nullptr;
    const auto map = session.lock(); const auto runtime = players.lock();
    return map && runtime && runtime->Active() && map->Map().EntityAccess() == access &&
        map->Map().RoundGeneration() == round ? Attached() : nullptr;
}
void SlowWallState::Remove()
{
    auto* wall = Attached();
    retired = true;
    creatingOwner = {};
    if (!wall) return;
    wall->SetThink(nullptr); wall->pev->nextthink = 0;
    wall->pev->solid = SOLID_NOT;
    UTIL_Remove(wall);
}

void CCSOLCharacterSlowWall::Spawn()
{
    const auto state = m_state;
    if (!state || !state->Current() || !gpGlobals || !g_engfuncs.pfnModelIndex ||
        !g_engfuncs.pfnSetModel || !g_engfuncs.pfnGetModelPtr || !g_engfuncs.pfnSetSize) return;
    const int model = MODEL_INDEX(WallModel);
    if (!state->Current() || model <= 0) return;
    pev->movetype = MOVETYPE_FLY; pev->solid = SOLID_TRIGGER;
    SET_MODEL(edict(), WallModel);
    if (!state->Current()) return;
    auto* studio = GET_MODEL_PTR(edict());
    if (!state->Current() || !studio) return;
    Vector minimum{}, maximum{};
    if (!::ExtractBbox(studio, 0, minimum, maximum) || !Finite(minimum) || !Finite(maximum)) return;
    UTIL_SetSize(pev, minimum, maximum);
    if (!state->Current()) return;
    pev->rendermode = kRenderTransAlpha; pev->renderamt = 250;
    state->spawned = true;
    SetThink(&CCSOLCharacterSlowWall::WallThink);
    pev->nextthink = std::min(state->expires, gpGlobals->time + .1f);
}

void CCSOLCharacterSlowWall::WallThink()
{
    const auto state = m_state;
    if (!state || state->retired) return;
    const auto session = state->session.lock();
    CSOLScenarioSdkScope sdk(CSOL_ScenarioOwnerRules(session.get()));
    if (!sdk.Current() || !state->Current() || !gpGlobals || !std::isfinite(gpGlobals->time) ||
        gpGlobals->time >= state->expires) { state->Remove(); return; }
    pev->nextthink = std::min(state->expires, gpGlobals->time + .1f);
}

void CCSOLCharacterSlowWall::Touch(CBaseEntity* other)
{
    const auto state = m_state;
    if (!state || !state->ready || state->touching || !state->Current() || !gpGlobals ||
        !other || !other->pev || (other->pev->flags & FL_KILLME)) return;
    const auto session = state->session.lock();
    CSOLScenarioSdkScope sdk(CSOL_ScenarioOwnerRules(session.get()));
    if (!sdk.Current()) return;
    state->touching = true;
    struct Guard { bool& flag; ~Guard() { flag = false; } } guard{state->touching};
    const EHANDLE handle(other);
    auto* monster = dynamic_cast<CCSOLScenarioMonster*>(other);
    auto* pool = monster ? monster->Pool() : nullptr;
    if (!monster || !pool || !pool->Borrowed(*monster)) return;
    const auto lease = pool->Lease(*monster);
    const auto current = [&] { return sdk.Current() && state->Current() &&
        handle.Get<CBaseEntity>() == monster && monster->Pool() == pool &&
        pool->Borrowed(*monster) && pool->Lease(*monster) == lease; };
    const int classification = monster->Classify();
    if (!current() || (classification != 17 && classification != 45)) return;
    const auto* type = monster->Type();
    if (type && (type->skill >= 1000 || type->eventClass > 149)) return;
    const auto& effects = monster->SpeedState().effects;
    const bool present = std::any_of(effects.begin(), effects.end(), [](const auto& effect) { return effect.id == 2; });
    if (!present)
    {
        ConfigError error;
        if (!csol::scenario::skills::PublishSlowWallMonsterEffect(*session, handle, error) || !current()) return;
    }
    //1085CB00 updates an existing ID even if its old deadline has passed;
    //1085CC70's preceding presence check deliberately does not expire it.
    monster->SpeedState().AddEffect(2, .3f, -100, 10, 1, monster->pev->velocity, gpGlobals->time);
}

void CCSOLCharacterSlowWall::OnDestroy()
{
    const auto state = std::move(m_state);
    if (state) { state->retired = true; state->creatingOwner = {}; }
    CBaseAnimating::OnDestroy();
}

bool CCSOLCharacterSlowWall::Create(const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CCSOLScenarioSession>& session, const Vector& origin,
    const Vector& angles, float duration, std::function<bool()> ownerCurrent, ConfigError& error)
{
    const auto access = session->Map().EntityAccess();
    const auto round = session->Map().RoundGeneration();
    const auto current = [&] { return ownerCurrent && ownerCurrent() && players->Active() && access && access->Allowed() &&
        session->Map().EntityAccess() == access && session->Map().RoundGeneration() == round; };
    if (!current() || !gpGlobals || !g_engfuncs.pfnCreateNamedEntity || !g_engfuncs.pfnAllocString) return false;
    const float expires = gpGlobals->time + duration;
    if (!std::isfinite(expires)) return false;
    const int name = ALLOC_STRING("slowwall");
    if (!current()) return false;
    auto* edict = CREATE_NAMED_ENTITY(name);
    if (!access->Allowed()) return false;
    const EHANDLE allocated(edict);
    auto* wall = dynamic_cast<CCSOLCharacterSlowWall*>(allocated.Get<CBaseEntity>());
    if (!current()) { if (wall && !wall->m_state) UTIL_Remove(wall); return false; }
    if (!wall || wall->m_state) { error.message = "slow-wall entity allocation failed"; return false; }
    const auto state = std::make_shared<SlowWallState>();
    state->players = players; state->session = session; state->access = access;
    state->entity = wall; state->identity = wall; state->round = round; state->expires = expires;
    state->creatingOwner = std::move(ownerCurrent);
    wall->m_state = state; wall->pev->angles = angles;
    UTIL_SetOrigin(wall->pev, origin);
    if (!state->Current()) { state->Remove(); return false; }
    const int dispatched = DispatchSpawn(edict);
    if (!state->Current() || dispatched < 0 || !state->spawned)
    { state->Remove(); error.message = "slow-wall Spawn failed"; return false; }
    state->ready = true;
    state->creatingOwner = {};
    return true;
}

namespace csol::scenario::skills
{
bool StartSlowWall17(const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session,
    const ActivationState& timers, ConfigError& error)
{
    if (!players || !state || !session || !gpGlobals || !g_engfuncs.pfnMakeVectors ||
        !g_engfuncs.pfnTraceLine || !std::isfinite(timers.effects.duration)) return false;
    const auto access = session->Map().EntityAccess();
    const auto round = session->Map().RoundGeneration();
    if (!access || !access->Allowed() || !players->Active()) return false;
    const auto life = state->skillLifeSerial; const auto skill = state->activeSkill;
    auto* actor = state->player.Get<CBasePlayer>();
    const auto current = [&] { return access && access->Allowed() && players->Active() &&
        session->Map().EntityAccess() == access && session->Map().RoundGeneration() == round &&
        players->Find(state->player) == state &&
        state->skillLifeSerial == life && state->activeSkill == skill &&
        state->player.Get<CBasePlayer>() == actor && actor && actor->pev &&
        !actor->has_disconnected && !(actor->pev->flags & (FL_KILLME | FL_DORMANT)); };
    if (!current()) return false;
    Vector origin{};
    if (!Placement(*actor, current, origin) || !current()) return false;
    const Vector angles(0, actor->pev->angles.y, 0);
    return CCSOLCharacterSlowWall::Create(players, session, origin, angles,
        timers.effects.duration, current, error) && current();
}
}
