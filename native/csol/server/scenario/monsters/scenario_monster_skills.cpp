// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_monster_skills.h"
#include "../core/scenario_breakable.h"
#include "scenario_monster_ordinary.h"
#include "../core/scenario_runtime.h"
#include "../core/scenario_session.h"
#include "../../../cso_tent.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>

#pragma clang fp contract(off)

namespace
{
void Clock(CSOLMonsterInterval& clock, float interval)
{
    clock.interval = interval;
    clock.deadline = gpGlobals->time + interval;
    clock.stamp = gpGlobals->time;
}

float Square3(const Vector& delta)
{
    return (delta.y * delta.y + delta.x * delta.x) + delta.z * delta.z;
}

// MP114BBE90: one Newton step with the original seed and float stores.
float InverseLength(float square)
{
    const auto bits = std::bit_cast<std::int32_t>(square);
    const float seed = std::bit_cast<float>(std::uint32_t{0x5f3759d5} -
        static_cast<std::uint32_t>(bits >> 1));
    float product = square * 0.5f;
    product *= seed;
    product *= seed;
    return (1.5f - product) * seed;
}

// MP 11383350(sequence, 1) with the same guarded frame reset as skill 8.
bool SetSkillSequence(CSOLMonsterSpawnCall& call, CSOLMonsterSkillGuard guard, int sequence)
{
    struct Reset
    {
        CSOLMonsterSpawnCall& call;
        CSOLMonsterSkillGuard guard;
        static bool Valid(const void* context)
        {
            const auto& self = *static_cast<const Reset*>(context);
            return self.guard.Current() && self.call.Current();
        }
        static bool Run(void* context)
        {
            auto& self = *static_cast<Reset*>(context);
            return CSOL_ResetMonsterSequenceInfo(self.call, Valid, context);
        }
    } reset{call, guard};
    auto* monster = guard.Current() ? call.Current() : nullptr;
    return monster && monster->Animation().SetSequenceGuarded(*monster, sequence, true, Reset::Run, &reset) &&
        guard.Current() && call.Current();
}

// MP 11399B00: behavior v78 Stop, destination.x=-99999, then +2AC..+2BC = 0.
bool ResetSkillMotion(CSOLMonsterSpawnCall& call, CSOLOrdinaryMonsterState& state, CSOLMonsterSkillGuard guard)
{
    auto* monster = guard.Current() ? call.Current() : nullptr;
    auto* behavior = monster ? dynamic_cast<CCSOLOrdinaryMonsterBehavior*>(monster->Behavior()) : nullptr;
    if (!behavior || !behavior->Stop(call) || !guard.Current() || !call.Current()) return false;
    state.position.destination.x = -99999.0f;
    state.position.motion.velocity = Vector2D(0, 0);
    state.deltaX = state.deltaY = state.motionRemainder = 0;
    return true;
}

enum class SkillLine { Clear, Rejected, Cancelled };

// Skills 9/12 repeat skill 8's 11533770 line and vBC/vA8/+6C hit filter:
// no hit, or an alive player whose team differs from the monster.
SkillLine EnemyLine(CSOLMonsterSpawnCall& call, CSOLMonsterSkillGuard guard,
    const Vector& start, const Vector& end)
{
    const auto current = [&] { return guard.Current() ? call.Current() : nullptr; };
    TraceResult trace{};
    UTIL_TraceLine(start, end, dont_ignore_monsters, current()->edict(), &trace);
    if (!current()) return SkillLine::Cancelled;
    if (!trace.pHit) return SkillLine::Clear;
    const EHANDLE hitHandle(trace.pHit);
    auto* hit = hitHandle.Get<CBaseEntity>();
    if (!hit) return SkillLine::Rejected;
    const bool isPlayer = hit->IsPlayer() != FALSE;
    if (!current()) return SkillLine::Cancelled;
    hit = hitHandle.Get<CBaseEntity>();
    if (!hit || !isPlayer) return SkillLine::Rejected;
    const bool alive = hit->IsAlive() != FALSE;
    if (!current()) return SkillLine::Cancelled;
    auto* player = dynamic_cast<CBasePlayer*>(hitHandle.Get<CBaseEntity>());
    return player && alive && static_cast<int>(player->m_iTeam) != current()->pev->team ?
        SkillLine::Clear : SkillLine::Rejected;
}

// Original Classify 23 is CBreakable/CPushable (vtable 119E09C0/119E0EB8,
// shared slot 10847550). The scenario wall overrides it with 16 or 0.
bool OriginalBreakableClass(CBaseEntity& entity)
{
    return dynamic_cast<CBreakable*>(&entity) && !dynamic_cast<CCSOLScenarioBreakable*>(&entity);
}

// MP1148E500/1148E540 draw CRT rand (IAT 117404D4), not engine RANDOM_*.
float RandomFloat(float low, float high)
{
    const float range = high - low;
    float value = static_cast<float>(std::rand()) * (1.0f / 32767.0f);
    value *= range;
    return value + low;
}
int RandomLong(int low, int high)
{
    return std::rand() % (high - low + 1) + low;
}

// Shared by 1155CA50/1155CBD0/1155CDE0: FL_DUCKING drops 18, otherwise 36.
float BoomerFloor(const entvars_t& pev)
{
    return (pev.origin.z - ((pev.flags & FL_DUCKING) ? 18.0f : 36.0f)) + 1.0f;
}

// MP1155CFC0 before 1155D030. The engine-private TE fields are copied from
// the original WRITE_* sequences; the client consumers mirror HW 37850920.
bool BoomerEffects(CSOLMonsterSpawnCall& call, const CCSOLScenarioMessages& messages,
    CSOLMonsterSkillGuard guard, csol::scenario::ConfigError& error)
{
    const auto current = [&] { return guard.Current() ? call.Current() : nullptr; };
    const int boomer = MODEL_INDEX(CSOLBoomerModel), poison = MODEL_INDEX(CSOLBoomerPoisonModel);
    const int sprite = MODEL_INDEX(CSOLBoomerSprite), blast = MODEL_INDEX(CSOLBoomerExplosionSprite);
    if (!current()) return false;
    if (boomer <= 0 || poison <= 0 || sprite <= 0 || blast <= 0 ||
        boomer > 65535 || poison > 65535 || sprite > 65535 || blast > 65535)
    { error.message = "scenario boomer effect resources are not precached"; return false; }
    const auto fail = [&](const char* message) { error.message = message; return false; };

    // 1155CCF0: TE138 at the raw origin, yaw RANDOM(0,180), ef_boomer.mdl,
    // life 10, rate 1, fade 1, amount 200, TransAlpha, no follow, speed 17.
    auto* monster = current();
    csol::cso_tent::Model shell;
    shell.extended = true;
    shell.origin = {monster->pev->origin.x, monster->pev->origin.y, monster->pev->origin.z};
    shell.angles = {0, RandomFloat(0, 180), 0};
    shell.model = static_cast<std::uint16_t>(boomer);
    shell.life = 10;
    shell.framerate = 1;
    shell.fade = 1;
    shell.renderamt = 200;
    shell.rendermode = kRenderTransAlpha;
    shell.fadeSpeed = 17;
    if (!messages.SendCsoTent(shell, monster->pev->origin)) return fail("scenario boomer TE138 message failed");
    if (!(monster = current())) return false;

    // 1155CBD0(pev, 50, 10, 3): three TE137 spr_boomer.spr, xy jitter
    // RANDOM_LONG(-50,50) around an origin read once, scale 10, 200, rate 20.
    {
        const float x = monster->pev->origin.x, y = monster->pev->origin.y;
        const float z = BoomerFloor(*monster->pev) + 60.0f;
        for (int count = 3; count > 0; --count)
        {
            csol::cso_tent::Sprite puff;
            puff.origin[0] = static_cast<float>(RandomLong(-50, 50)) + x;
            puff.origin[1] = static_cast<float>(RandomLong(-50, 50)) + y;
            puff.origin[2] = z;
            puff.model = static_cast<std::uint16_t>(sprite);
            puff.scale = 10;
            puff.brightness = 200;
            puff.framerate = 20;
            if (!messages.SendCsoTent(puff)) return fail("scenario boomer TE137 message failed");
            if (!current()) return false;
        }
    }

    // 1155CDE0: one TE137 ef_boomer_ex.spr, scale 20, 200, rate 8.
    monster = current();
    csol::cso_tent::Sprite blastSprite;
    blastSprite.origin = {monster->pev->origin.x, monster->pev->origin.y, BoomerFloor(*monster->pev) + 60.0f};
    blastSprite.model = static_cast<std::uint16_t>(blast);
    blastSprite.scale = 20;
    blastSprite.brightness = 200;
    blastSprite.framerate = 8;
    if (!messages.SendCsoTent(blastSprite)) return fail("scenario boomer TE137 message failed");
    if (!(monster = current())) return false;

    // 1155CA50(pev, 0, 1) only on ground: one TE133 ef_poison03.mdl with a
    // zero-width xy jitter (rand is still drawn), yaw RANDOM(0,180), life 10,
    // rate 1, fade 1, amount 100, TransAdd, no follow.
    if (!(monster->pev->flags & FL_ONGROUND)) return true;
    csol::cso_tent::Model pool;
    pool.origin[0] = RandomFloat(-0.0f, 0.0f) + monster->pev->origin.x;
    pool.origin[1] = RandomFloat(-0.0f, 0.0f) + monster->pev->origin.y;
    pool.origin[2] = BoomerFloor(*monster->pev);
    pool.angles[1] = RandomFloat(0, 180);
    pool.model = static_cast<std::uint16_t>(poison);
    pool.life = 10;
    pool.framerate = 1;
    pool.fade = 1;
    pool.renderamt = 100;
    pool.rendermode = kRenderTransAdd;
    if (!messages.SendCsoTent(pool, monster->pev->origin)) return fail("scenario boomer TE133 message failed");
    return current() != nullptr;
}
}

