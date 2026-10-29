// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"

namespace position_test
{
void CancellationCases()
{
    // The callback poisons pHit. Any old-path interpretation after one of
    // these invalidations would dereference it rather than simply failing a
    // post-call state comparison.
    for (int cancel = 0; cancel < 6; ++cancel)
    {
        Fixture f;
        f.LongRoute(true);
        f.position.motion.velocity = Vector2D(7, 8);
        f.position.moveFlags = 32;
        f.monster->pev->velocity = Vector(1, 2, 3);
        int rays = 0;
        f.traceResponse = [&](const TraceCall&, TraceResult& hit)
        {
            Require(++rays == 1, "cancelled F4 must not emit another route trace");
            if (cancel == 0) Check(f.Update() == R::Cancelled, "nested F4 rejects same-owner reentry");
            if (cancel == 1) Check(!f.Jump(), "Jump requested during F4 cancels rather than starting");
            if (cancel == 2) f.ClearGraph();
            if (cancel == 3) Require(f.requests.Reset(), "dispatcher generation invalidates F4");
            if (cancel == 4) f.ReborrowSameSlot();
            if (cancel == 5) f.position.follower.Reset();
            f.monster->pev->velocity = Vector(81, 82, 83);
            hit = {}; hit.flFraction = .5f; hit.pHit = reinterpret_cast<edict_t*>(1);
        };
        Check(f.Update(.1f) == R::Cancelled && rays == 1 && !f.position.updating,
            "outer F4 cancels at trace boundary and releases update guard");
        Check(Equal(f.monster->pev->velocity, Vector(81, 82, 83)) &&
            f.position.motion.velocity.x == 7 && f.position.motion.velocity.y == 8 &&
            f.position.moveFlags == 32 && f.position.jumpUntil == -1,
            "cancelled F4 preserves callback velocity, old accumulator and no Jump startup");
    }
    {
        Fixture f;
        f.LongRoute();
        // Off-mesh endpoint forces Build through the real nearest-area
        // ground trace, before F4 can enter Advance.
        f.position.destination = Vector(2000, 0, 0);
        bool replaced = false;
        std::uint64_t replacementRevision = 0;
        f.traceResponse = [&](const TraceCall& ray, TraceResult& hit)
        {
            if (!replaced)
            {
                replaced = true;
                Require(f.position.follower.Build(*f.call, f.world, Vector(0, 0, 0),
                    Vector(20, 0, 0), false, {}), "Build callback installs replacement route");
                replacementRevision = f.position.follower.Revision();
            }
            hit = {}; hit.flFraction = 1; hit.vecEndPos = ray.to; hit.vecPlaneNormal = Vector(0, 0, 1);
        };
        Check(f.Update() == R::Cancelled && replaced && f.position.follower.HasRoute() &&
            f.position.follower.Revision() == replacementRevision,
            "F4 checks expected Build revision and preserves callback replacement route");
    }
    for (int invalidation = 0; invalidation < 4; ++invalidation)
    {
        Fixture f;
        f.OneArea();
        f.HookAnimation();
        Require(f.monster->Animation().LockUntil(4), "seed previous animation lock");
        f.onAnimation = [&](Hook*, CBaseAnimating*)
        {
            if (invalidation == 0) Require(f.requests.Reset(), "Jump hook changes dispatcher generation");
            if (invalidation == 1) f.ReborrowSameSlot();
            if (invalidation == 2) f.monster->pev->deadflag = DEAD_DEAD;
            if (invalidation == 3) Require(f.pool.ParkForDeath(*f.monster), "Jump hook parks same lease");
            Require(f.monster->Animation().LockUntil(99), "callback replacement owns its lock");
        };
        Check(!f.Jump() && !f.position.startingJump && f.monster->Animation().LockedUntil() == 99,
            "Jump cannot install old lock after hook invalidates session, lease, alive or borrowed state");
    }
    {
        Fixture f;
        f.OneArea();
        f.position.worldGeneration = f.world.Generation();
        f.ClearGraph();
        f.Flat(0, 0);
        f.Grid();
        Check(f.Update() == R::Cancelled && f.traces.empty(),
            "changed NAV generation rejects dangling cached area before dereference");
    }
}
}
