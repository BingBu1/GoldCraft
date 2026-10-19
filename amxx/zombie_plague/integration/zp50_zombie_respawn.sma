#include <amxmodx>
#include <reapi>
#include <zp50_gamemodes>
#include <zp50_deathmatch>

const TASK_RESPAWN = 100;
const TASK_PROGRESS = 200;
new gZombieDelay, gHumanDelay;
new gUserId[MAX_PLAYERS + 1], gDuration[MAX_PLAYERS + 1];
new Float:gStarted[MAX_PLAYERS + 1];
new gRespawnPolicy;
new bool:gCheckingPolicy[MAX_PLAYERS + 1];
new bool:gRestarting;

public plugin_init()
{
    register_plugin("Zombie Plague Respawn", "1.1.1", "GoldCraft contributors");
    register_dictionary("zp50_zombie_respawn.txt");
    gZombieDelay = create_cvar("zp_zombie_respawn_delay", "3", FCVAR_NONE,
        "Seconds before a dead zombie respawns; 0 disables", true, 0.0, true, 60.0);
    gHumanDelay = create_cvar("zp_human_respawn_delay", "3", FCVAR_NONE,
        "Seconds before a dead human respawns as a zombie; 0 disables", true, 0.0, true, 60.0);
    RegisterHookChain(RG_CBasePlayer_Killed, "PlayerKilledPost", true);
    RegisterHookChain(RG_CBasePlayer_Spawn, "PlayerSpawnPost", true);
    RegisterHookChain(RG_CBasePlayer_RoundRespawn, "RoundRespawnPre", false);
    RegisterHookChain(RG_CSGameRules_RestartRound, "RestartRoundPre", false);
    RegisterHookChain(RG_CSGameRules_RestartRound, "RestartRoundPost", true);
    // Use the same ET_CONTINUE veto contract as zp50_deathmatch. Only the
    // selected mode is unpaused, so custom modes participate without a name list.
    gRespawnPolicy = CreateMultiForward("zp_fw_deathmatch_respawn_pre", ET_CONTINUE, FP_CELL);
}

public PlayerKilledPost(id, attacker, shouldgib)
{
    Cancel(id);
    if (!is_user_connected(id) || is_user_hltv(id) || is_user_alive(id) || !ActiveRound()) return;
    new TeamName:team = get_member(id, m_iTeam);
    if (team != TEAM_TERRORIST && team != TEAM_CT) return;
    new delay = get_pcvar_num(zp_core_is_zombie(id) ? gZombieDelay : gHumanDelay);
    if (!delay || !ModeAllowsRespawn(id)) return;

    gUserId[id] = get_user_userid(id);
    gDuration[id] = delay;
    gStarted[id] = get_gametime();
    set_task(float(delay), "Respawn", TASK_RESPAWN + id);
    if (!is_user_bot(id)) {
        client_print(id, print_center, "%L", id, "ZP_ZOMBIE_RESPAWN", delay);
        ShowProgress(TASK_PROGRESS + id);
        // Spectator target changes can clear BarTime; refresh only this player's
        // own countdown, never the living player's or other observers' bars.
        set_task(0.25, "ShowProgress", TASK_PROGRESS + id, _, _, "b");
    }
}

bool:ActiveRound()
{
    return !gRestarting && zp_gamemodes_get_current() != ZP_NO_GAME_MODE && !get_member_game(m_bRoundTerminating);
}

bool:Pending(id)
{
    if (!gUserId[id] || !is_user_connected(id) || get_user_userid(id) != gUserId[id]
        || is_user_alive(id) || !ActiveRound()) return false;
    new TeamName:team = get_member(id, m_iTeam);
    return team == TEAM_TERRORIST || team == TEAM_CT;
}

bool:ModeAllowsRespawn(id)
{
    new result;
    gCheckingPolicy[id] = true;
    ExecuteForward(gRespawnPolicy, result, id);
    gCheckingPolicy[id] = false;
    return result < PLUGIN_HANDLED;
}

public ShowProgress(taskid)
{
    new id = taskid - TASK_PROGRESS;
    if (!Pending(id) || !ModeAllowsRespawn(id)) { Cancel(id); return; }
    new Float:percent = floatclamp((get_gametime() - gStarted[id]) * 100.0 / float(gDuration[id]), 0.0, 100.0);
    rg_send_bartime2(id, gDuration[id], percent, false);
}

public Respawn(taskid)
{
    new id = taskid - TASK_RESPAWN;
    new bool:ready = Pending(id) && ModeAllowsRespawn(id);
    Cancel(id);
    if (!ready) return;
    zp_core_respawn_as_zombie(id, true);
    rg_round_respawn(id);
}

public zp_fw_deathmatch_respawn_pre(id)
{
    if (gCheckingPolicy[id]) return PLUGIN_CONTINUE;
    // If stock deathmatch is enabled too, it must not beat this countdown or
    // respawn the player as human. Its task also stops once our spawn succeeds.
    return Pending(id) ? PLUGIN_HANDLED : PLUGIN_CONTINUE;
}

public RoundRespawnPre(id)
{
    // ZP's worldspawn recovery also schedules a respawn shortly after Spawn.
    // It does not use the deathmatch forward; keep every RoundRespawn caller
    // behind this countdown. Respawn() clears ownership before its own call.
    if (Pending(id)) return HC_SUPERCEDE;
    if (is_user_connected(id) && !is_user_alive(id) && ActiveRound() && !ModeAllowsRespawn(id))
        return HC_SUPERCEDE;
    return HC_CONTINUE;
}

public RestartRoundPre() { gRestarting = true; CancelAll(); return HC_CONTINUE; }
public RestartRoundPost() { gRestarting = false; }

Cancel(id)
{
    remove_task(TASK_RESPAWN + id);
    remove_task(TASK_PROGRESS + id);
    if (gUserId[id] && is_user_connected(id) && get_user_userid(id) == gUserId[id] && !is_user_bot(id))
        rg_send_bartime(id, 0, false);
    gUserId[id] = 0;
}

public PlayerSpawnPost(id) { if (is_user_alive(id)) Cancel(id); }
public client_disconnected(id) { Cancel(id); }
public client_putinserver(id) { Cancel(id); }
public plugin_end() { DestroyForward(gRespawnPolicy); }
public zp_fw_gamemodes_end() { CancelAll(); }
public CancelAll()
{
    for (new id = 1; id <= MaxClients; id++) Cancel(id);
    return HC_CONTINUE;
}