bool CSOL_UseMonsterDisarm(CSOLMonsterSpawnCall& call, CSOLOrdinaryMonsterState& state,
    const CSOLMonsterSpawnState& spawn, const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    CSOLMonsterSkillGuard guard, csol::scenario::ConfigError& error)
{
    const auto current = [&] { return guard.Current() ? call.Current() : nullptr; };
    if (!gpGlobals || !current()) return false;
    // 113F0F20/113F1021: helper early returns always reach these clocks.
    const auto finish = [&]
    {
        if (!current()) return false;
        Clock(state.skillActive, 1.0f);
        Clock(state.skill, 10.0f);
        return true;
    };
    const EHANDLE targetHandle = state.target;
    auto* target = targetHandle.Get<CBaseEntity>();
    if (!target) return finish();
    const bool playerTarget = target->IsPlayer() != FALSE;
    if (!current()) return false;
    target = targetHandle.Get<CBaseEntity>();
    if (!target || !playerTarget) return finish();
    auto* monster = current();
    const auto* appearance = csol::scenario::FindMonsterAppearance(monster->Type()->appearance);
    if (!appearance || !g_engfuncs.pfnTraceLine)
    { error.message = "scenario disarm requires its appearance and trace service"; return false; }
    const Vector origin = monster->pev->origin + Vector(0, 0, appearance->eyesZ);
    const Vector goal = target->pev->origin;
    const Vector delta = goal - origin;
    const float square = (delta.y * delta.y + delta.x * delta.x) + delta.z * delta.z;
    const float length = static_cast<float>(std::sqrt(static_cast<double>(square)));
    if (!std::isfinite(length))
    { error.message = "scenario disarm direction is not finite"; return false; }
    Vector direction(0, 0, 1);
    if (length != 0)
    {
        const float inverse = 1.0f / length;
        direction = Vector(delta.x * inverse, delta.y * inverse, delta.z * inverse);
    }
    TraceResult trace{};
    UTIL_TraceLine(origin, goal, dont_ignore_monsters, monster->edict(), &trace);
    if (!current()) return false;
    if (trace.pHit)
    {
        const EHANDLE hitHandle(trace.pHit);
        auto* hit = hitHandle.Get<CBaseEntity>();
        if (!hit) return finish();
        const bool isPlayer = hit->IsPlayer() != FALSE;
        if (!current()) return false;
        hit = hitHandle.Get<CBaseEntity>();
        if (!hit || !isPlayer) return finish();
        const bool alive = hit->IsAlive() != FALSE;
        if (!current()) return false;
        auto* player = dynamic_cast<CBasePlayer*>(hitHandle.Get<CBaseEntity>());
        if (!player || !alive || static_cast<int>(player->m_iTeam) == current()->pev->team) return finish();
    }

    struct Reset
    {
        CSOLMonsterSpawnCall& call;
        CSOLMonsterSkillGuard guard;
        static bool Valid(const void* context)
        {
            const auto& self = *static_cast<const Reset*>(context);
            return self.guard.Current() && self.call.Current();
        }
        static bool Run(void* context)
        {
            auto& self = *static_cast<Reset*>(context);
            return CSOL_ResetMonsterSequenceInfo(self.call, Valid, context);
        }
    } reset{call, guard};
    monster = current();
    if (!monster->Animation().SetSequenceGuarded(*monster,
        spawn.sequences[csol::scenario::MonsterSequence::AlternateShootGrenadeIdle],
        true, Reset::Run, &reset) || !current()) return false;
    Clock(state.melee.attack, 1.0f);
    Clock(state.melee.avoidance, 1.0f);
    current()->Animation().LockUntil(gpGlobals->time + 1.0f);
    Clock(state.melee.idleObstacleCheck, 1.0f);
    state.position.faceGoal = goal;
    state.position.faceRequested = true;
    bool created{};
    if (!CSOL_CreateMonsterDisarm(call, runtime, origin, direction, created, error) || !current())
        return false;
    if (!created) return finish();
    auto* behavior = dynamic_cast<CCSOLOrdinaryMonsterBehavior*>(current()->Behavior());
    if (!behavior || !behavior->Stop(call) || !current()) return false;
    state.position.destination.x = -99999.0f;
    state.position.motion.velocity = Vector2D(0, 0);
    state.deltaX = state.deltaY = state.motionRemainder = 0;
    Clock(state.findPath, 1.0f);
    return finish();
}

