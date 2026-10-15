// Local opt-in TNT observer: real ZP identities and native damage callbacks.
// Does not invoke damage or change the production ZP rules.
#include <amxmodx>
#include <fakemeta>
#include <reapi>
#include <zp50_core>
#include <zp50_gamemodes>

new gEnabled, gGuard, gVictim, gCalls, gOriginal, gInfections, gInfectionAttacker;
new gAttacker, gBits, Float:gAmount;

public plugin_init()
{
    register_plugin("GoldCraft TNT ZP observer", "0.1.0", "GoldCraft contributors");
    gEnabled = create_cvar("mc_tnt_zp_test", "0");
    register_srvcmd("gc_tnt_zp_guard", "CreateGuard");
    register_srvcmd("gc_tnt_zp_start", "StartMode");
    register_srvcmd("gc_tnt_zp_identity", "SetIdentity");
    register_srvcmd("gc_tnt_zp_observe", "Observe");
    register_srvcmd("gc_tnt_zp_status", "Status");
    register_srvcmd("gc_tnt_zp_lifecycle", "Lifecycle");
    RegisterHookChain(RG_CBasePlayer_TakeDamage, "AfterDamage", true);
}

stock bool:FixturePlayer(id)
{
    if (!is_user_connected(id) || !(get_entvar(id, var_flags) & FL_FAKECLIENT)) return false;
    new name[32]; get_user_name(id, name, charsmax(name));
    return equal(name, "GC_HeadlessNative") || equal(name, "GC_TNT_Movement") || equal(name, "GC_TNT_ZPGuard");
}

public CreateGuard()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    if (!is_user_connected(gGuard)) {
        gGuard = engfunc(EngFunc_CreateFakeClient, "GC_TNT_ZPGuard");
        if (!gGuard) return PLUGIN_HANDLED;
        new reject[128];
        dllfunc(DLLFunc_ClientConnect, gGuard, "GC_TNT_ZPGuard", "127.0.0.1", reject);
        dllfunc(DLLFunc_ClientPutInServer, gGuard);
        set_member(gGuard, m_iJoiningState, JOINED);
        set_member(gGuard, m_bJustConnected, false);
        set_member(gGuard, m_iMenu, Menu_OFF);
        rg_set_user_team(gGuard, TEAM_CT, MODEL_CT_URBAN);
    }
    if (!is_user_alive(gGuard)) rg_round_respawn(gGuard);
    set_entvar(gGuard, var_flags, get_entvar(gGuard, var_flags) | FL_FAKECLIENT);
    set_entvar(gGuard, var_takedamage, DAMAGE_NO);
    server_print("[GC TNT ZP] guard=%d alive=%d", gGuard, is_user_alive(gGuard));
    return PLUGIN_HANDLED;
}

public client_disconnected(id)
{
    // Never reclaim a disconnected fixture's slot from a later connection.
    if (id == gGuard) gGuard = 0;
    if (id == gVictim) gVictim = 0;
}

public Lifecycle()
{
    if (!get_pcvar_num(gEnabled) || read_argc() != 3) return PLUGIN_HANDLED;
    new text[16]; read_argv(1, text, charsmax(text)); new id = str_to_num(text);
    if (!FixturePlayer(id)) return PLUGIN_HANDLED;
    read_argv(2, text, charsmax(text));
    new userid = get_user_userid(id);
    if (equal(text, "respawn")) {
        rg_round_respawn(id);
        set_entvar(id, var_flags, get_entvar(id, var_flags) | FL_FAKECLIENT);
        set_entvar(id, var_takedamage, DAMAGE_NO);
    } else if (equal(text, "kill")) {
        user_kill(id, 1);
    } else if (equal(text, "disconnect")) {
        server_cmd("kick #%d", userid); server_exec();
    } else return PLUGIN_HANDLED;
    server_print("[GC TNT lifecycle] action=%s slot=%d userid=%d connected=%d alive=%d",
        text, id, userid, is_user_connected(id), is_user_alive(id));
    return PLUGIN_HANDLED;
}

public StartMode()
{
    if (!get_pcvar_num(gEnabled) || !FixturePlayer(gGuard) || !is_user_alive(gGuard)) return PLUGIN_HANDLED;
    // The manager only accepts start while its real round-start task is pending.
    new mode = zp_gamemodes_get_id("Infection Mode");
    new started = zp_gamemodes_start(mode, gGuard);
    server_print("[GC TNT ZP] started=%d mode=%d guardZombie=%d", started, zp_gamemodes_get_current(), zp_core_is_zombie(gGuard));
    return PLUGIN_HANDLED;
}

public SetIdentity()
{
    if (!get_pcvar_num(gEnabled) || read_argc() != 3) return PLUGIN_HANDLED;
    new text[16]; read_argv(1, text, charsmax(text)); new id = str_to_num(text);
    read_argv(2, text, charsmax(text)); new zombie = str_to_num(text);
    if (!FixturePlayer(id) || !is_user_alive(id) || (zombie != 0 && zombie != 1)) return PLUGIN_HANDLED;
    if (zombie) zp_core_force_infect(id); else zp_core_force_cure(id);
    // Read the actual resulting identity: a ZP forward can reject even force APIs.
    server_print("[GC TNT ZP] slot=%d zombie=%d team=%d", id, zp_core_is_zombie(id), get_member(id, m_iTeam));
    return PLUGIN_HANDLED;
}

public Observe()
{
    if (!get_pcvar_num(gEnabled) || read_argc() != 2) return PLUGIN_HANDLED;
    new text[16]; read_argv(1, text, charsmax(text)); new id = str_to_num(text);
    if (!FixturePlayer(id) || !is_user_alive(id)) return PLUGIN_HANDLED;
    gVictim = id;
    gCalls = gOriginal = gInfections = gInfectionAttacker = gAttacker = gBits = 0;
    gAmount = 0.0;
    return Status();
}

public AfterDamage(id, inflictor, attacker, Float:damage, bits)
{
    if (!get_pcvar_num(gEnabled) || id != gVictim || !FixturePlayer(id)) return HC_CONTINUE;
    new source[32];
    if (is_nullent(inflictor)) return HC_CONTINUE;
    get_entvar(inflictor, var_classname, source, charsmax(source));
    if (!equal(source, "goldcraft_tnt")) return HC_CONTINUE;
    gCalls++;
    if (IsReapiHookOriginalWasCalled(RG_CBasePlayer_TakeDamage)) gOriginal++;
    gAttacker = attacker; gBits = bits; gAmount = damage;
    return HC_CONTINUE;
}

public zp_fw_core_infect_post(id, attacker)
{
    if (get_pcvar_num(gEnabled) && id == gVictim && FixturePlayer(id)) {
        gInfections++; gInfectionAttacker = attacker;
    }
}

public Status()
{
    if (!get_pcvar_num(gEnabled) || !FixturePlayer(gVictim)) return PLUGIN_HANDLED;
    server_print("[GC TNT ZP] slot=%d alive=%d zombie=%d team=%d mode=%d infectionAllowed=%d humans=%d zombies=%d calls=%d original=%d infections=%d infectionAttacker=%d attacker=%d bits=%d amount=%.6f",
        gVictim, is_user_alive(gVictim), zp_core_is_zombie(gVictim), get_member(gVictim, m_iTeam),
        zp_gamemodes_get_current(), zp_gamemodes_get_allow_infect(), zp_core_get_human_count(), zp_core_get_zombie_count(),
        gCalls, gOriginal, gInfections, gInfectionAttacker, gAttacker, gBits, gAmount);
    return PLUGIN_HANDLED;
}
