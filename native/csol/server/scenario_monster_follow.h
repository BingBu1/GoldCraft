// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_monster_factory.h"
#include "scenario_nav_path.h"
#include "scenario_nav_world.h"
#include <optional>

enum class CSOLMonsterFollowResult
{
    Empty, Moving, Ladder, Arrived, InvalidPath, Stuck, FellOff, Cancelled, Failed
};

struct CSOLMonsterFollowInput
{
    // Borrowed from the same NAV world as the route. The owner invalidates
    // the world before releasing either areas or the old map's edicts.
    CNavArea* lastArea{};
    float moveYaw{};
    bool crouching{}, jumping{}, running{}, usingLadder{}, avoidObstacles{};
    float standUntil{-1.0f};
};

struct CSOLMonsterFollowStep
{
    CSOLMonsterFollowResult result{CSOLMonsterFollowResult::Empty};
    Vector goal{};
    bool crouch{}, approachingJump{};
    bool clearClimb{}, startLadder{};
    bool clearAnimationQueue{};
    const CNavLadder* ladder{};
    NavTraverseType how{NUM_TRAVERSE_TYPES};
    Vector approach{}, depart{};
    bool hasApproach{}, hasDepart{};
    std::uint64_t revision{};
    int segment{};
};

struct CSOLMonsterRouteMotion
{
    Vector2D velocity{};
    Vector jumpGoal{};
    float moveYaw{}, turnSide{};
    int moveType{}, maxMoveType{2};
    bool crouching{}, jumping{}, traversingLadder{}, climbSequence{};
    float standUntil{-1.0f};
    bool clearAnimationQueue{};
    // Ordinary initial behavior state: arrival sets MoveDone (2).
    int moveState{2};
};

// The ordinary monster's concrete route/steering owner. Performs real engine
// visibility queries; it does not substitute hostage CImprov callbacks.
// Ladder steps must be consumed by actual ladder movement, then acknowledged
// with CompleteLadder. They are never treated as an ordinary walk or success.
class CCSOLMonsterPathFollower
{
public:
    void Reset();
    void ResetStuck();
    bool Build(CSOLMonsterSpawnCall& call, const CCSOLScenarioNavWorld& world,
        const Vector& feet, const Vector& goal, bool climber,
        std::span<const CNavLadder* const> climberLadders);
    CSOLMonsterFollowStep Update(CSOLMonsterSpawnCall& call,
        const CCSOLScenarioNavWorld& world,
        const csol::scenario::MonsterAppearance& appearance,
        const CSOLMonsterFollowInput& input);
    // Concrete ordinary consumer: applies pose, ladder state/vertical speed,
    // and TrackPath acceleration. The caller subsequently applies friction,
    // state effects and neighboring-monster forces to the planar accumulator.
    CSOLMonsterFollowResult Advance(CSOLMonsterSpawnCall& call,
        const CCSOLScenarioNavWorld& world,
        const csol::scenario::MonsterAppearance& appearance,
        CNavArea* lastArea, CSOLMonsterRouteMotion& motion,
        float deltaTime, std::int16_t skill, bool avoidObstacles);
    bool CompleteLadder(CSOLMonsterSpawnCall& call, const CSOLMonsterFollowStep& step,
        const CCSOLScenarioNavWorld& world);
    std::uint64_t Revision() const { return m_revision; }
    bool HasRoute() const { return m_route.count > 0; }
    std::optional<NavDirType> LadderDirection(CSOLMonsterSpawnCall& call,
        const CCSOLScenarioNavWorld& world) const;

private:
    struct Query;
    bool SameOwner(CSOLMonsterSpawnCall& call) const;
    int FindClosest(Query& query, Vector& close) const;
    int FindPoint(Query& query, Vector& point, int& behind) const;
    bool AdjustFeelers(Query& query, Vector& goal) const;
    bool StandUp(Query& query, CSOLMonsterFollowStep& step) const;
    bool UpdateStuck(const Vector& centroid, bool usingLadder, float now);
    CSOLScenarioRoute m_route;
    std::optional<CSOLMonsterSpawnCall> m_owner;
    std::uint64_t m_revision{};
    int m_segment{1};
    bool m_ladderStarted{}, m_stuck{};
    Vector m_stuckSpot{}, m_lastCentroid{};
    float m_stuckStart{-1.0f};
    // CSO's constructor does not initialize lastTime in the recovered path.
    // Seed it explicitly instead of exposing uninitialized host memory.
    float m_lastTime{};
    std::array<float, 5> m_speeds{};
    int m_speedIndex{}, m_speedCount{};
};

// Actual ordinary TrackPath stage. Changes yaw and the behavior's planar
// acceleration accumulator; friction, speed effects and neighboring monster
// avoidance run afterwards, before the entity velocity is committed.
bool CSOL_TrackMonsterPath(CSOLMonsterSpawnCall& call, const Vector& feet,
    const Vector& goal, float deltaTime, int moveType, bool crouching,
    bool jumping, std::int16_t skill, Vector2D& velocity,
    float& moveYaw, float& turnSide, Vector& jumpGoal);