bool CSOL_UseMonsterDash(CSOLMonsterSpawnCall& call, CSOLOrdinaryMonsterState& state,
    const CSOLMonsterSpawnState& spawn, bool suppressed, CSOLMonsterSkillGuard guard)
{
    const auto current = [&] { return guard.Current() ? call.Current() : nullptr; };
    if (!gpGlobals || !current()) return false;
    // 113F0F66/113F1021 mode15: 113EC3E0 returns always reach 1.5 active and
    // a 3+1 retry. Mode 59's 6+8 and mode 28's Classify16 gate do not apply.
    const auto finish = [&]
    {
        if (!current()) return false;
        Clock(state.skillActive, 1.5f);
        Clock(state.skill, 4.0f);
        return true;
    };
    if (suppressed) return finish();
    const EHANDLE targetHandle = state.target;
    auto* target = targetHandle.Get<CBaseEntity>();
    if (!target) return finish();
    const bool playerTarget = target->IsPlayer() != FALSE;
    if (!current()) return false;
    target = targetHandle.Get<CBaseEntity>();
    if (!target || !playerTarget) return finish();
    const Vector start = current()->pev->origin;
    const Vector goal = target->pev->origin;
    if (Square3(goal - start) > 1394997.125f) return finish();
    switch (EnemyLine(call, guard, start, goal))
    {
    case SkillLine::Cancelled: return false;
    case SkillLine::Rejected: return finish();
    case SkillLine::Clear: break;
    }
    Clock(state.melee.attack, 1.26f);
    Clock(state.melee.avoidance, 1.26f);
    current()->Animation().LockUntil(gpGlobals->time + 1.26f);
    Clock(state.melee.idleObstacleCheck, 1.26f);
    Clock(state.findPath, 1.26f);
    state.position.faceGoal = goal;
    state.position.faceRequested = true;
    if (!ResetSkillMotion(call, state, guard) ||
        !SetSkillSequence(call, guard, spawn.sequences[csol::scenario::MonsterSequence::Dash]))
        return false;
    // 113F03D0 launches the 1200-unit dash once this 0.76 s wind-up expires.
    Clock(state.secondarySkill, 0.76f);
    return finish();
}

