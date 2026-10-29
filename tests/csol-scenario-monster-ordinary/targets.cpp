// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"

namespace ordinary_test
{
namespace
{
CSOLOrdinaryMonsterState& Prepare(Fixture& f)
{
    f.Spawn();
    auto& state = f.CaptureState();
    state.melee.attack.deadline = 1000;
    state.findPath.deadline = 1000;
    state.findTarget.deadline = 0;
    return state;
}
CBasePlayer& Player(Fixture& f, int slot, float x, TeamName team = CT)
{
    auto& player = f.engine.Client(slot);
    player.m_iTeam = team;
    player.pev->origin = Vector(x, 0, 0);
    return player;
}
void Select(Fixture& f, CSOLOrdinaryMonsterState& state, float now)
{
    state.findTarget.deadline = 0;
    state.melee.attack.deadline = state.findPath.deadline = 1000;
    f.Tick(now);
}
}

void TargetCases()
{
    {
        Fixture f; auto& state = Prepare(f);
        Player(f, 1, 1, TERRORIST);
        auto& dead = Player(f, 2, 2); dead.pev->health = 0;
        auto& dormant = Player(f, 3, 3); dormant.pev->flags = static_cast<int>(0x80000000u);
        auto& wanted = Player(f, 4, 50);
        Select(f, state, 101);
        Check(state.target.Get<CBaseEntity>() == &wanted,
            "same-team dead and exactly dormant players excluded before nearest selection");
        Check(Near(state.findTarget.interval, 3 + state.jitter) && Near(state.findTarget.deadline, 104 + state.jitter),
            "actual target clock uses three seconds plus this tree's CRT jitter");
    }
    {
        Fixture f; auto& state = Prepare(f);
        auto& excluded = Player(f, 1, 1);
        auto& allowed = Player(f, 2, 2); allowed.pev->flags = static_cast<int>(0x80000000u | FL_MONSTER);
        f.services->excluded = [&](const EHANDLE& handle) { return handle.Get<CBaseEntity>() == &excluded; };
        Select(f, state, 101);
        Check(state.target.Get<CBaseEntity>() == &allowed,
            "feature exclusion applied; dormant plus other flags is not the exact dormant value");
    }
    {
        Fixture f; auto& state = Prepare(f);
        auto& nearest = Player(f, 1, 25); Player(f, 2, 50); Player(f, 3, 100);
        Select(f, state, 101);
        Check(state.target.Get<CBaseEntity>() == &nearest,
            "nearest distance below close-range threshold takes deterministic nearest path");
        auto& retained = Player(f, 4, 150); state.target = &retained;
        f.Monster().HostState().targetPolicy = 100;
        Select(f, state, 102);
        Check(state.target.Get<CBaseEntity>() == &retained, "policy100 preserves living previous target");
        f.Monster().HostState().targetPolicy = 200;
        retained.pev->rendermode = kRenderTransAlpha; retained.pev->renderamt = 0;
        Select(f, state, 103);
        Check(!state.target.Get<CBaseEntity>(), "policy200 clears invisible previous player without choosing nearest in same call");
    }
    {
        Fixture f; auto& state = Prepare(f);
        auto& a = Player(f, 1, 100); auto& b = Player(f, 2, 120); auto& c = Player(f, 3, 150);
        Select(f, state, 101);
        auto* selected = state.target.Get<CBaseEntity>();
        Check(selected == &a || selected == &b || selected == &c,
            "three nearby candidates use actual CRT choice within the qualifying set");
        Check(!state.distantTarget.Get<CBaseEntity>(), "nearby random choice does not populate far cache");
    }
    {
        Fixture f; auto& state = Prepare(f);
        auto& a = Player(f, 1, 900); auto& b = Player(f, 2, 1100);
        Select(f, state, 101);
        const EHANDLE selected = state.target;
        Require(selected.Get<CBaseEntity>() == &a || selected.Get<CBaseEntity>() == &b, "far cache chosen from actual candidates");
        Check(state.distantTarget.Get<CBaseEntity>() == selected.Get<CBaseEntity>(), "far choice saved in persistent target cache");
        a.pev->origin.x = 1600; b.pev->origin.x = 800;
        Select(f, state, 102);
        Check(state.target.Get<CBaseEntity>() == selected.Get<CBaseEntity>(), "far cache survives subsequent nearest-order change");
        a.pev->origin.x = 20;
        Select(f, state, 103);
        Check(state.target.Get<CBaseEntity>() == &a && !state.distantTarget.Get<CBaseEntity>(),
            "near candidate clears distant cache and restores nearest choice");
    }
    for (int roll : {15, 16})
    {
        Fixture f; auto& state = Prepare(f);
        auto& follower = f.engine.AddTarget(500, "fixture_follower");
        follower.pev->classname = Engine::AllocString("zs_follower");
        follower.pev->health = 100; follower.pev->deadflag = DEAD_NO; follower.pev->origin = Vector(500,0,0);
        f.randomLong = roll; const int traces = f.traces, randomCalls = f.randomCalls;
        Select(f, state, 101);
        Check(f.randomCalls == randomCalls + 1, "follower RNG called even with zero player candidates");
        Check((state.target.Get<CBaseEntity>() == &follower) == (roll == 15),
            "inclusive follower RNG boundary is 15 with clear actual trace");
        Check(f.traces - traces == (roll == 15 ? 1 : 0), "only follower candidate takes visibility trace");
    }
    {
        Fixture f; auto& state = Prepare(f); Player(f, 1, 25);
        const float status = f.Monster().HostState().statusUpdateAt;
        f.services->onExcluded = [&]
        {
            f.services->onExcluded = {};
            Require(f.Reset(), "target feature callback may reset the same lease");
        };
        Select(f, state, 101);
        Check(f.Monster().HostState().statusUpdateAt == status && f.Behavior().Error().message.empty(),
            "target scan cancels after same-lease reset before reading old selection state");
    }
}
}
