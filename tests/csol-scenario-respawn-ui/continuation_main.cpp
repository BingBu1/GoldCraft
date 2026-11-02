// SPDX-License-Identifier: GPL-3.0-or-later
// Reuse the existing real continuation regression, including its production
// Continuation dependency. Do not duplicate or approximate its transitions.
#define main PriorConsumablesSuiteMain
#include "../csol-scenario-consumables/main.cpp"
#undef main

int main()
{
    ContinuationTiming();
    std::printf("scenario_retry_continuation checks=%d failures=%d\n",checks,failures);
    return failures?1:0;
}