bool CSOL_UseMonsterPierce(CSOLMonsterSpawnCall& call, CSOLOrdinaryMonsterState& state,
    const CSOLMonsterSpawnState& spawn, bool suppressed, CSOLMonsterSkillGuard guard)
{
    using Sequence = csol::scenario::MonsterSequence;
    const auto current = [&] { return guard.Current() ? call.Current() : nullptr; };
    if (!gpGlobals || !current()) return false;
    // 113F0FBC/113F1021: 1.5 active and a 9+1 retry after every return.
    const auto finish = [&]
    {
        if (!current()) return false;
        Clock(state.skillActive, 1.5f);
        Clock(state.skill, 10.0f);
        return true;
    };
    if (suppressed) return finish();
    const EHANDLE targetHandle = state.target;
    auto* target = targetHandle.Get<CBaseEntity>();
    if (!target) return finish();
    const bool playerTarget = target->IsPlayer() != FALSE;
    if (!current()) return false;
    target = targetHandle.Get<CBaseEntity>();
    if (!target || !playerTarget) return finish();
    auto* monster = current();
    const float baseDamage = spawn.attributes.attackDamage + spawn.attributes.attackDamage;
    constexpr float Range = 157.48f;
    // Owner vF0 11359A70: v84 feet (11399CB0) strictly within 157.48.
    Vector feet = monster->pev->origin;
    if (const auto* appearance = csol::scenario::FindMonsterAppearance(monster->Type()->appearance))
        feet.z += appearance->feetZ;
    if (!(Range * Range > Square3(feet - target->pev->origin))) return finish();
    state.position.faceGoal = target->pev->origin;
    state.position.faceRequested = true;
    const auto& velocity = monster->pev->velocity;
    const float speed = velocity.x * velocity.x + velocity.y * velocity.y;
    const auto slot = speed > 25600.0f ? Sequence::SkillRun :
        speed > 225.0f ? Sequence::SkillWalk : Sequence::SkillIdle;
    if (!SetSkillSequence(call, guard, spawn.sequences[slot])) return false;
    Clock(state.melee.attack, 1.5f);
    Clock(state.melee.avoidance, 1.5f);
    current()->Animation().LockUntil(gpGlobals->time + 1.5f);
    Clock(state.melee.idleObstacleCheck, 1.5f);

    monster = current();
    const Vector origin = monster->pev->origin;
    // The original passes pev+74 (v_angle), not angles. Ordinary AI writes
    // only angles (PointAt 113532E4), so this keeps the original direction.
    UTIL_MakeVectors(monster->pev->v_angle);
    if (!current()) return false;
    const Vector forward = gpGlobals->v_forward;
    const Vector end(forward.x * Range + origin.x, forward.y * Range + origin.y,
        forward.z * Range + origin.z);
    Vector start = origin;
    int damage = static_cast<int>(baseDamage);
    float segment = Range;
    // gMultiDamage.type is preset to DMG_BULLET|DMG_NEVERGIB; each pass then
    // clears and accumulates one TraceAttack. At most five entities.
    for (int remaining = 5;;)
    {
        ClearMultiDamage();
        TraceResult trace{};
        UTIL_TraceLine(start, end, dont_ignore_monsters, current()->edict(), &trace);
        if (!current()) return false;
        if (trace.flFraction >= 1.0f)
        {
            // A missed line retries a human hull from the first origin.
            UTIL_TraceHull(origin, end, dont_ignore_monsters, human_hull, current()->edict(), &trace);
            if (!current()) return false;
            if (trace.flFraction >= 1.0f) return finish();
        }
        edict_t* hitEdict = trace.pHit ? trace.pHit : INDEXENT(0);
        if (!hitEdict) return finish();
        const EHANDLE hitHandle(hitEdict);
        auto* hit = hitHandle.Get<CBaseEntity>();
        if (!hit) return finish();
        const bool bsp = hit->IsBSPModel() != FALSE;
        if (!current()) return false;
        hit = hitHandle.Get<CBaseEntity>();
        if (!hit) return finish();
        const bool breakable = OriginalBreakableClass(*hit);
        if ((bsp && !breakable) || trace.flFraction == 1.0f) return finish();
        const float distance = trace.flFraction * segment;
        const float falloff = static_cast<float>(std::pow(static_cast<double>(0.95f),
            static_cast<double>(distance / 500.0f)));
        damage = static_cast<int>(falloff * static_cast<float>(damage));
        start = Vector(trace.vecEndPos.x + forward.x * 32.0f, trace.vecEndPos.y + forward.y * 32.0f,
            trace.vecEndPos.z + forward.z * 32.0f);
        remaining = distance > Range ? 0 : remaining - 1;
        segment = (segment - distance) - 32.0f;
        const bool player = hit->IsPlayer() != FALSE;
        if (!current()) return false;
        hit = hitHandle.Get<CBaseEntity>();
        if (player && hit)
        {
            hit->TraceAttack(current()->pev, static_cast<float>(damage), forward, &trace,
                DMG_BULLET | DMG_NEVERGIB);
            if (!current()) return false;
            ApplyMultiDamage(current()->pev, current()->pev);
            if (!current()) return false;
        }
        damage = static_cast<int>(static_cast<double>(damage) * 0.75);
        // An intact Classify23 breakable stops the pierce after this pass.
        hit = hitHandle.Get<CBaseEntity>();
        if (breakable && hit && hit->pev->health > 0.0f) return finish();
        if (!remaining) return finish();
    }
}

