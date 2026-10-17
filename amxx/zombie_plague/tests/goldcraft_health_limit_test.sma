// Isolated headless regression. Never enable on a normal game server.
#include <amxmodx>
#include <goldcraft_amxx>
#include <hamsandwich>
#include <zp50_core>
#include <zp50_gamemodes>
#include <zp50_class_human>
#include <zp50_class_zombie>
#include <zp50_class_survivor>
#include <zp50_class_nemesis>

new const REPORT[] = "addons/amxmodx/logs/goldcraft-health-limit.jsonl";
new gEnabled, gPlayers[4], gChecks, gFailed, gHealCalls;
new bool:gVeto;

public plugin_init()
{
    register_plugin("GoldCraft class health test", "1.0", "GoldCraft contributors");
    gEnabled = create_cvar("mc_health_limit_test", "0");
    register_srvcmd("gc_health_create", "CreatePlayers");
    register_srvcmd("gc_health_classes", "Classes");
    register_srvcmd("gc_health_mode", "Mode");
    RegisterHookChain(RG_CBasePlayer_TakeHealth, "BeforeHeal");
    RegisterHookChain(RG_CBasePlayer_TakeHealth, "AfterHeal", true);
}

public BeforeHeal(const id, Float:amount, bits)
{
    if (!gVeto) return HC_CONTINUE;
    SetHookChainReturn(ATYPE_INTEGER, 0);
    return HC_SUPERCEDE;
}
public AfterHeal(const id, Float:amount, bits) { gHealCalls++; }

Check(const label[], const assertion[], bool:passed)
{
    gChecks++;
    if (!passed) gFailed++;
    new f = fopen(REPORT, "at");
    if (f) {
        fprintf(f, "{^"name^":^"%s/%s^",^"passed^":%d}^n", label, assertion, passed);
        fclose(f);
    }
    server_print("[GC Health] %s %s/%s", passed ? "PASS" : "FAIL", label, assertion);
}
Summary()
{
    server_print("[GC Health] checks=%d failed=%d", gChecks, gFailed);
    return PLUGIN_HANDLED;
}

public CreatePlayers()
{
    if (!get_pcvar_num(gEnabled) || gPlayers[0]) return PLUGIN_HANDLED;
    delete_file(REPORT);
    for (new n; n < sizeof gPlayers; n++) {
        new name[32], reject[128];
        formatex(name, charsmax(name), "GC_Health_%d", n + 1);
        new id = engfunc(EngFunc_CreateFakeClient, name);
        gPlayers[n] = id;
        if (!id) { Check("setup", "fakeclient", false); return Summary(); }
        dllfunc(DLLFunc_ClientConnect, id, name, "127.0.0.1", reject);
        dllfunc(DLLFunc_ClientPutInServer, id);
        set_entvar(id, var_flags, get_entvar(id, var_flags) | FL_FAKECLIENT);
        set_member(id, m_iJoiningState, JOINED);
        set_member(id, m_bJustConnected, false);
        set_member(id, m_iMenu, Menu_OFF);
        rg_set_user_team(id, n == 0 ? TEAM_TERRORIST : TEAM_CT);
        rg_round_respawn(id);
        // Freeze fixture actors; the tests exercise actual class/heal APIs.
        set_entvar(id, var_flags, get_entvar(id, var_flags) | FL_FROZEN);
        Check("setup", "alive", bool:is_user_alive(id));
    }
    return Summary();
}

