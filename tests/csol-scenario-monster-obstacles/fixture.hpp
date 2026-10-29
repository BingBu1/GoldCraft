// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../csol-scenario-monster-events/fixture.hpp"
namespace obstacle_test
{
using model_events_test::Fixture;
using model_events_test::Hook;
using model_events_test::Near;
using death_events_test::Check;
using death_events_test::Require;
void ObstacleCases();
void ObstacleCancellationCases();
}
