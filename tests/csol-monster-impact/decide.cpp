// SPDX-License-Identifier: GPL-3.0-or-later
// Evaluates the production scenario-monster impact decision for cases read
// from stdin; run.py compares the output with the original-client oracle.
#include "../../native/csol/monster_impact.hpp"
#include <cstdio>
#include <iostream>
#include <string>

namespace mi = csol::monster_impact;

static_assert(mi::BulletSize(0) == 4 && mi::BulletSize(140) == 4 && mi::BulletSize(12) == 6);
static_assert(mi::Metal("models/monster/giant.mdl") && !mi::Metal("models/monster/giant.mdl2"));
static_assert(mi::ClassOf("models/monster/zbs_bossL_big08.mdl") == mi::ModelClass::Shielded);
static_assert(mi::ClassOf("models/player/zg_goliath/zg_goliath2.mdl") == mi::ModelClass::Bloodless);

int main()
{
    int failures = 0;
    const auto expect = [&](bool ok, const char* name) { if (!ok) { std::fprintf(stderr, "FAIL %s\n", name); ++failures; } };
    // Production name inputs: the full studio name decides, the truncated
    // 32-byte physent name only supplies the player initial.
    const auto zombie = mi::Classify("models/player/zombi_origin/zomb",
        "models/player/zombi_origin/zombi_origin.mdl", 3, 1, 12);
    expect(zombie == mi::Effect{mi::Kind::Blood, 6}, "ordinary zombie bleeds");
    expect(mi::Classify("models/boss/zbs91_boss.mdl", "models/boss/zbs91_boss.mdl", 3, 2, 12) ==
        mi::Effect{mi::Kind::Spark, 24}, "metal boss sparks");
    expect(mi::Classify("models/monster/zbs_bossL_big08.", "models/monster/zbs_bossL_big08.mdl", 3, 8, 12) ==
        mi::Effect{mi::Kind::Spark, 24}, "class 7 shield sparks");
    expect(mi::Classify("models/monster/zbs_bossL_big08.", "models/monster/zbs_bossL_big08.mdl", 3, 1, 12) ==
        mi::Effect{mi::Kind::Blood, 6}, "class 7 flesh bleeds");
    expect(mi::Classify("models/player/zg_goliath/zg_gol", "models/player/zg_goliath/zg_goliath2.mdl", 3, 1, 12) ==
        mi::Effect{}, "class 8 suppressed");
    expect(mi::Classify("player", "models/player/zombi_origin/zombi_origin.mdl", 3, 1, 12) == mi::Effect{},
        "player name suppressed");
    expect(mi::Classify("models/x.mdl", "models/x.mdl", 4, 1, 12) == mi::Effect{}, "BSP excluded");
    expect(mi::Classify("models/x.mdl", "models/x.mdl", 2, 1, 12) == mi::Effect{}, "bbox excluded");
    expect(mi::Classify("models/x.mdl", "models/x.mdl", 3, 1, 23) == mi::Effect{mi::Kind::Blood, 4}, "bullet 23");

    // Oracle mode encoding: 3 denotes scenario mode, not a production marker.
    // stdin: scenario-mode initial metal class solid hitgroup bullet
    int marker{}, initial{}, metal{}, modelClass{}, solid{}, hitgroup{}, bullet{};
    while (std::cin >> marker >> initial >> metal >> modelClass >> solid >> hitgroup >> bullet)
    {
        const auto cls = modelClass == 7 ? mi::ModelClass::Shielded :
            modelClass == 8 ? mi::ModelClass::Bloodless : mi::ModelClass::Other;
        const auto effect = marker == 3 ?
            mi::Decide(static_cast<char>(initial), metal != 0, cls, solid, hitgroup, bullet) : mi::Effect{};
        const char* kind = effect.kind == mi::Kind::Blood ? "blood" : effect.kind == mi::Kind::Spark ? "spark" : "none";
        std::printf("%s %d\n", kind, effect.amount);
    }
    if (failures) return 1;
    std::fprintf(stderr, "named checks passed\n");
    return 0;
}