Cycle(const id, const label[], const Float:expected)
{
    new Float:hp = get_entvar(id, var_health), Float:maximum = get_entvar(id, var_max_health);
    server_print("[GC Health] role=%s health=%.3f maximum=%.3f expected=%.3f", label, hp, maximum, expected);
    Check(label, "starting_health", floatabs(hp - expected) < 0.01);
    Check(label, "class_maximum", floatabs(maximum - expected) < 0.01);
    set_entvar(id, var_takedamage, DAMAGE_AIM);
    gc_set_health(id, floatround(expected - 25.0));
    Check(label, "loss_keeps_maximum", floatabs(Float:get_entvar(id, var_max_health) - expected) < 0.01);
    new before = gHealCalls;
    ExecuteHamB(Ham_TakeHealth, id, 10.0, DMG_GENERIC);
    Check(label, "native_healing", floatabs(Float:get_entvar(id, var_health) - (expected - 15.0)) < 0.01);
    new Float:beforeVeto = get_entvar(id, var_health);
    gVeto = true;
    ExecuteHamB(Ham_TakeHealth, id, 10.0, DMG_GENERIC);
    gVeto = false;
    Check(label, "reapi_veto", floatabs(Float:get_entvar(id, var_health) - beforeVeto) < 0.01);
    ExecuteHamB(Ham_TakeHealth, id, 100000.0, DMG_GENERIC);
    Check(label, "healing_cap", floatabs(Float:get_entvar(id, var_health) - expected) < 0.01);
    Check(label, "reapi_post", gHealCalls >= before + 2);
    gc_set_health(id, floatround(expected + 50.0));
    Check(label, "reward_health_preserved", floatabs(Float:get_entvar(id, var_health) - (expected + 50.0)) < 0.01);
    Check(label, "reward_keeps_class_cap", floatabs(Float:get_entvar(id, var_max_health) - expected) < 0.01);
    gc_set_health(id, floatround(expected));
}

public Classes()
{
    if (!get_pcvar_num(gEnabled) || !is_user_alive(gPlayers[0])) return PLUGIN_HANDLED;
    new id = gPlayers[0];
    zp_core_force_infect(id);
    Cycle(id, "zombie", float(zp_class_zombie_get_max_health(id, zp_class_zombie_get_current(id))));
    zp_core_force_cure(id);
    Cycle(id, "human", float(zp_class_human_get_max_health(id, zp_class_human_get_current(id))));
    set_cvar_num("zp_nemesis_health", 12000);
    zp_class_nemesis_set(id);
    Cycle(id, "nemesis", 12000.0);
    set_cvar_num("zp_survivor_health", 1200);
    zp_class_survivor_set(id);
    Cycle(id, "survivor", 1200.0);
    // A survivor is already human; cure alone intentionally retains that role.
    zp_core_force_infect(id);
    zp_core_force_cure(id);
    Cycle(id, "cured_survivor", float(zp_class_human_get_max_health(id, zp_class_human_get_current(id))));
    // A native death and real RoundRespawn must establish the human class again.
    gc_set_health(id, 0);
    Check("respawn", "dead", !is_user_alive(id));
    rg_round_respawn(id);
    Cycle(id, "respawn", float(zp_class_human_get_max_health(id, zp_class_human_get_current(id))));
    return Summary();
}

public Mode()
{
    if (!get_pcvar_num(gEnabled) || !gPlayers[0]) return PLUGIN_HANDLED;
    new arg[8], name[32]; read_argv(1, arg, charsmax(arg));
    new variant = str_to_num(arg);
    copy(name, charsmax(name), variant == 0 ? "Infection Mode" : variant == 1 ? "Armageddon Mode" : "Plague Mode");
    set_cvar_num("zp_nemesis_health", 12000);
    set_cvar_num("zp_survivor_health", 1200);
    set_cvar_float("zp_zombie_first_hp_multiplier", 3.0);
    set_cvar_float("zp_armageddon_nem_hp_multi", 0.5);
    set_cvar_float("zp_armageddon_surv_hp_multi", 0.5);
    set_cvar_float("zp_plague_nem_hp_multi", 0.75);
    set_cvar_float("zp_plague_surv_hp_multi", 0.75);
    new mode = zp_gamemodes_get_id(name);
    Check(name, "mode_found", mode != ZP_INVALID_GAME_MODE);
    Check(name, "mode_started", bool:zp_gamemodes_start(mode, gPlayers[0]));
    for (new n; n < sizeof gPlayers; n++) {
        new id = gPlayers[n], label[48];
        new Float:expected;
        if (zp_class_nemesis_get(id)) expected = variant == 1 ? 6000.0 : 9000.0;
        else if (zp_class_survivor_get(id)) expected = variant == 1 ? 600.0 : 900.0;
        else if (zp_core_is_zombie(id)) expected = float(zp_class_zombie_get_max_health(id, zp_class_zombie_get_current(id))) * (variant == 0 ? 3.0 : 1.0);
        else expected = float(zp_class_human_get_max_health(id, zp_class_human_get_current(id)));
        formatex(label, charsmax(label), "mode%d_player%d", variant, n);
        Cycle(id, label, expected);
    }
    return Summary();
}
