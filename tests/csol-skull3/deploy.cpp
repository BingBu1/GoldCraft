// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/weapons/skull3.hpp"
#include <cstdio>

// Input comes from execution of the original MP instructions. The fixture
// does not calculate expected clip, reserve, parity, sequences or timings.
int main()
{
    int dual, clip, reserve, position, flags, expectedClip, expectedReserve,
        expectedFlags, expectedSequence, cases = 0, checks = 0, failures = 0;
    float expectedAttack, expectedIdle;
    while (std::scanf("%d %d %d %d %d %d %d %d %d %f %f", &dual, &clip,
        &reserve, &position, &flags, &expectedClip, &expectedReserve,
        &expectedFlags, &expectedSequence, &expectedAttack, &expectedIdle) == 11)
    {
        const auto result = csol::skull3::Deploy(dual != 0, position, clip, reserve, flags);
        const bool equal[] = {clip == expectedClip, reserve == expectedReserve,
            flags == expectedFlags, result.sequence == expectedSequence,
            result.nextAttack == expectedAttack, result.idle == expectedIdle};
        for (bool check : equal) { ++checks; failures += !check; }
        ++cases;
    }
    if (cases == 0) ++failures;
    std::printf("{\"cases\":%d,\"checks\":%d,\"failures\":%d}\n", cases, checks, failures);
    return failures != 0;
}
