// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"

namespace position_test
{
void SchedulingCases()
{
    {
        Fixture f;
        f.OneArea();
        f.position.updateAfter = 10;
        f.position.motion.velocity = Vector2D(8, 9);
        f.monster->pev->velocity = Vector(1, 2, 3);
        Check(f.Update() == R::Skipped && Equal(f.monster->pev->velocity, Vector(1, 2, 3))
            && f.position.routeTick == 1 && f.traces.empty(),
            "F4 equality at owner deadline skips before route or velocity writes");
        f.engine.globals.time = std::nextafter(10.0f, 11.0f);
        Check(f.Update() == R::Applied && Near(f.monster->pev->velocity.x, 8) &&
            Near(f.monster->pev->velocity.y, 9) && f.monster->pev->velocity.z == 3,
            "first representable time after owner deadline applies");
        Check(f.position.updateAfter == 10 && !f.position.updating,
            "F4 consumes deadline; cadence advancement belongs to real owner");
    }
    for (int gate = 0; gate < 3; ++gate)
    {
        Fixture f;
        f.OneArea();
        if (gate == 0) f.types.back().speed = 0;
        const float hostAfter = gate == 1 ? std::nextafter(10.0f, 11.0f) : 10.0f;
        f.monster->pev->velocity = Vector(1, 2, 3);
        const auto result = f.Update(0, hostAfter);
        Check(result == (gate < 2 ? R::Skipped : R::Applied),
            "zero type speed and future host timer skip; equal host timer permits F4");
        if (gate < 2)
            Check(Equal(f.monster->pev->velocity, Vector(1, 2, 3)) && f.traces.empty(),
                "host/type gate has no movement side effect");
    }
    {
        Fixture f;
        f.OneArea();
        auto* cached = f.position.lastArea;
        f.monster->pev->origin = Vector(40, 40, 1000);
        f.position.destination = f.monster->pev->origin;
        f.position.missingAreaUntil = 2;
        Check(f.Update() == R::Applied && f.position.lastArea == cached &&
            f.position.missingAreaUntil == -1,
            "cached area uses inclusive XY overlap even far above its surface");
        f.monster->pev->origin.x = std::nextafter(40.0f, 41.0f);
        f.position.destination = f.monster->pev->origin;
        Check(f.Update() == R::Applied && f.position.lastArea == cached &&
            f.position.missingAreaUntil == 20,
            "outside cached XY with failed lookup preserves old pointer and starts ten-second timer");
        f.engine.globals.time = 20;
        Check(f.Update() == R::Applied, "missing-area timeout equality remains active");
        f.engine.globals.time = std::nextafter(20.0f, 21.0f);
        f.monster->pev->velocity = Vector(7, 8, 9);
        Check(f.Update() == R::NavigationExpired && Equal(f.monster->pev->velocity, Vector(7, 8, 9)),
            "strictly expired missing-area timer returns before final velocity");
        f.monster->pev->origin = Vector(0, 0, 1000);
        f.position.destination = f.monster->pev->origin;
        Check(f.Update() == R::Applied && f.position.missingAreaUntil == -1,
            "regained cached XY clears expired missing-area timer");
    }
    {
        Fixture f;
        f.OneArea();
        f.position.lastArea = nullptr;
        f.monster->pev->origin.z = 120;
        f.position.destination = f.monster->pev->origin;
        Check(f.Update() == R::Applied && f.position.lastArea == f.areas.front(),
            "uncached F4 lookup includes floor exactly120 below origin");
        f.position.lastArea = nullptr;
        f.monster->pev->origin.z = std::nextafter(120.0f, 121.0f);
        f.position.destination = f.monster->pev->origin;
        Check(f.Update() == R::Applied && !f.position.lastArea && f.position.missingAreaUntil == 20,
            "uncached floor beyond120 starts missing-area timer");
    }
    {
        Fixture f;
        f.OneArea();
        f.position.faceRequested = true;
        f.position.faceGoal = Vector(-100, 0, 999);
        f.position.motion.velocity = Vector2D(0, 0);
        Check(f.Update(.1f) == R::Applied && f.position.motion.moveYaw == -30 &&
            f.monster->pev->angles.y == -30 && f.position.motion.turnSide == -1,
            "independent face request turns toward rear target using XY and actual deltaTime");
        Check(f.position.motion.facingDirection.x == -1 && f.position.motion.facingDirection.y == 0,
            "face request stores exact rear direction before partial yaw turn");
        f.position.faceGoal = Vector(100, 0, -999);
        f.position.motion.moveYaw = 0;
        f.monster->pev->angles.y = 0;
        Check(f.Update(.1f) == R::Applied && !f.position.faceRequested && f.monster->pev->angles.y == 0,
            "front deadzone clears face request without yaw drift");
        Check(f.position.motion.facingDirection.x == 1 && f.position.motion.facingDirection.y == 0,
            "face request deadzone still replaces previous contact direction");
    }
}

void RouteCases()
{
    {
        Fixture f;
        f.LongRoute();
        f.position.routeTick = 2;
        const auto revision = f.position.follower.Revision();
        Check(f.Update() == R::Applied && !f.position.follower.HasRoute() &&
            f.position.routeTick == 3 && f.position.follower.Revision() == revision,
            "old even route tick skips empty-route build");
        Check(f.Update() == R::Applied && f.position.follower.HasRoute() &&
            f.position.routeTick == 4 && f.position.follower.Revision() == revision + 1,
            "old odd tick builds actual NAV route once");
        Check(f.Update() == R::Applied && f.position.follower.HasRoute() &&
            f.position.routeTick == 5 && f.position.follower.Revision() == revision + 1,
            "existing route advances without rebuilding on later odd tick");
    }
    for (int boundary = 0; boundary < 4; ++boundary)
    {
        Fixture f;
        f.LongRoute();
        if (boundary == 0) f.monster->pev->basevelocity = Vector(10, 0, 0);
        if (boundary == 1) f.monster->pev->basevelocity = Vector(std::nextafter(10.0f, 0.0f), 0, 0);
        if (boundary == 2) f.monster->pev->basevelocity = Vector(0, 0, 1000);
        if (boundary == 3) f.position.destination.x = -99999;
        Check(f.Update() == R::Applied, "route gate still reaches final motion");
        Check(f.position.follower.HasRoute() == (boundary == 1 || boundary == 2),
            "build requires horizontal base speed squared below100 and valid destination sentinel");
        Check(f.position.routeTick == 2, "route attempt counter increments even when build gate rejects");
    }
    for (bool atBoundary : {false, true})
    {
        Fixture f;
        f.OneArea();
        f.position.destination = Vector(atBoundary ? 20.0f : std::nextafter(20.0f, 0.0f), 0, 0);
        Check(f.Update() == R::Applied && f.position.routeTick == (atBoundary ? 2u : 1u),
            "route20 distance boundary is strict and nearby idle destination does not consume tick");
    }
    for (bool enabled : {false, true})
    {
        Fixture f;
        f.LongRoute(true);
        f.position.obstacleAfter = enabled ? std::nextafter(10.0f, 0.0f) : 10.0f;
        int feelers = 0;
        f.traceResponse = [&](const TraceCall& ray, TraceResult& hit)
        {
            hit = {}; hit.flFraction = 1; hit.vecEndPos = ray.to; hit.vecPlaneNormal = Vector(0, 0, 1);
            if (ray.from.z == 18.1f && ray.to.z == 18.1f) ++feelers;
        };
        Check(f.Update() == R::Applied && feelers == (enabled ? 2 : 0),
            "F4 enables real feelers strictly after obstacle deadline");
    }
}
}
