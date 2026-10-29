// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"

namespace position_test
{
void LadderCases()
{
    for (int direction = NORTH; direction <= WEST; ++direction)
    {
        Fixture f;
        auto* bottom = f.Flat(0, 0);
        auto* top = f.Flat(0, 0, 100);
        CNavLadder ladder;
        ladder.m_bottomArea = bottom;
        ladder.m_topForwardArea = top;
        ladder.m_bottom = Vector(0, 0, 0);
        ladder.m_top = Vector(0, 0, 100);
        ladder.m_dir = static_cast<NavDirType>(direction);
        ladder.m_isDangling = false;
        bottom->AddLadderUp(&ladder);
        f.Grid();
        f.types.back().appearance = "zombieclimber";
        f.position.lastArea = bottom;
        f.position.destination = Vector(0, 0, 100);
        Require(f.position.follower.Build(*f.call, f.world, Vector(0, 0, 0),
            f.position.destination, true, {}), "real climber ladder route");
        Check(f.position.follower.LadderDirection(*f.call, f.world) == ladder.m_dir,
            "accessor reads current route segment ladder direction");
        const std::array<float, 4> expectedYaw{90, 180, 270, 0};
        Check(f.Update() == R::Applied && f.position.motion.climbSequence &&
            f.monster->pev->velocity.z == 150 && f.monster->pev->angles.y == expectedYaw[direction],
            "F4 climber maps N/E/S/W to90/180/270/0 and commits climb vertical velocity");
        if (direction == NORTH)
        {
            f.ReborrowSameSlot();
            CSOLMonsterSpawnCall fresh(f.map, f.pool, f.requests, *f.monster);
            Check(!f.position.follower.LadderDirection(fresh, f.world),
                "fresh lease cannot read prior lease ladder direction");
        }
        else if (direction == EAST)
        {
            f.ClearGraph();
            Check(!f.position.follower.LadderDirection(*f.call, f.world),
                "world reset rejects released ladder-route areas before access");
        }
        else if (direction == SOUTH)
        {
            f.position.follower.Reset();
            Check(!f.position.follower.LadderDirection(*f.call, f.world),
                "reset follower exposes no stale ladder direction");
        }
        else
        {
            ladder.m_dir = static_cast<NavDirType>(NUM_DIRECTIONS);
            Check(!f.position.follower.LadderDirection(*f.call, f.world),
                "malformed ladder direction is rejected");
        }
    }
}
}
