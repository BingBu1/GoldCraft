// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/skull3.hpp"
#include <cstdio>

// Expected values are supplied by execution of original MP instructions.
// Compare bits so a change in precision or accidental FMA remains visible.
int main()
{
    char kind;
    int cases = 0, checks = 0, failures = 0;
    auto check = [&](bool value) { ++checks; failures += !value; };
    auto bits = [](float value) { return std::bit_cast<std::uint32_t>(value); };
    while (std::scanf(" %c", &kind) == 1)
    {
        int dual;
        if (std::scanf("%d", &dual) != 1) return 2;
        if (kind == 'A')
        {
            int shots; unsigned expected;
            if (std::scanf("%d %u", &shots, &expected) != 2) return 2;
            check(bits(csol::skull3::AccuracyAfterTrigger(dual != 0, shots)) == expected);
        }
        else if (kind == 'S')
        {
            unsigned accuracy, expected; int grounded, forcedAirborne, forcedStanding;
            if (std::scanf("%u %d %d %d %u", &accuracy, &grounded, &forcedAirborne,
                &forcedStanding, &expected) != 5) return 2;
            check(bits(csol::skull3::Spread(dual != 0, std::bit_cast<float>(accuracy),
                grounded != 0, forcedAirborne != 0, forcedStanding != 0)) == expected);
        }
        else if (kind == 'R')
        {
            int grounded, moving, ducking, directionChange; unsigned expected[6];
            if (std::scanf("%d %d %d %u %u %u %u %u %u %d", &grounded, &moving,
                &ducking, &expected[0], &expected[1], &expected[2], &expected[3],
                &expected[4], &expected[5], &directionChange) != 10) return 2;
            const auto kick = csol::skull3::Recoil(dual != 0, grounded != 0, moving != 0, ducking != 0);
            const float actual[]{kick.upBase, kick.lateralBase, kick.upModifier,
                kick.lateralModifier, kick.upMaximum, kick.lateralMaximum};
            for (int i = 0; i < 6; ++i) check(bits(actual[i]) == expected[i]);
            check(kick.directionChange == directionChange);
        }
        else if (kind == 'P')
        {
            unsigned expected[6]; int penetration;
            if (std::scanf("%u %u %u %u %u %u %d", &expected[0], &expected[1],
                &expected[2], &expected[3], &expected[4], &expected[5], &penetration) != 7) return 2;
            const auto p = csol::skull3::Parameters(dual != 0);
            const float actual[]{p.interval, p.reloadTime, p.rangeModifier,
                p.accuracyAdd, p.accuracyLimit, p.standSpread};
            for (int i = 0; i < 6; ++i) check(bits(actual[i]) == expected[i]);
            check(p.penetration == penetration);
        }
        else return 2;
        ++cases;
    }
    if (!cases) ++failures;
    std::printf("{\"cases\":%d,\"checks\":%d,\"failures\":%d}\n", cases, checks, failures);
    return failures != 0;
}
