// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "weapon_damage_runtime.h"
#include "weapon_damage_file.h"

namespace
{
CCSOLBeamDamageConfig beamDamage;
constexpr char DamageFile[] = "csol/weapons/beamgun_damage.json";

void ReloadDamageCommand()
{
    if (CMD_ARGC() != 1)
    {
        SERVER_PRINT("CSOL: usage: csol_reload_beam_damage (server console only)\n");
        return;
    }
    CSOL_ReloadBeamDamage();
}
}

void CSOL_InitBeamDamage()
{
    ADD_SERVER_COMMAND("csol_reload_beam_damage", ReloadDamageCommand);
}

bool CSOL_ReloadBeamDamage()
{
    // GET_GAME_DIR is supplied by the dedicated engine, not a client command.
    // Keep one fixed file under that server's game directory.
    char gameDirectory[512]{};
    GET_GAME_DIR(gameDirectory);
    gameDirectory[sizeof(gameDirectory) - 1] = '\0';
    std::string error;
    bool loaded = false;
    try
    {
        if (gameDirectory[0])
            loaded = CSOL_LoadBeamDamageFile(std::filesystem::path(gameDirectory) / DamageFile, beamDamage, error);
        else
            error = "engine did not provide a game directory";
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
    }
    if (loaded)
        SERVER_PRINT("CSOL: loaded csol/weapons/beamgun_damage.json for beamgun and beamgunle.\n");
    else
        ALERT(at_console, "CSOL: cannot load %s: %s; %s.\n", DamageFile, error.c_str(),
            beamDamage.IsLoaded() ? "retaining the previous valid damage configuration" : "beam weapons remain unavailable");
    return loaded;
}

bool CSOL_BeamDamageReady()
{
    return beamDamage.IsLoaded();
}

float CSOL_BeamDamage(CSOLBeamVariant variant, CSOLDamageMode mode, CSOLBeamDamage attack)
{
    return beamDamage.Get(variant, mode, attack);
}
