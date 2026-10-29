// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"

namespace position_test
{
Fixture::Fixture()
{
    types.back().speed = 200;
    monster->SpeedState().baseSpeed = 200;
    monster->pev->basevelocity = Vector(0, 0, 0);
    monster->pev->velocity = Vector(0, 0, 7);
    position.destination = monster->pev->origin;
    position.motion.moveType = position.motion.maxMoveType = 2;
    position.motion.moveState = 1;
    position.obstacleAfter = 1000;
    sequences[csol::scenario::MonsterSequence::Jump] = 5;
    ClearTraces();
}

Fixture::~Fixture()
{
    occupancy.Shutdown();
}

void Fixture::OneArea()
{
    position.lastArea = Flat(0, 0);
    Grid();
}

void Fixture::LongRoute(bool build)
{
    auto* start = Flat(0, 0);
    auto* finish = Flat(1000, 0);
    start->ConnectTo(finish, EAST);
    finish->ConnectTo(start, WEST);
    Grid();
    position.lastArea = start;
    position.destination = Vector(1000, 0, 0);
    if (build)
        Require(position.follower.Build(*call, world, monster->pev->origin,
            position.destination, false, {}), "real long route ready for F4");
}

R Fixture::Update(float deltaTime, float hostMoveAfter)
{
    return CSOL_UpdateOrdinaryMonsterPosition(*call, world, occupancy, appearance,
        position, deltaTime, hostMoveAfter, 1000.0f, {});
}

bool Fixture::Jump()
{
    return CSOL_StartMonsterJump(*call, position, sequences);
}
}
