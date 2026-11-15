// SPDX-License-Identifier: GPL-3.0-or-later
// Links the actual six CSX translation units, also used for the shipped DLL.
#include "rank.h"
#include <array>
#include <cstdarg>
#include <cstdlib>
#include <iostream>

namespace
{
static_assert(sizeof(CPlayer)==0x31d0);
static_assert(offsetof(edict_t,v)+offsetof(entvars_t,owner)==0x218);
std::array<edict_t, 80> entities;
int checks{}, failures{}, forwards{}, lastPlayer{}, lastEntity{}, lastWeapon{};
void Check(bool value, const char* name)
{ ++checks; if (!value) { ++failures; std::cerr << "FAIL " << name << '\n'; } }
int Forward(int id, ...)
{
    va_list args;
    va_start(args, id);
    lastPlayer = va_arg(args, cell); lastEntity = va_arg(args, cell); lastWeapon = va_arg(args, cell);
    va_end(args); ++forwards; return 0;
}
}
int main()
{
    static globalvars_t globals{};
    globals.maxClients = 32;
    gpGlobals = &globals;
    static meta_globals_t meta{};
    gpMetaGlobals = &meta;
    g_engfuncs.pfnIndexOfEdict = [](const edict_t* e) -> int { return static_cast<int>(e-entities.data()); };
    g_fn_ExecuteForward = Forward;
    static cvar_t paused{}, bots{};
    bots.value = 0;
    csstats_pause = &paused; csstats_rankbots = &bots;
    for (int i=1; i<=32; ++i)
    {
        players[i].Init(i, &entities[i]);
        entities[i].v.flags = FL_CLIENT;
    }
    auto& projectile = entities[70];
    const char* he = "models/w_hegrenade.mdl";
    projectile.v.owner = &entities[54];
    SetModel_Post(&projectile, he);
    Check(forwards == 0 && meta.mres == MRES_IGNORED, "dump's NPC54 owner cannot enter the33-player statistics array");
    for (int i : {0, 33, 34, 54, 79})
    {
        projectile.v.owner = &entities[i];
        SetModel_Post(&projectile, he);
        Check(forwards == 0, "non-player owner produces neither player stats nor grenade forwards");
    }
    projectile.v.owner = &entities[1];
    SetModel_Post(&projectile, he);
    Check(forwards == 1 && lastPlayer == 1 && lastEntity == 70 && lastWeapon == CSW_HEGRENADE,
          "real player's HE retains the original grenade forward");
    Check(players[1].weapons[CSW_HEGRENADE].shots == 1 && players[1].life.shots == 1,
          "real player's HE retains original shot accounting");
    SetModel_Post(&projectile, "models/w_flashbang.mdl");
    Check(forwards == 2 && lastWeapon == CSW_FLASHBANG && players[1].life.shots == 1,
          "flash forward retains its original statistics behavior");
    SetModel_Post(&projectile, "models/w_smokegrenade.mdl");
    Check(forwards == 3 && lastWeapon == CSW_SMOKEGRENADE, "smoke forward remains available");
    globals.maxClients = 1;
    projectile.v.owner = &entities[2];
    SetModel_Post(&projectile, he);
    Check(forwards == 3 && players[2].life.shots == 0, "actual server client bound also limits the fixed table");
    paused.value = 1;
    projectile.v.owner = &entities[1];
    SetModel_Post(&projectile, he);
    Check(forwards == 3, "original module pause still disables accounting");
    std::cout << "checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
