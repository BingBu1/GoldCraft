// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"

namespace position_test
{
void JumpCases()
{
    {
        Fixture f;
        f.position.motion.jumpGoal = Vector(3, 4, 1000);
        Require(CSOL_PrepareMonsterJumpDirection(*f.call, f.position), "prepare actual XY jump direction");
        const Vector2D direction = f.position.jumpDirection;
        Check(direction.x > .59f && direction.x < .60f && direction.y > .79f && direction.y < .80f,
            "jump direction retains approximate inverse square root rather than exact normalization");
        f.monster->pev->origin = Vector(100, -200, 50);
        f.position.motion.jumpGoal = Vector(103, -196, -1000);
        Check(CSOL_PrepareMonsterJumpDirection(*f.call, f.position) &&
            f.position.jumpDirection.x == direction.x && f.position.jumpDirection.y == direction.y,
            "cached jump target is relative to current origin; height does not affect planar direction");
        f.position.motion.jumpGoal = f.monster->pev->origin;
        Check(CSOL_PrepareMonsterJumpDirection(*f.call, f.position) &&
            f.position.jumpDirection.x == 0 && f.position.jumpDirection.y == 0,
            "zero planar distance produces finite zero direction");
    }
    {
        Fixture f;
        f.OneArea();
        f.position.jumpUntil = 10;
        f.position.moveFlags = 8;
        f.monster->pev->velocity = Vector(0, 0, 0);
        Check(!f.Jump() && f.position.moveFlags == 8 && f.monster->pev->origin.z == 0,
            "Jump strict deadline equality has no startup side effect");
        f.engine.globals.time = std::nextafter(10.0f, 11.0f);
        f.monster->pev->flags &= ~FL_ONGROUND;
        Require(f.Jump(), "airborne Jump is permitted after deadline");
        Check(f.monster->pev->origin.z == 2 && f.position.moveFlags == 10 &&
            !f.position.jumpStarted && f.position.motion.jumping,
            "low-speed startup lifts origin2 and preserves unrelated move flag bits");
        Check(f.position.jumpInterval == 2 && f.position.jumpStamp == f.engine.globals.time &&
            f.position.jumpUntil == f.engine.globals.time + 2,
            "Jump records two-second interval and startup timestamp");
        Check(f.monster->pev->sequence == 5 && f.monster->pev->frame == 0 &&
            f.monster->Animation().CachedSequence() == 5 &&
            f.monster->Animation().LockedUntil() == f.engine.globals.time + .75f && !f.position.startingJump,
            "real SDK sequence reset and animation lock complete before Jump guard clears");
    }
    for (int skill : {0, 7})
    {
        Fixture f;
        f.OneArea();
        f.types.back().skill = static_cast<std::int16_t>(skill);
        f.position.jumpDirection = Vector2D(.6f, -.8f);
        f.position.motion.velocity = Vector2D(7, 8);
        f.monster->pev->velocity = Vector(0, 0, 7);
        f.monster->pev->flags |= FL_ONGROUND;
        Require(f.Jump(), "jump starts with nonzero original velocity");
        Check(f.monster->pev->origin.z == 0, "nonzero speed does not receive startup lift");
        const float speed = skill == 7 ? 300.0f : 150.0f;
        const float z = skill == 7 ? 507.0f : 307.0f;
        Check(f.Update(.1f) == R::Jumping && f.monster->pev->velocity.z == z &&
            f.monster->pev->velocity.x == speed * .6f && f.monster->pev->velocity.y == speed * -.8f,
            "first Jump update adds ordinary300 or skill7 500 vertical impulse and sets planar speed");
        Check(f.Update(.1f) == R::Jumping && f.monster->pev->velocity.z == z &&
            !f.position.leftGround && f.position.jumpUntil == 12,
            "remaining on ground does not count as landing or add a second impulse");
        f.monster->pev->flags &= ~FL_ONGROUND;
        Check(f.Update(.1f) == R::Jumping && f.position.leftGround,
            "later airborne observation arms landing detection");
        f.monster->pev->flags |= FL_ONGROUND;
        Check(f.Update(.1f) == R::Jumping && f.position.jumpUntil == -1 && f.position.jumpStamp == -1 &&
            !f.position.motion.jumping && f.monster->pev->velocity.x == speed * .6f &&
            f.position.motion.velocity.x == 7 && f.position.moveFlags == 2,
            "landing tick clears timers but still bypasses friction, effects and move flag reset");
        Check(f.Update() == R::Applied && Near(f.monster->pev->velocity.x, 7) &&
            Near(f.monster->pev->velocity.y, 8) && f.position.moveFlags == 0,
            "following non-jump tick resumes stored planar accumulator and final-motion stage");
    }
    {
        Fixture f;
        f.OneArea();
        f.position.jumpUntil = 10;
        f.position.jumpDirection = Vector2D(1, 0);
        Check(f.Update() == R::Jumping, "F4 jumping state includes jump-deadline equality");
        f.engine.globals.time = std::nextafter(10.0f, 11.0f);
        Check(f.Update() == R::Applied && !f.position.motion.jumping,
            "F4 expires jumping immediately after deadline without waiting for landing");
    }
    {
        Fixture f;
        f.OneArea();
        f.HookAnimation();
        int hookCalls = 0;
        f.onAnimation = [&](Hook* chain, CBaseAnimating* entity)
        {
            ++hookCalls;
            const auto serial = f.position.updateSerial;
            const auto revision = f.position.follower.Revision();
            const auto until = f.position.jumpUntil;
            const Vector origin = f.monster->pev->origin;
            Check(f.Update() == R::Cancelled && !f.Jump() &&
                !CSOL_PrepareMonsterJumpDirection(*f.call, f.position),
                "sequence callback rejects nested F4, Jump and direction preparation");
            Check(f.position.updateSerial == serial && f.position.follower.Revision() == revision &&
                f.position.jumpUntil == until && Equal(f.monster->pev->origin, origin),
                "nested sequence calls leave active Jump transaction unchanged");
            f.engine.globals.time = 20;
            chain->callNext(entity);
        };
        Check(f.Jump() && hookCalls == 1 && !f.position.startingJump,
            "outer Jump survives rejected nested sequence calls");
        Check(f.position.jumpStamp == 10 && f.position.jumpUntil == 12 &&
            f.monster->Animation().LockedUntil() == 20.75f,
            "Jump clock uses pre-hook time and animation lock uses post-hook time");
    }
}
}
