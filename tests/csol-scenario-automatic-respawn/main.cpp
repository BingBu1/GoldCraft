// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/scenario/automatic_respawn.hpp"
#include <array>
#include <cstdio>
#include <limits>

namespace respawn = csol::scenario::respawn;
namespace
{
unsigned checks{}, failures{};
void Check(bool value, const char* reason)
{
    ++checks;
    if (!value) { ++failures; std::printf("FAIL %u: %s\n", checks, reason); }
}
using Clock = respawn::Clock;
using Action = Clock::Action;

void OriginalDelay()
{
    struct Example { int level; float factor, expected; };
    constexpr Example cases[]{{1, 1, 15}, {10, 1, 15}, {11, 1, 20},
        {20, 1, 20}, {21, 1, 25}, {90, 1, 55}, {91, 1, 60},
        {101, 2, 120}, {2147483647, 1, 60}, {11, 0.5f, 10}, {1, 0, 0}};
    for (const auto& example : cases)
    {
        float value = -1;
        Check(respawn::Delay(example.level, example.factor, value) && value == example.expected,
            "original level/factor boundary and cap order");
    }
    float value = -1;
    const float infinity = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    Check(!respawn::Delay(0, 1, value), "zero base level has no inferred delay");
    Check(!respawn::Delay(-1, 1, value), "negative base level denied");
    Check(!respawn::Delay(1, -1, value), "negative difficulty denied");
    Check(!respawn::Delay(1, infinity, value), "infinite difficulty denied");
    Check(!respawn::Delay(1, nan, value), "NaN difficulty denied");
    Check(!respawn::Delay(91, std::numeric_limits<float>::max(), value), "overflow delay denied");
    constexpr bool table[]{false, true, true, true, true, false, true, false, false, false, true, false};
    for (int subtype = 0; subtype < 12; ++subtype)
        Check(respawn::TimedSubtype(subtype) == table[subtype], "original two-level subtype table");
    Check(!respawn::TimedSubtype(-1), "negative subtype denied");
}

void TimeOrigins()
{
    Clock uninitialized;
    Check(uninitialized.Poll(600, 15).action == Action::None, "map uptime does not invent a death");
    Check(!uninitialized.Claim(0), "uninitialized life has no spawn claim");

    Clock late;
    Check(late.Begin(600), "accepted selection arms late join at time600");
    Check(!late.Claim(late.Generation()), "Begin alone cannot authorize an early spawn");
    auto step = late.Poll(603, 15);
    Check(step.action == Action::Countdown && step.seconds == 12,
        "JOINED processing three seconds later preserves choice timestamp");
    Check(late.Poll(603.5f, 15).action == Action::None, "notice once per second");
    step = late.Poll(604, 15);
    Check(step.action == Action::Countdown && step.seconds == 11, "next original countdown boundary");
    step = late.Poll(std::nextafter(615.0f, 0.0f), 15);
    Check(step.action != Action::Spawn, "fraction before deadline cannot spawn");
    step = late.Poll(615, 15);
    Check(step.action == Action::Spawn && late.Claim(step.generation), "exact original deadline spawns");
    Check(late.Poll(615, 15).action == Action::None && late.Poll(700, 15).action == Action::None,
        "callback reentry and later frames cannot spawn same life");
    Check(!late.Claim(step.generation), "same authorization consumed once");

    Clock died;
    Check(died.Begin(602), "death animation completion is explicit clock origin");
    Check(died.Poll(615, 15).action == Action::Countdown, "Killed at600 does not advance dead-time deadline");
    Check(died.Poll(617, 15).action == Action::Spawn, "ordinary death expires from dead-time602");

    Clock zero;
    Check(zero.Begin(0), "time zero is a valid initialized lifecycle");
    Check(zero.Poll(14, 15).action == Action::Countdown, "time-zero death remains waiting");
    Check(zero.Poll(15, 15).action == Action::Spawn, "time-zero death expires normally");
    Clock fixed;
    Check(fixed.Begin(700), "fixed original path initialized");
    Check(fixed.Poll(709, 10).action == Action::Countdown && fixed.Poll(710, 10).action == Action::Spawn,
        "host-selected fixed ten seconds has exact boundary");
    Clock immediate;
    Check(immediate.Begin(50) && immediate.Poll(50, 0).action == Action::Spawn,
        "legitimate zero difficulty delay expires at origin");
}

void Lifecycle()
{
    Clock clock;
    Check(clock.Begin(600), "first connection waiting life");
    const auto old = clock.Poll(615, 15);
    clock.Cancel();
    Check(!clock.Armed() && !clock.Claim(old.generation), "disconnect/map/round cancellation revokes pending result");
    Check(clock.Poll(1000, 15).action == Action::None, "cancelled result cannot revive on later frame");
    Check(clock.Begin(700), "new connection/life explicitly arms clock");
    Check(clock.Generation() != old.generation && !clock.Claim(old.generation), "stale life generation cannot claim new clock");
    const auto current = clock.Poll(715, 15);
    Check(current.action == Action::Spawn && clock.Claim(current.generation), "current generation claim valid");
    clock.Cancel();
    Check(clock.Begin(800) && clock.Poll(815, 15).action == Action::Spawn, "new legitimate death allowed after spawn invalidation");
    const auto dueGeneration = clock.Generation();
    Check(clock.Poll(799, 15).action == Action::None && !clock.Claim(dueGeneration),
        "invalid later Poll revokes a previously due result");
    Check(clock.Poll(815, 15).action == Action::Spawn && clock.Claim(dueGeneration),
        "fresh due evaluation permits the current lifecycle once");

    Clock suspended;
    Check(suspended.Begin(900), "phase eligibility is owned outside clock");
    // A host may skip Poll when temporarily ineligible; this must not invent
    // a fresh time origin when its original lifecycle becomes eligible again.
    Check(suspended.Poll(916, 15).action == Action::Spawn, "eligibility gaps do not reset or pause original deadline");
    suspended.Cancel();
    Check(suspended.Poll(917, 15).action == Action::None, "failed phase explicitly revoked old lifecycle");
}

void ThirtyTwoIndependentSlots()
{
    std::array<Clock, 33> clocks;
    std::array<std::uint64_t, 33> generations{};
    for (unsigned slot = 1; slot <= 32; ++slot)
        Check(clocks[slot].Begin(1000 + 2.0f * slot), "each slot has its own accepted selection time");
    for (unsigned slot = 1; slot <= 32; ++slot)
    {
        const float origin = 1000 + 2.0f * slot;
        const auto before = clocks[slot].Poll(origin + 14, 15);
        Check(before.action == Action::Countdown && before.seconds == 1, "all32 clocks retain independent countdowns");
        const auto due = clocks[slot].Poll(origin + 15, 15);
        generations[slot] = due.generation;
        Check(due.action == Action::Spawn && clocks[slot].Claim(due.generation), "all32 current lifecycles claim exactly once");
        Check(clocks[slot].Poll(origin + 16, 15).action == Action::None, "claimed slot does not repeat");
    }
    Check(!clocks[0].Armed(), "slot zero remains unused");
    clocks[32].Cancel();
    Check(clocks[32].Begin(2000), "slot32 connection replacement can begin again");
    Check(!clocks[32].Claim(generations[32]), "slot32 old connection result rejected");
    Check(clocks[1].Poll(3000, 15).action == Action::None, "slot32 replacement does not unclaim slot1");
    Check(clocks[32].Poll(2015, 15).action == Action::Spawn, "slot32 replacement keeps own origin");
}

void InvalidClocks()
{
    const float infinity = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    Clock clock;
    Check(!clock.Begin(-1) && !clock.Armed(), "negative lifecycle time denied");
    Check(!clock.Begin(infinity) && !clock.Begin(nan), "non-finite lifecycle times denied");
    Check(clock.Begin(100), "valid lifecycle restored after rejected origin");
    for (float now : {99.0f, infinity, nan})
        Check(clock.Poll(now, 15).action == Action::None, "backward/non-finite current time denied");
    for (float delay : {-1.0f, infinity, nan})
        Check(clock.Poll(100, delay).action == Action::None, "invalid delay does not authorize spawn");
    Check(clock.Begin(std::numeric_limits<float>::max()), "finite maximum origin accepted");
    Check(clock.Poll(std::numeric_limits<float>::max(), std::numeric_limits<float>::max()).action == Action::None,
        "overflow deadline cannot authorize spawn");
    Check(clock.Begin(0), "large finite delay fixture");
    const auto large = clock.Poll(0, 1.0e20f);
    Check(large.action == Action::Countdown && large.seconds == std::numeric_limits<int>::max(),
        "notice integer saturates without undefined conversion");
    Check(!clock.Begin(-1) && clock.Poll(1.0e20f, 1).action == Action::None,
        "invalid replacement cannot leave old lifecycle armed");
}
}
int main()
{
    OriginalDelay(); TimeOrigins(); Lifecycle(); ThirtyTwoIndependentSlots(); InvalidClocks();
    std::printf("automatic respawn helper: %u checks, %u failures; SDK and live game not exercised\n", checks, failures);
    return failures ? 1 : 0;
}