bool CSOL_UseMonsterLeap(CSOLMonsterSpawnCall& call, CSOLOrdinaryMonsterState& state,
    const CSOLMonsterSpawnState& spawn, bool suppressed, CSOLMonsterSkillGuard guard,
    csol::scenario::ConfigError& error)
{
    const auto current = [&] { return guard.Current() ? call.Current() : nullptr; };
    if (!gpGlobals || !current()) return false;
    // 113F0FE8/113F1021: 2.3 active and a 4+1 retry after every return.
    const auto finish = [&]
    {
        if (!current()) return false;
        Clock(state.skillActive, 2.3f);
        Clock(state.skill, 5.0f);
        return true;
    };
    if (suppressed) return finish();
    const EHANDLE targetHandle = state.target;
    auto* target = targetHandle.Get<CBaseEntity>();
    if (!target) return finish();
    const bool playerTarget = target->IsPlayer() != FALSE;
    if (!current()) return false;
    target = targetHandle.Get<CBaseEntity>();
    if (!target || !playerTarget) return finish();
    const float collisionDamage = spawn.attributes.attackDamage + spawn.attributes.attackDamage;
    const Vector start = current()->pev->origin;
    const Vector goal = target->pev->origin;
    const float square = Square3(goal - start);
    // Original COMISS/JA pair: leap only between 196.85 and 393.70 units.
    if (square > 154999.6875f || 38749.921875f > square) return finish();
    switch (EnemyLine(call, guard, start, goal))
    {
    case SkillLine::Cancelled: return false;
    case SkillLine::Rejected: return finish();
    case SkillLine::Clear: break;
    }
    Clock(state.melee.attack, 2.3f);
    Clock(state.melee.avoidance, 2.3f);
    current()->Animation().LockUntil(gpGlobals->time + 2.3f);
    Clock(state.melee.idleObstacleCheck, 2.3f);
    state.position.faceGoal = goal;
    state.position.faceRequested = true;
    if (!ResetSkillMotion(call, state, guard) ||
        !SetSkillSequence(call, guard, spawn.sequences[csol::scenario::MonsterSequence::Skill1]))
        return false;
    // 1061F370 Vector::Normalize: double sqrt narrowed to float; zero is +Z.
    const Vector delta = goal - start;
    const float length = static_cast<float>(std::sqrt(static_cast<double>(
        (delta.x * delta.x + delta.y * delta.y) + delta.z * delta.z)));
    if (!std::isfinite(length))
    { error.message = "scenario leap direction is not finite"; return false; }
    Vector direction(0, 0, 1);
    if (length != 0.0f)
    {
        const float inverse = 1.0f / length;
        direction = Vector(delta.x * inverse, delta.y * inverse, delta.z * inverse);
    }
    current()->pev->velocity = Vector(direction.x * 800.0f, direction.y * 800.0f, direction.z * 800.0f);
    // +1D80..+1D88 holds the position update, +1E10..+1E18 is the contact
    // window Touch reads and +1E1C its damage.
    state.position.updateInterval = 2.3f;
    state.position.updateAfter = gpGlobals->time + 2.3f;
    state.updateStamp = gpGlobals->time;
    state.secondaryUpdateInterval = 2.3f;
    state.secondaryUpdateAfter = gpGlobals->time + 2.3f;
    state.secondaryUpdateStamp = gpGlobals->time;
    state.skillCollisionDamage = collisionDamage;
    return finish();
}

