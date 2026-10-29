// SPDX-License-Identifier: GPL-3.0-or-later
// Retain the existing controlled engine fixture byte-for-byte through include;
// add one explicit post-DispatchSpawn mutation seam for lifecycle validation.
#include "../csol-scenario-point-manager/engine_fixture.h"
#define DispatchSpawn SpawnFixtureBaseDispatch
#include "../csol-scenario-point-manager/engine_fixture.cpp"
#undef DispatchSpawn
namespace spawn_test { std::function<void(edict_t*)> afterDispatch; }
int DispatchSpawn(edict_t* entity)
{
    const auto result=SpawnFixtureBaseDispatch(entity);
    if(spawn_test::afterDispatch) spawn_test::afterDispatch(entity);
    return result;
}
