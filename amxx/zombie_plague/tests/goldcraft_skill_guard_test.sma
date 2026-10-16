// Opt-in local fixture. Real CBasePlayer/ZP/Pawn paths, controlled fake clients.
#include <amxmodx>
#include <fakemeta>
#include <hamsandwich>
#include <reapi>
#include <goldcraft>
#include <zp50_core>
#include <zp50_gamemodes>

new gEnabled, gPlayers[3];

public plugin_init()
{
    register_plugin("GoldCraft skill guard test", "0.1.0", "GoldCraft contributors");
    gEnabled = create_cvar("mc_skill_guard_test", "0");
    register_srvcmd("gc_skill_fixture", "Command");
    RegisterHookChain(RG_CBasePlayer_Spawn, "AfterSpawn", true);
}

public AfterSpawn(id)
{
    for (new n = 0; n < sizeof gPlayers; n++)
        if (gPlayers[n] == id) set_entvar(id, var_flags, get_entvar(id, var_flags) | FL_FAKECLIENT);
}

stock bool:Ready()
{
    for (new n = 0; n < sizeof gPlayers; n++)
        if (!is_user_connected(gPlayers[n]) || !(get_entvar(gPlayers[n], var_flags) & FL_FAKECLIENT)) return false;
    return true;
}

public client_disconnected(id)
{
    for (new n = 0; n < sizeof gPlayers; n++) if (gPlayers[n] == id) gPlayers[n] = 0;
}

stock Status()
{
    if (!Ready()) return;
    new victim = gPlayers[1];
    server_print("[GC Skill] attacker=%d victim=%d helper=%d alive=%d zombie=%d form=%d flags=%d health=%.6f armor=%.6f mode=%d humans=%d zombies=%d",
        gPlayers[0], victim, gPlayers[2], is_user_alive(victim), zp_core_is_zombie(victim),
        gc_get_form(victim), gc_player_flags(victim), Float:get_entvar(victim, var_health), Float:get_entvar(victim, var_armorvalue),
        zp_gamemodes_get_current(), zp_core_get_human_count(), zp_core_get_zombie_count());
}

public Command()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    new action[20]; read_argv(1, action, charsmax(action));
    if (equal(action, "create")) {
        if (Ready()) { Status(); return PLUGIN_HANDLED; }
        for (new n = 0; n < sizeof gPlayers; n++) {
            if (gPlayers[n]) return PLUGIN_HANDLED;
            new name[32], reject[128]; formatex(name, charsmax(name), "GC_Skill_%d", n);
            new id = engfunc(EngFunc_CreateFakeClient, name);
            if (!id) return PLUGIN_HANDLED;
            gPlayers[n] = id;
            dllfunc(DLLFunc_ClientConnect, id, name, "127.0.0.1", reject);
            dllfunc(DLLFunc_ClientPutInServer, id);
            set_member(id, m_iJoiningState, JOINED);
            set_member(id, m_bJustConnected, false);
            set_member(id, m_iMenu, Menu_OFF);
            rg_set_user_team(id, TEAM_CT, MODEL_CT_URBAN);
            rg_round_respawn(id);
            set_entvar(id, var_flags, get_entvar(id, var_flags) | FL_FAKECLIENT);
        }
    }
    if (!Ready()) return PLUGIN_HANDLED;
    new victim = gPlayers[1], attacker = gPlayers[0];
    if (equal(action, "start")) {
        server_print("[GC Skill] started=%d", zp_gamemodes_start(zp_gamemodes_get_id("Infection Mode"), attacker));
    } else if (equal(action, "reset")) {
        if (!is_user_alive(victim)) rg_round_respawn(victim);
        set_entvar(victim, var_flags, get_entvar(victim, var_flags) | FL_FAKECLIENT);
        zp_core_force_cure(victim);
        set_entvar(victim, var_health, 500.0);
        set_entvar(victim, var_takedamage, DAMAGE_AIM);
        rg_set_user_armor(victim, 0, ARMOR_NONE);
        set_member(victim, m_LastHitGroup, HIT_GENERIC);
        engfunc(EngFunc_SetOrigin, victim, Float:{496.0, 176.0, 36.03125});
        set_entvar(victim, var_velocity, Float:{0.0, 0.0, 0.0});
        if (!zp_core_is_zombie(attacker)) zp_core_force_infect(attacker);
    } else if (equal(action, "form")) {
        new arg[8]; read_argv(2, arg, charsmax(arg)); new form = str_to_num(arg);
        if (form == 0 || form == 1) server_print("[GC Skill] formResult=%d", gc_set_form(victim, form));
    } else if (equal(action, "hit") && is_user_alive(victim) && is_user_alive(attacker)) {
        ExecuteHamB(Ham_TakeDamage, victim, attacker, attacker, 40.0, DMG_SLASH);
    } else if (equal(action, "force") && is_user_alive(victim)) {
        zp_core_force_infect(victim);
    } else if (equal(action, "respawn")) {
        rg_round_respawn(victim);
        set_entvar(victim, var_flags, get_entvar(victim, var_flags) | FL_FAKECLIENT);
    } else if (equal(action, "kill") && is_user_alive(victim)) {
        user_kill(victim, 1);
    }
    Status();
    return PLUGIN_HANDLED;
}