bool CSOL_ApplyMonsterFreeExplosion(CSOLMonsterSpawnCall& call, float damage, float radius,
    float force, const CCSOLScenarioMessages& messages, CSOLMonsterSkillGuard guard,
    csol::scenario::ConfigError& error)
{
    const auto current = [&] { return guard.Current() ? call.Current() : nullptr; };
    if (!gpGlobals || !current()) return false;
    if (!std::isfinite(damage) || !std::isfinite(radius) || !std::isfinite(force))
    { error.message = "scenario Free explosion input is not finite"; return false; }
    if (!BoomerEffects(call, messages, guard, error)) return false;
    const Vector origin = current()->pev->origin;
    const float radiusSquare = radius * radius;
    // MP1155D030 reads maxClients again after every player.
    for (int index = 1; index <= gpGlobals->maxClients; ++index)
    {
        auto* player = dynamic_cast<CBasePlayer*>(UTIL_PlayerByIndex(index));
        if (!player || !player->edict() || !OFFSET(player->edict()) || player->pev->deadflag != DEAD_NO) continue;
        // |dz| is truncated to int before the float radius*0.25 comparison;
        // the planar test is two-dimensional (y*y + x*x).
        const int height = static_cast<int>(std::fabs(player->pev->origin.z - origin.z));
        if (!(radius * 0.25f > static_cast<float>(height))) continue;
        const float dy = player->pev->origin.y - origin.y;
        const float dx = player->pev->origin.x - origin.x;
        const float square = dy * dy + dx * dx;
        if (!(radiusSquare > square)) continue;
        const EHANDLE handle(player->edict());
        if (!messages.SendMonsterSkillShake(player->edict()))
        { error.message = "scenario Free explosion screen shake failed"; return false; }
        if (!current()) return false;
        player = dynamic_cast<CBasePlayer*>(handle.Get<CBaseEntity>());
        if (!player) continue;
        player->TakeDamage(current()->pev, current()->pev, damage, DMG_EXPLOSION);
        if (!current()) return false;
        player = dynamic_cast<CBasePlayer*>(handle.Get<CBaseEntity>());
        if (!player) continue;
        // MP114BBE90 approximation; the push is written directly to velocity.
        const float inverse = InverseLength(square);
        const float scale = (1.0f - (1.0f / inverse) * (1.0f / radius)) * force;
        const float planar = scale * inverse * 150.0f;
        player->pev->velocity.x = planar * dx + player->pev->velocity.x;
        player->pev->velocity.y = planar * dy + player->pev->velocity.y;
        player->pev->velocity.z = scale * 150.0f + player->pev->velocity.z;
    }
    // Mode 28 also enters this tail; the scenario mode uses host+12B4 only.
    if (current()->HostState().activeSkill != 13) return true;
    if (!g_engfuncs.pfnFindEntityInSphere)
    { error.message = "scenario Free explosion sphere search is unavailable"; return false; }
    // Preserve engine sphere order with an edict cursor, never a retained
    // CBaseEntity pointer across TakeDamage callbacks.
    edict_t* cursor{};
    for (int remaining = gpGlobals->maxEntities;;)
    {
        auto* entry = g_engfuncs.pfnFindEntityInSphere(cursor, origin, radius);
        if (!current()) return false;
        if (FNullEnt(entry)) break;
        if (--remaining < 0 || entry == cursor)
        { error.message = "scenario Free explosion sphere traversal did not advance"; return false; }
        cursor = entry;
        const EHANDLE handle(entry);
        auto* target = handle.Get<CBaseEntity>();
        if (!target) continue;
        const int classification = target->Classify();
        if (!current()) return false;
        target = handle.Get<CBaseEntity>();
        if (!target || classification != 16 || target->pev->solid == SOLID_NOT ||
            target->pev->takedamage == DAMAGE_NO) continue;
        target->TakeDamage(current()->pev, current()->pev, damage, DMG_EXPLOSION);
        if (!current()) return false;
    }
    return true;
}
