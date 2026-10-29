// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
#include "hostage/hostage.h"

namespace ordinary_test
{
namespace
{
bool Same(const CSOLMonsterGaitState& a, const CSOLMonsterGaitState& b)
{
    return a.sequence == b.sequence && a.frame == b.frame && a.yaw == b.yaw &&
        a.estimatedYaw == b.estimatedYaw && a.previousOrigin == b.previousOrigin &&
        a.pitchController == b.pitchController && a.yawController == b.yawController &&
        a.distance == b.distance && a.turnRate == b.turnRate;
}
void Update(Fixture& f)
{
    auto call = f.Call(); ConfigError error;
    Require(CSOL_UpdateMonsterGait(call, error) && error.message.empty(), "real gait update succeeds");
}
void Prepare(Fixture& f, float dt)
{
    auto& monster = f.Monster();
    monster.pev->gaitsequence = 1; monster.pev->angles = Vector(0,0,0);
    monster.pev->origin = Vector(0,0,0); monster.pev->framerate = 1; monster.pev->frame = 73;
    monster.GaitState() = {}; f.engine.globals.frametime = dt;
    f.model.sequence[1].numframes = 11; f.model.sequence[1].fps = 8;
    f.model.sequence[1].linearmovement = Vector(0,0,0);
}
}
void GaitCases()
{
    // Expected boundaries come from the sealed DOT/gait contract. These
    // constants intentionally do not implement a second copy of its formula.
    for (int sequence : {0, -1})
    {
        Fixture f; auto& monster = f.Spawn();
        monster.pev->gaitsequence = sequence;
        monster.GaitState() = {7, 2, 3, 4, Vector(5,6,7), 8, 9, 10, 11};
        const auto before = monster.GaitState(); const auto frame = monster.pev->frame;
        monster.pev->controller[0] = 17; monster.pev->controller[1] = 19;
        f.engine.globals.frametime = std::numeric_limits<float>::quiet_NaN();
        const int queries = f.modelQueries;
        Update(f);
        Check(Same(monster.GaitState(), before) && monster.pev->controller[0] == 17 &&
            monster.pev->controller[1] == 19 && monster.pev->frame == frame && f.modelQueries == queries,
            "nonpositive gait returns before state writes model query or frametime validation");
    }
    for (float dt : {-1.0f, 0.0f})
    {
        Fixture f; auto& monster = f.Spawn(); Prepare(f, dt);
        auto& gait = monster.GaitState(); gait.previousOrigin = Vector(10,20,30);
        gait.yaw = 17; gait.estimatedYaw = 19; gait.turnRate = 2; gait.distance = 9; gait.frame = 4;
        Update(f);
        Check(gait.distance == 0 && gait.previousOrigin == Vector(10,20,30) && gait.yaw == 17 &&
            gait.estimatedYaw == 19 && gait.turnRate == 2 && gait.frame == 4,
            "zero or clamped-negative dt only clears estimator distance and leaves persistent estimate history");
        Check(monster.pev->frame == 73 && gait.sequence == 1, "positive gait still runs main tail at dt zero");
    }
    for (float dt : {.125f, 1.0f, 2.0f})
    {
        Fixture f; auto& monster = f.Spawn(); Prepare(f, dt); Update(f);
        Check(monster.GaitState().frame == (dt == .125f ? 1.0f : 8.0f),
            "default frame path uses actual frametime with upper clamp one");
        Check(monster.pev->frame == 73, "gait frame never overwrites ordinary animation frame");
    }
    for (float distance : {std::nextafter(5.0f, 0.0f), 5.0f})
    {
        Fixture f; auto& monster = f.Spawn(); Prepare(f, 1);
        monster.pev->origin.x = distance; f.model.sequence[1].linearmovement.x = 10;
        Update(f);
        Check(monster.GaitState().distance == (distance == 5 ? 5.0f : 0.0f),
            "speed five is moving while adjacent lower speed is stationary");
        Check(monster.GaitState().frame == (distance == 5 ? 5.5f : 0.0f),
            "linear gait uses numframes eleven rather than numframes minus one");
    }
    {
        Fixture f; auto& monster = f.Spawn(); Prepare(f, 1);
        monster.pev->origin = Vector(3,0,4); f.model.sequence[1].linearmovement.x = 10;
        Update(f);
        Check(monster.GaitState().distance == 5 && monster.GaitState().frame == 5.5f,
            "vertical displacement participates in actual three dimensional gait distance");
    }
    struct Turn { float angle, previous, expected; };
    for (const auto row : std::array<Turn, 9>{{{0,2,.05f},{4,2,.05f},{5,2,2},{-5,2,2},
        {89,2,2},{-89,2,2},{90,2,3.5f},{-90,2,3.5f},{45,.05f,.05f}}})
    {
        Fixture f; auto& monster = f.Spawn(); Prepare(f, .125f);
        monster.GaitState().turnRate = row.previous; monster.pev->angles.y = row.angle;
        Update(f);
        Check(monster.GaitState().turnRate == row.expected, "stationary turn factor preserves exact five and eighty-nine boundaries");
    }
    for (float dt : {.125f, .25f})
    {
        Fixture f; auto& monster = f.Spawn(); Prepare(f, dt);
        monster.GaitState().turnRate = 2; monster.pev->angles.y = 16;
        Update(f);
        Check(monster.GaitState().yaw == 4 && monster.GaitState().estimatedYaw == 4,
            "quarter-second boundary stops multiplying by persistent turn factor");
    }
    for (float rate : {.025f, .05f})
    {
        Fixture f; auto& monster = f.Spawn(); Prepare(f, .125f);
        monster.GaitState().turnRate = rate; monster.pev->angles.y = 16;
        Update(f);
        Check(monster.GaitState().yaw == (rate == .025f ? 0.0f : .1f),
            "turn step below .1 is suppressed and float .1 is preserved");
    }
    for (float yaw : {120.0f, -120.0f, 121.0f, -121.0f})
    {
        Fixture f; auto& monster = f.Spawn(); Prepare(f, 1);
        monster.pev->origin = Vector(10,0,0); monster.pev->angles.y = yaw;
        auto& gait = monster.GaitState(); Update(f);
        const bool reverse = std::abs(yaw) > 120;
        Check(gait.distance == (reverse ? -10 : 10) && gait.estimatedYaw == 0 &&
            gait.yaw == (yaw > 120 ? -180 : yaw < -120 ? 180 : 0),
            "reverse gate is strict at plus/minus120 and does not resync saved estimate yaw");
    }
    {
        Fixture f; auto& monster = f.Spawn(); Prepare(f, 0);
        monster.pev->angles.y = .25f; Update(f);
        Check(monster.pev->controller[0] == 127 && monster.GaitState().yawController > 127 &&
            monster.GaitState().yawController < 128,
            "yaw byte truncates while owned cache preserves pretruncation fraction");
    }
    struct Pitch { float angle; int controller; };
    for (const auto row : std::array<Pitch, 8>{{{-15,255},{-14.9f,252},{0,127},{14.9f,2},
        {15,0},{345,255},{-345,255},{715,0}}})
    {
        Fixture f; auto& monster = f.Spawn(); Prepare(f, 0); monster.pev->angles.x = row.angle;
        Update(f);
        Check(monster.pev->controller[1] == row.controller && monster.GaitState().pitchController == row.controller,
            "pitch truncates before boundary mapping and subtracts one revolution only above180");
    }
    {
        Fixture f; auto& monster = f.Spawn(); Prepare(f, .125f);
        monster.pev->angles.x = -15; monster.GaitState().frame = 4; f.nullModel = true;
        bool controllersBeforeQuery{};
        f.onModel = [&] { controllersBeforeQuery = monster.pev->controller[0] == 128 && monster.pev->controller[1] == 255; };
        Update(f);
        Check(controllersBeforeQuery && monster.GaitState().frame == 4 && monster.pev->frame == 73,
            "controllers update before missing-model early return and neither frame is overwritten");
    }
    {
        Fixture f; auto& monster = f.Spawn(); Prepare(f, 1);
        f.model.sequence[1].linearmovement.x = 10;
        monster.pev->origin.x = 5; monster.pev->angles.y = 180; monster.GaitState().frame = 1;
        Update(f);
        Check(monster.GaitState().frame == 6.5f, "negative distance wraps internal gait frame by eleven");
        monster.pev->gaitsequence = 2; f.model.sequence[2].fps = 0;
        f.engine.globals.frametime = 0; monster.GaitState().turnRate = 2;
        Update(f);
        Check(monster.GaitState().frame == 6.5f && monster.GaitState().turnRate == 2,
            "sequence change preserves frame and turn factor");
        const auto before = monster.GaitState(); Require(f.Reset(), "ordinary reset with persistent gait");
        Check(Same(monster.GaitState(), before), "ordinary behavior reset preserves host gait state");
    }
    {
        Fixture f; auto& monster = f.Spawn(); Prepare(f, .125f);
        auto call = f.Call(); ConfigError error; bool valid = true;
        f.onModel = [&] { valid = false; monster.GaitState().frame = 7; };
        Check(!CSOL_UpdateMonsterGait(call, error, [](const void* value) { return *static_cast<const bool*>(value); }, &valid) &&
            monster.GaitState().frame == 7, "owner invalidation in real model query prevents stale gait frame write");
    }
    {
        Fixture f; auto& monster = f.Spawn(); Prepare(f, .125f);
        cv_hostage_stop.value = 1; f.SpawnState().animationAt = 10000; f.SpawnState().updateAt = 0;
        f.Tick(100);
        Check(monster.GaitState().frame == 1 && monster.HostState().statusUpdateAt == 101,
            "owner first due pass runs actual gait and status");
        f.Tick(100.03125f);
        Check(monster.GaitState().frame == 1, "animation-only owner tick does not bypass D0 effects cadence");
        f.Tick(100.125f);
        Check(monster.GaitState().frame == 2 && monster.HostState().statusUpdateAt == 101,
            "gait advances at next due pass even when one-second DOT gate remains closed");
        f.Tick(105);
        Check(monster.GaitState().frame == 3 && monster.HostState().statusUpdateAt == 106,
            "missed seconds do not produce catch-up effects passes");
    }
}
}
