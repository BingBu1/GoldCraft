// Isolated authority-channel regression. Do not enable on a normal server.
#include <amxmodx>
#include <goldcraft_amxx>
#include <hamsandwich>

new gEnabled, gPlayer, gHeals, gDamage, gDeaths;

public plugin_init()
{
    register_plugin("GoldCraft health channel test", "1.0", "GoldCraft contributors");
    gEnabled = create_cvar("mc_status_health_test", "0");
    register_srvcmd("gc_status_health", "Command");
    RegisterHookChain(RG_CBasePlayer_TakeHealth, "AfterHeal", true);
    RegisterHookChain(RG_CBasePlayer_TakeDamage, "AfterDamage", true);
    RegisterHookChain(RG_CBasePlayer_Killed, "AfterKilled", true);
}
public AfterHeal(const id, Float:amount, bits) { if (id == gPlayer) gHeals++; }
public AfterDamage(const id, inflictor, attacker, Float:amount, bits) { if (id == gPlayer) gDamage++; }
public AfterKilled(const id, attacker, gib) { if (id == gPlayer) gDeaths++; }

public Command()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    new action[16], first[32], second[32];
    read_argv(1, action, charsmax(action));
    read_argv(2, first, charsmax(first));
    read_argv(3, second, charsmax(second));
    if (equal(action, "create") && !gPlayer) {
        for (new n; n < 2; n++) {
            new name[32], reject[128];
            formatex(name, charsmax(name), "GC_StatusHealth_%d", n);
            new id = engfunc(EngFunc_CreateFakeClient, name);
            if (!id) return PLUGIN_HANDLED;
            dllfunc(DLLFunc_ClientConnect, id, name, "127.0.0.1", reject);
            dllfunc(DLLFunc_ClientPutInServer, id);
            set_entvar(id, var_flags, get_entvar(id, var_flags) | FL_FAKECLIENT);
            set_member(id, m_iJoiningState, JOINED);
            set_member(id, m_bJustConnected, false);
            set_member(id, m_iMenu, Menu_OFF);
            rg_set_user_team(id, n ? TEAM_TERRORIST : TEAM_CT);
            rg_round_respawn(id);
            set_entvar(id, var_flags, get_entvar(id, var_flags) | FL_FROZEN);
            if (!n) gPlayer = id;
        }
    }
    if (!gPlayer || !is_user_connected(gPlayer)) return PLUGIN_HANDLED;
    if (equal(action, "set")) {
        set_entvar(gPlayer, var_max_health, str_to_float(first));
        set_entvar(gPlayer, var_health, str_to_float(second));
        set_entvar(gPlayer, var_takedamage, DAMAGE_AIM);
    }
    else if (equal(action, "heal")) ExecuteHamB(Ham_TakeHealth, gPlayer, str_to_float(first), DMG_GENERIC);
    else if (equal(action, "kill")) gc_set_health(gPlayer, 0);
    else if (equal(action, "respawn")) {
        rg_round_respawn(gPlayer);
        set_entvar(gPlayer, var_flags, get_entvar(gPlayer, var_flags) | FL_FROZEN);
    }
    server_print("[GC StatusHealth] slot=%d alive=%d health=%.3f base=%.3f heals=%d damage=%d deaths=%d",
        gPlayer, is_user_alive(gPlayer), Float:get_entvar(gPlayer, var_health), Float:get_entvar(gPlayer, var_max_health), gHeals, gDamage, gDeaths);
    return PLUGIN_HANDLED;
}
