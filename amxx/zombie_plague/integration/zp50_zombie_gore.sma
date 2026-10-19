#include <amxmodx>
#include <fakemeta>
#include <hamsandwich>
#include <reapi>
#include <zp50_core>

new gBlood, gGibDamage, gGibModel;
new gLife[MAX_PLAYERS + 1];
new Float:gGibRemainder[MAX_PLAYERS + 1];

#include "components/zp50_zombie_hitfx.sma"
#include "components/zp50_zombie_injury.sma"

public plugin_precache()
{
    gGibModel = precache_model("models/hgibs.mdl");
}

public plugin_init()
{
    register_plugin("Zombie Plague Gore", "1.0.1", "GoldCraft contributors");
    gBlood = create_cvar("zp_zombie_blood", "1", FCVAR_NONE,
        "Blood stream on confirmed zombie injury", true, 0.0, true, 1.0);
    gGibDamage = create_cvar("zp_zombie_gib_damage", "1000", FCVAR_NONE,
        "Actual health lost per random flesh gib; 0 disables", true, 0.0, true, 1000000.0);
    InitZombieHitEffects();
    InitZombieInjury();
    RegisterHookChain(RG_CBasePlayer_Spawn, "GoreSpawnPost", true);
}

ResetZombieGore(id)
{
    gLife[id]++;
    gGibRemainder[id] = 0.0;
    gHitTime[id] = -1.0;
}

public client_putinserver(id) { ResetZombieGore(id); }
public client_disconnected(id) { ResetZombieGore(id); }
public zp_fw_core_infect_post(id, attacker) { ResetZombieGore(id); }
public zp_fw_core_cure_post(id, attacker) { ResetZombieGore(id); }
public GoreSpawnPost(id) { ResetZombieGore(id); }

public plugin_end()
{
    ArrayDestroy(gTraceScopes);
    ArrayDestroy(gInjuryScopes);
}
