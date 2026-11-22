// SPDX-License-Identifier: GPL-3.0-or-later
#include "../csol-scenario-rules-session/sdk_engine.hpp"
#include "../../native/csol/server/scenario/monsters/scenario_monster_pool.h"

using namespace sdk_test;

namespace {
void CheckPush(CCSOLScenarioMonster& monster, float speed, float recovery, bool hitWall)
{
    const float start = gpGlobals->time;
    monster.pev->velocity = Vector(0, 0, -5);
    monster.ApplyCofMeleeControl(Vector(3, 4, 120), speed, recovery);
    Check(std::abs(monster.pev->velocity.Length2D() - speed) < .001f, "requested horizontal speed");
    Check(monster.pev->velocity.z == -5, "push preserves vertical velocity");
    Check(monster.UpdateCofMeleeControl(start + .01f), "AI held during horizontal push");
    const float stopped = start + (hitWall ? .05f : .35f);
    if (hitWall) monster.pev->velocity.x = monster.pev->velocity.y = 0;
    Check(monster.UpdateCofMeleeControl(stopped), "stagger starts at wall contact or push timeout");
    Check(monster.pev->velocity.Length2D() == 0 && monster.pev->velocity.z == -5, "stopped without lifting");
    Check(monster.UpdateCofMeleeControl(stopped + recovery - .01f), "full recovery follows the stop");
    Check(!monster.UpdateCofMeleeControl(stopped + recovery), "AI released at recovery deadline");
    gpGlobals->time = stopped + recovery + 1;
}
}

int main()
{
    try {
        auto engine = std::make_unique<Engine>();
        engine->Prepare("0");
        g_engfuncs.pfnGetModelPtr = [](edict_t*) -> void* { return nullptr; };
        auto token = std::make_shared<CSOLScenarioEntityAccess>();
        CCSOLMonsterPool pool;
        Require(pool.BindEntityAccess(token) && pool.Initialize(), "current monster pool");
        csol::scenario::MonsterType type{};
        type.id = type.skill = 11;
        csol::scenario::SpawnPoints points;
        auto* monster = pool.Acquire(type, 7, points);
        Require(monster && pool.Borrowed(*monster), "actual leased monster");
        monster->pev->health = 5000;
        monster->pev->deadflag = DEAD_NO;
        gpGlobals->time = 10;
        for (bool wall : {false, true}) {
            CheckPush(*monster, 500, 1, wall);
            CheckPush(*monster, 200, .5f, wall);
        }
        const float now = gpGlobals->time;
        monster->pev->velocity = Vector(80, 15, 0);
        monster->ApplyCofMeleeControl(Vector(1, 0, 0), 0, 1.5f);
        Check(monster->pev->velocity == Vector(), "pickaxe stops immediately");
        Check(monster->UpdateCofMeleeControl(now + 1.49f), "pickaxe holds for 1.5 seconds");
        Check(!monster->UpdateCofMeleeControl(now + 1.5f), "pickaxe releases AI at 1.5 seconds");
        monster->ApplyCofMeleeControl(Vector(1, 0, 0), 500, 1);
        monster->pev->health = 0;
        Check(!monster->UpdateCofMeleeControl(now + .01f) && !monster->HostState().cofMelee.lease,
              "death retires the previous control state");
        pool.Shutdown();
        std::cout << "checks=" << checks << " failures=" << failures << std::endl;
        return failures ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 2;
    }
}
