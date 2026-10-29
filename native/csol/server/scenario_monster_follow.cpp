// SPDX-License-Identifier: GPL-3.0-or-later
// Path follower structure follows ReGameDLL_CS game_shared/bot/nav_path.cpp
// (GPL-2.0-or-later, Half-Life linking exception). CSOL traversal, numerical
// policy and monster control are based on the preserved MP research.
#include "precompiled.h"
#include "scenario_monster_follow.h"
#include "scenario_monster_heading.h"
#include "scenario_performance.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace
{
bool Finite(const Vector& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
float Square2(const Vector& v) { return v.x * v.x + v.y * v.y; }
float Square3(const Vector& v) { return Square2(v) + v.z * v.z; }
float Length2(const Vector& v) { return static_cast<float>(std::sqrt(static_cast<double>(Square2(v)))); }
float Normalize(Vector& v, const Vector& zero)
{
    const float length = static_cast<float>(std::sqrt(static_cast<double>(Square3(v))));
    if (length > 0) v /= length;
    else v = zero;
    return length;
}
CCSOLScenarioMonster* Active(CSOLMonsterSpawnCall& call)
{
    auto* monster = call.Current();
    return monster && monster->pev->deadflag != DEAD_DEAD &&
        monster->Pool()->Borrowed(*monster) ? monster : nullptr;
}
bool StopAhead(const CNavPath::PathSegment& segment)
{
    return segment.ladder || (segment.area->GetAttributes() & NAV_JUMP);
}
}

struct CCSOLMonsterPathFollower::Query
{
    CCSOLMonsterPathFollower& follower;
    CSOLMonsterSpawnCall& call;
    const CCSOLScenarioNavWorld& world;
    CSOLMonsterFollowInput input;
    std::uint64_t revision, generation;
    Vector feet, centroid, eyes;
    float feetZ{}, centroidZ{}, eyesZ{};
    bool failed{}, cancelled{};

    bool Current()
    {
        // Check world/revision before resolving entity handles or NAV pointers.
        if (world.Generation() != generation || follower.m_revision != revision || !follower.SameOwner(call))
            cancelled = true;
        return !cancelled;
    }

    bool Positions()
    {
        if (!Current()) return false;
        const Vector origin = Active(call)->pev->origin;
        feet = origin + Vector(0, 0, feetZ);
        centroid = origin + Vector(0, 0, centroidZ);
        eyes = origin + Vector(0, 0, eyesZ);
        if (!Finite(feet) || !Finite(centroid) || !Finite(eyes)) failed = true;
        return !failed;
    }

    bool Clear(const Vector& from, const Vector& to, unsigned flags)
    {
        if (!Current()) return false;
        if (!Finite(from) || !Finite(to) || !g_engfuncs.pfnTraceLine)
        { failed = true; return false; }
        Vector direction = to - from;
        const float total = Square3(direction);
        if (!std::isfinite(total)) { failed = true; return false; }
        Normalize(direction, Vector(0, 0, 1));
        Vector start = from;
        edict_t* ignored{};
        edict_t* previous{};
        // The original uses distance-progress termination. Retain that and
        // the SDK's bounded trace count/two-hit cycle rejection for bad maps.
        for (int attempt = 0; attempt < 50; ++attempt)
        {
            TraceResult trace{};
            csol::scenario::perf::Add(csol::scenario::perf::Counter::FollowTraces);
            UTIL_TraceLine(start, to, ignore_monsters, dont_ignore_glass, ignored, &trace);
            if (!Current()) return false;
            if (!std::isfinite(trace.flFraction) || !Finite(trace.vecEndPos))
            { failed = true; return false; }
            if (trace.flFraction == 1.0f) return true;
            if (!trace.pHit) return false;
            if (!gpGlobals || !gpGlobals->pStringBase) { failed = true; return false; }
            auto* vars = &trace.pHit->v;
            const bool door = FClassnameIs(vars, "func_door") || FClassnameIs(vars, "func_door_rotating");
            const bool breakable = FClassnameIs(vars, "func_breakable") && vars->takedamage == DAMAGE_YES;
            if (!((door && (flags & 1)) || (breakable && (flags & 2)))) return false;
            if (trace.pHit == previous) return false;
            previous = ignored;
            ignored = trace.pHit;
            start = trace.vecEndPos + 5.0f * direction;
            if (Square3(start - from) > total) return false;
        }
        return false;
    }
};

void CCSOLMonsterPathFollower::Reset()
{
    ++m_revision;
    m_route.Invalidate();
    m_owner.reset();
    m_segment = 1;
    m_ladderStarted = false;
    ResetStuck();
}

bool CCSOLMonsterPathFollower::SameOwner(CSOLMonsterSpawnCall& call) const
{
    if (!m_owner) return false;
    auto* original = m_owner->Current();
    return original && original == Active(call);
}

std::optional<NavDirType> CCSOLMonsterPathFollower::LadderDirection(CSOLMonsterSpawnCall& call,
    const CCSOLScenarioNavWorld& world) const
{
    if (m_route.generation != world.Generation() || !SameOwner(call) ||
        m_segment < 0 || m_segment >= m_route.count) return std::nullopt;
    const auto* ladder = m_route.segments[m_segment].ladder;
    if (!ladder || ladder->m_dir < NORTH || ladder->m_dir > WEST) return std::nullopt;
    return ladder->m_dir;
}

void CCSOLMonsterPathFollower::ResetStuck()
{
    m_stuck = false;
    m_speedIndex = 0;
    m_speedCount = 0;
}

bool CCSOLMonsterPathFollower::UpdateStuck(const Vector& centroid, bool usingLadder, float now)
{
    if (m_stuck)
    {
        if (Square3(centroid - m_stuckSpot) > 5625.0f) ResetStuck();
    }
    else
    {
        Vector delta = centroid - m_lastCentroid;
        if (!usingLadder) delta.z = 0;
        const float elapsed = now - m_lastTime;
        if (elapsed <= 0) return true;
        const float speed = static_cast<float>(std::sqrt(static_cast<double>(Square3(delta)))) / elapsed;
        if (!std::isfinite(speed)) return false;
        m_lastTime = now;
        m_speeds[m_speedIndex++] = speed;
        if (m_speedIndex == 5) m_speedIndex = 0;
        // The sixth accepted sample first evaluates the rolling five.
        if (m_speedCount < 5) ++m_speedCount;
        else
        {
            float average{};
            for (float sample : m_speeds) average += sample;
            average /= m_speedCount;
            if (average < (usingLadder ? 10.0f : 20.0f))
            { m_stuckStart = now; m_stuckSpot = centroid; m_stuck = true; }
        }
    }
    m_lastCentroid = centroid;
    return true;
}

bool CCSOLMonsterPathFollower::Build(CSOLMonsterSpawnCall& call,
    const CCSOLScenarioNavWorld& world, const Vector& feet, const Vector& goal,
    bool climber, std::span<const CNavLadder* const> climberLadders)
{
    Reset();
    const auto revision = m_revision, generation = world.Generation();
    if (!Active(call)) return false;
    CSOLScenarioRoute candidate;
    const bool built = CSOL_BuildScenarioRoute(candidate, world, feet, goal, climber, climberLadders);
    // Nested callbacks can replace this route without replacing the entity.
    if (revision != m_revision || generation != world.Generation() || !Active(call))
    {
        csol::scenario::perf::Add(csol::scenario::perf::Counter::RouteCancelled);
        return false;
    }
    if (!built)
    {
        csol::scenario::perf::Add(csol::scenario::perf::Counter::RouteFailed);
        return false;
    }
    m_route = candidate;
    m_owner.emplace(call);
    return true;
}

int CCSOLMonsterPathFollower::FindClosest(Query& q, Vector& close) const
{
    float closest = 1.0e10f;
    int index = -1;
    const int end = std::min(m_route.count, m_segment + 3);
    for (int i = std::max(1, m_segment - 3); i < end; ++i)
    {
        const Vector from = m_route.segments[i - 1].pos;
        const Vector to = m_route.segments[i].pos;
        Vector along = to - from;
        const float length = Normalize(along, Vector(0, 0, 1));
        const float projected = DotProduct(q.feet - from, along);
        const Vector position = projected <= 0 ? from : projected >= length ? to : from + projected * along;
        const float distance = Square3(position - q.feet);
        if (!(distance < closest)) continue;
        const bool clear = q.Clear(q.eyes, position + Vector(0, 0, 36), 3);
        if (q.cancelled || q.failed) return -1;
        if (!clear) continue;
        close = position;
        closest = distance;
        index = i - 1;
    }
    return index;
}

int CCSOLMonsterPathFollower::FindPoint(Query& q, Vector& point, int& behind) const
{
    Vector close;
    int start = FindClosest(q, close);
    behind = start;
    if (start <= 0 || q.cancelled || q.failed) return m_segment;
    if (q.input.crouching)
    {
        int i = std::min(start + 1, m_route.count - 1);
        point = m_route.segments[i].pos;
        while (Square2(point - close) < 400.0f)
        {
            if (++i >= m_route.count) { i = m_route.count - 1; break; }
            point = m_route.segments[i].pos;
        }
        return i;
    }
    while (start < m_route.count - 1 && Square2(m_route.segments[start + 1].pos - close) < 400.0f)
        ++start;
    if (start > m_segment && StopAhead(m_route.segments[start]))
    { point = m_route.segments[start].pos; return start; }
    start = std::min(start + 1, m_route.count - 1);
    if (StopAhead(m_route.segments[start]))
    { point = m_route.segments[start].pos; return start; }
    Vector initial = m_route.segments[start].pos - m_route.segments[start - 1].pos;
    Normalize(initial, Vector(0, 0, 1));
    if (!q.Positions()) return m_segment;
    Vector previous = initial;
    float range{};
    bool visible = true, corner = false;
    int i = start;
    for (; i < m_route.count; ++i)
    {
        const Vector position = m_route.segments[i].pos;
        Vector direction = position - m_route.segments[i - 1].pos;
        const float length = static_cast<float>(std::sqrt(static_cast<double>(Square3(direction))));
        if (length > 0) direction *= 1.0f / length;
        else direction = Vector(0, 0, 1);
        if (DotProduct(direction, initial) < 0) { --i; break; }
        if (DotProduct(direction, previous) < 0.5f) { corner = true; --i; break; }
        previous = direction;
        visible = q.Clear(q.eyes, position + Vector(0, 0, 36), 2);
        if (q.cancelled || q.failed) return m_segment;
        if (!visible || StopAhead(m_route.segments[i])) break;
        range += Length2(position - (i == start ? q.feet : m_route.segments[i - 1].pos));
        if (range >= 300.0f) break;
    }
    const int after = std::clamp(i, start, m_route.count - 1);
    const Vector before = m_route.segments[after - 1].pos;
    const Vector along = m_route.segments[after].pos - before;
    const float length = Length2(along);
    // A vertical/coincident segment must not divide by zero and poison
    // server movement. Explicit adaptation; ordinary segments use original math.
    if (!(length > 0)) point = m_route.segments[after].pos;
    else
    {
        float t = std::clamp(1.0f - (range - 300.0f) / length, 0.0f, 1.0f);
        point = before + t * along;
        if (!visible)
        {
            const Vector probe = point + Vector(0, 0, 36);
            const float step = 25.0f / length;
            // CSO and the SDK keep this probe fixed during backtracking.
            // Do not quietly substitute a different line-of-sight policy.
            while (t > 0)
            {
                const bool clear = q.Clear(q.eyes, probe, 2);
                if (q.cancelled || q.failed) return m_segment;
                if (clear) break;
                const float next = t - step;
                if (!(next < t)) { q.failed = true; return m_segment; }
                t = next;
                point = before + t * along;
            }
            if (t <= 0) point = before;
        }
    }
    if (!corner)
    {
        if (!q.Positions()) return m_segment;
        const Vector delta = point - q.centroid;
        if (delta.x * initial.x + delta.y * initial.y < 0 || Square2(delta) < 2500.0f)
        {
            for (i = start; i < m_route.count; ++i)
            {
                if (StopAhead(m_route.segments[i]) || Square2(m_route.segments[i].pos - q.centroid) > 2500.0f)
                { point = m_route.segments[i].pos; start = i; break; }
            }
            if (i == m_route.count)
            { start = m_route.count - 1; point = m_route.segments[start].pos; }
        }
    }
    return start;
}

bool CCSOLMonsterPathFollower::AdjustFeelers(Query& q, Vector& goal) const
{
    if (!q.Positions()) return false;
    if (q.input.lastArea && (q.input.lastArea->GetAttributes() & NAV_PRECISE)) return true;
    float cosine{}, sine{};
    if (!CSOL_MonsterHeading(q.input.moveYaw, cosine, sine)) { q.failed = true; return false; }
    Vector direction(cosine, sine, 0);
    Normalize(direction, Vector(0, 0, 0));
    Vector lateral(-direction.y, direction.x, 0), normal;
    float height{};
    const bool ground = q.world.SimpleGroundHeight(q.eyes, height, &normal);
    if (!q.Positions()) return false;
    if (!ground) return true;
    // v6C also raises height to overlapping NAV floor; feelers consume only
    // the trace normal, so that height correction has no effect here.
    direction = CrossProduct(lateral, normal);
    lateral = CrossProduct(direction, normal);
    const float offset = q.input.crouching ? 20.0f : 25.0f;
    const float length = q.input.crouching ? 20.0f : q.input.running ? 50.0f : 30.0f;
    const Vector feet = q.feet + Vector(0, 0, 18.1f);
    Vector from = feet + offset * lateral;
    const bool left = q.Clear(from, from + length * direction, 3);
    if (q.cancelled || q.failed) return false;
    from = feet - offset * lateral;
    const bool right = q.Clear(from, from + length * direction, 3);
    if (q.cancelled || q.failed) return false;
    const float range = q.input.crouching ? 150.0f : 300.0f;
    if (!right && left) goal += range * lateral;
    else if (right && !left) goal -= range * lateral;
    return true;
}

bool CCSOLMonsterPathFollower::StandUp(Query& q, CSOLMonsterFollowStep& step) const
{
    if (!step.crouch || gpGlobals->time <= q.input.standUntil) return true;
    if (!g_engfuncs.pfnTraceLine || !std::isfinite(q.input.standUntil))
    { q.failed = true; return false; }
    for (int y = -24; y <= 24; y += 12)
        for (int x = -24; x <= 24; x += 12)
        {
            if (!q.Positions()) return false;
            auto* monster = Active(q.call);
            TraceResult trace{};
            csol::scenario::perf::Add(csol::scenario::perf::Counter::FollowTraces);
            UTIL_TraceLine(q.feet + Vector(0, 0, 3), q.feet + Vector(float(x), float(y), 72),
                ignore_monsters, ignore_glass, monster->edict(), &trace);
            if (!q.Current()) return false;
            if (!std::isfinite(trace.flFraction)) { q.failed = true; return false; }
            if (trace.flFraction < 1.0f) return true;
        }
    step.clearAnimationQueue = true;
    step.crouch = false;
    return true;
}

CSOLMonsterFollowStep CCSOLMonsterPathFollower::Update(CSOLMonsterSpawnCall& call,
    const CCSOLScenarioNavWorld& world, const csol::scenario::MonsterAppearance& appearance,
    const CSOLMonsterFollowInput& input)
{
    using Result = CSOLMonsterFollowResult;
    CSOLMonsterFollowStep step;
    step.revision = m_revision;
    step.segment = m_segment;
    step.crouch = input.crouching;
    if (!m_route.count) return step;
    if (world.Generation() != m_route.generation || !SameOwner(call))
    { step.result = Result::Cancelled; Reset(); return step; }
    if (m_segment < 0 || m_segment >= m_route.count)
    { step.result = Result::InvalidPath; Reset(); return step; }
    const Vector origin = Active(call)->pev->origin;
    Query q{*this, call, world, input, m_revision, m_route.generation,
        origin + Vector(0, 0, appearance.feetZ),
        origin + Vector(0, 0, appearance.centroidZ),
        origin + Vector(0, 0, appearance.eyesZ),
        appearance.feetZ, appearance.centroidZ, appearance.eyesZ};
    if (!Finite(q.feet) || !Finite(q.eyes) || !Finite(q.centroid) ||
        !gpGlobals || !std::isfinite(gpGlobals->time))
    { step.result = Result::Failed; return step; }
    const auto node = m_route.segments[m_segment];
    step.clearClimb = true;
    if (node.ladder)
    {
        step.result = Result::Ladder;
        step.startLadder = !m_ladderStarted;
        m_ladderStarted = true;
        step.ladder = node.ladder;
        step.how = node.how;
        step.goal = node.pos;
        if (m_segment > 0) { step.hasApproach = true; step.approach = m_route.segments[m_segment - 1].pos; }
        if (m_segment + 1 < m_route.count)
        { step.hasDepart = true; step.depart = m_route.segments[m_segment + 1].pos; }
        return step;
    }
    m_ladderStarted = false;
    if (Square3(q.feet - node.pos) < 400.0f && ++m_segment >= m_route.count)
    { step.result = Result::Arrived; step.goal = m_route.segments[m_route.count - 1].pos; Reset(); return step; }
    step.goal = node.pos;
    int behind{};
    const int next = FindPoint(q, step.goal, behind);
    if (q.cancelled || q.failed)
    { step.result = q.cancelled ? Result::Cancelled : Result::Failed; return step; }
    m_segment = std::min(next, m_route.count - 1);
    if (!q.Positions())
    { step.result = q.cancelled ? Result::Cancelled : Result::Failed; return step; }
    if (!input.usingLadder)
    {
        if (input.lastArea && (input.lastArea->GetAttributes() & NAV_CROUCH) &&
            !(input.lastArea->GetAttributes() & NAV_JUMP)) step.crouch = false;
        bool crouchAhead = false;
        for (int i = m_segment; i < m_route.count; ++i)
        {
            auto* area = m_route.segments[i].area;
            if (area->GetAttributes() & NAV_JUMP) { step.approachingJump = true; break; }
            Vector close;
            area->GetClosestPointOnArea(&q.centroid, &close);
            if (Square2(close - q.feet) > 2500.0f) break;
            if (area->GetAttributes() & NAV_CROUCH) { crouchAhead = true; break; }
        }
        // Ordinary CBaseZombieImprov's Crouch slot explicitly clears the
        // crouch flag. Do not inherit the hostage implementation/short hull.
        if (crouchAhead) step.crouch = false;
        else if (!input.jumping && !StandUp(q, step))
        { step.result = q.cancelled ? Result::Cancelled : Result::Failed; return step; }
    }
    if (!q.Positions())
    { step.result = q.cancelled ? Result::Cancelled : Result::Failed; return step; }
    if (!UpdateStuck(q.centroid, input.usingLadder, gpGlobals->time))
    { step.result = Result::Failed; return step; }
    const float stuckTime = m_stuck ? (m_stuckStart > 0 ? gpGlobals->time - m_stuckStart : 99999.9f) : 0;
    if (stuckTime > 2.0f)
    { step.result = Result::Stuck; Reset(); return step; }
    if (step.goal.z - q.feet.z > JumpCrouchHeight && Square2(q.feet - step.goal) < 5625.0f)
    {
        const int nextBehind = behind + 1;
        if (behind < 0 || nextBehind >= m_route.count || m_route.segments[nextBehind].pos.z - q.feet.z > JumpCrouchHeight)
        { step.result = Result::FellOff; Reset(); return step; }
    }
    q.input.crouching = step.crouch;
    if (input.avoidObstacles && !step.approachingJump && !input.jumping && m_segment < m_route.count - 1)
    {
        if (!AdjustFeelers(q, step.goal))
        { step.result = q.cancelled ? Result::Cancelled : Result::Failed; return step; }
    }
    step.segment = m_segment;
    step.result = Finite(step.goal) ? Result::Moving : Result::Failed;
    return step;
}

bool CCSOLMonsterPathFollower::CompleteLadder(CSOLMonsterSpawnCall& call, const CSOLMonsterFollowStep& step,
    const CCSOLScenarioNavWorld& world)
{
    if (step.result != CSOLMonsterFollowResult::Ladder || step.revision != m_revision ||
        step.segment != m_segment || m_route.generation != world.Generation() ||
        !SameOwner(call) || m_segment < 0 || m_segment >= m_route.count ||
        m_route.segments[m_segment].ladder != step.ladder)
        return false;
    ++m_segment;
    return true;
}

CSOLMonsterFollowResult CCSOLMonsterPathFollower::Advance(CSOLMonsterSpawnCall& call,
    const CCSOLScenarioNavWorld& world, const csol::scenario::MonsterAppearance& appearance,
    CNavArea* lastArea, CSOLMonsterRouteMotion& motion,
    float deltaTime, std::int16_t skill, bool avoidObstacles)
{
    csol::scenario::perf::Scope performance(csol::scenario::perf::Timer::Follow);
    using Result = CSOLMonsterFollowResult;
    if (!std::isfinite(deltaTime) || deltaTime < 0 || motion.moveType < 0 ||
        motion.moveType > 2 || motion.maxMoveType < 0 || motion.maxMoveType > 2)
        return Result::Failed;
    // Ordinary IsUsingLadder is independent of the climb-animation bit;
    // pass the proven ordinary getter value, not traversingLadder.
    CSOLMonsterFollowInput input{lastArea, motion.moveYaw, motion.crouching,
        motion.jumping, motion.moveType == 2, false, avoidObstacles, motion.standUntil};
    const auto owner = m_owner;
    const auto generation = m_route.generation, revision = m_revision;
    const auto step = Update(call, world, appearance, input);
    if (step.result == Result::Empty || step.result == Result::Cancelled) return step.result;
    const bool terminal = step.result == Result::Arrived || step.result == Result::InvalidPath ||
        step.result == Result::Stuck || step.result == Result::FellOff;
    // A trace callback may reset/reborrow this same behavior. Do not write
    // the previous step's pose into the new owner's movement state.
    if (generation != world.Generation() || !owner || !owner->Current() ||
        owner->Current() != Active(call) || m_revision != revision + (terminal ? 1 : 0))
        return Result::Cancelled;
    if (step.clearClimb) motion.climbSequence = false;
    motion.crouching = step.crouch;
    // The ordinary animation owner consumes this command before its next
    // animation update; navigation never resets the monster's current MDL
    // sequence as a substitute for clearing that pending behavior queue.
    if (step.clearAnimationQueue) motion.clearAnimationQueue = true;
    if (step.result == Result::Arrived) { motion.moveState = 2; return step.result; }
    if (step.result != Result::Moving && step.result != Result::Ladder) return step.result;
    if (step.revision != m_revision || !SameOwner(call) || world.Generation() != m_route.generation)
        return Result::Cancelled;
    auto* monster = Active(call);
    const Vector feet = monster->pev->origin + Vector(0, 0, appearance.feetZ);
    Vector goal = step.goal;
    if (step.result == Result::Ladder)
    {
        const auto* ladder = step.ladder;
        if (!ladder || ladder->m_dir < NORTH || ladder->m_dir > WEST) return Result::Failed;
        if (step.startLadder) motion.traversingLadder = true;
        goal = step.how == GO_LADDER_DOWN ? ladder->m_bottom : ladder->m_top;
        AddDirectionVector(&goal, ladder->m_dir, 16.0f);
        bool complete{};
        if (step.how == GO_LADDER_DOWN)
        {
            if (ladder->m_top.z - 36.0f > feet.z && Square2(feet - goal) < 2500.0f)
                monster->pev->velocity.z = -100.0f;
            complete = ladder->m_bottom.z + 36.0f > feet.z;
        }
        else if (motion.traversingLadder)
        {
            const float range = Square2(feet - goal);
            if (range < 10000.0f) motion.moveType = std::min(motion.maxMoveType, 1);
            if (range < 1600.0f)
            { monster->pev->velocity.z = 150.0f; motion.climbSequence = true; }
            if (feet.z > ladder->m_top.z) motion.traversingLadder = false;
        }
        else
        {
            if (step.hasDepart)
            {
                float closest = 999999.875f;
                for (const auto* area : {ladder->m_topForwardArea, ladder->m_topLeftArea, ladder->m_topRightArea})
                {
                    if (!area) continue;
                    const float range = Square3(step.depart - *area->GetCenter());
                    if (!(range < closest)) continue;
                    closest = range;
                    goal = *area->GetCenter();
                    if (area->GetAttributes() & NAV_CROUCH) motion.crouching = false;
                }
            }
            complete = Square2(feet - goal) < 400.0f;
        }
        if (complete) return CompleteLadder(call, step, world) ? Result::Ladder : Result::Cancelled;
    }
    if (!CSOL_TrackMonsterPath(call, feet, goal, deltaTime, motion.moveType,
        motion.crouching, motion.jumping, skill, motion.velocity,
        motion.moveYaw, motion.turnSide, motion.jumpGoal, motion.facingDirection)) return Result::Failed;
    return step.result;
}

bool CSOL_TrackMonsterPath(CSOLMonsterSpawnCall& call, const Vector& feet,
    const Vector& goal, float deltaTime, int moveType, bool crouching,
    bool jumping, std::int16_t skill, Vector2D& velocity,
    float& moveYaw, float& turnSide, Vector& jumpGoal, Vector2D& facingDirection)
{
    #pragma clang fp contract(off)
    auto* monster = Active(call);
    if (!monster || !Finite(feet) || !Finite(goal) || !std::isfinite(deltaTime) ||
        deltaTime < 0 || moveType < 0 || moveType > 2) return false;
    Vector delta = goal - feet;
    delta.z = 0;
    const float square = delta.x * delta.x + delta.y * delta.y;
    const float distance = static_cast<float>(std::sqrt(static_cast<double>(square)));
    if (!std::isfinite(distance)) return false;
    const Vector facing = distance > 0 ? delta / distance : Vector(1, 0, 0);
    // FaceTowards stores the normalized goal direction before its turn gate.
    // Collision skills consume this history, not the entity's quantized yaw.
    facingDirection = Vector2D(facing.x, facing.y);
    float cosine{}, sine{};
    if (!CSOL_MonsterHeading(moveYaw, cosine, sine)) return false;
    const float forward = facing.x * cosine + facing.y * sine;
    float side = facing.x * -sine + facing.y * cosine;
    if (forward < 0) side = side < 0 ? -1.0f : 1.0f;
    float yaw = moveYaw;
    const bool turned = forward < 0 || std::abs(side) >= 0.05f;
    if (turned) yaw = static_cast<float>(static_cast<double>(deltaTime * 300.0f * side) + moveYaw);
    Vector2D accelerated = velocity;
    if (!jumping)
    {
        if (!(delta.x > -0.01f && delta.x < 0.01f && delta.y > -0.01f && delta.y < 0.01f))
        {
            // MoveTowards uses the original fast vector normalizer, unlike
            // FaceTowards immediately above (which uses sqrt and division).
            const float squared = Square3(delta);
            const float estimate = std::bit_cast<float>(std::uint32_t{0x5f3759d5} -
                (std::bit_cast<std::uint32_t>(squared) >> 1));
            const float half = squared * 0.5f;
            const float first = half * estimate;
            const float second = first * estimate;
            const float inverse = (1.5f - second) * estimate;
            delta *= inverse;
        }
        float acceleration = moveType == 0 ? 0.0f : crouching ? 250.0f : moveType == 1 ? 500.0f : 1000.0f;
        if (monster->pev->maxspeed >= 300.0f && skill != 1) acceleration = 1200.0f;
        const float amount = acceleration * deltaTime;
        accelerated.x += delta.x * amount;
        accelerated.y += delta.y * amount;
    }
    if (!std::isfinite(yaw) || !std::isfinite(accelerated.x) || !std::isfinite(accelerated.y)) return false;
    if (turned) { monster->pev->angles.y = yaw; moveYaw = yaw; turnSide = side; }
    velocity = accelerated;
    jumpGoal = goal;
    return true;
}
