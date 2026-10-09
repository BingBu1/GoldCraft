// Installed only in the independent headless combat test server.
// Uses public ReAPI 5.29 and AMXX interfaces, never private offsets.
#include <amxmodx>
#include <goldcraft_amxx>
#include <fakemeta>
#include <reapi>

new gBot, gEnabled, gDamageCalls, gApplied, gDeaths;
new Float:gBefore, Float:gLastAmount;
new gLastBits, gLastSource[64];

public plugin_init()
{
    register_plugin("GoldCraft headless combat fixtures", "0.1.0", "GoldCraft contributors");
    gEnabled = create_cvar("goldcraft_headless_test", "0");
    register_srvcmd("gc_headless_create", "CreatePlayer");
    register_srvcmd("gc_headless_reset", "ResetPlayer");
    register_srvcmd("gc_headless_fire", "FireBullet");
    register_srvcmd("gc_headless_status", "Status");
    register_srvcmd("gc_headless_select", "SelectPlayer");
    register_srvcmd("gc_headless_team", "Team");
    RegisterHookChain(RG_CBasePlayer_TakeDamage, "BeforeDamage", false);
    RegisterHookChain(RG_CBasePlayer_TakeDamage, "AfterDamage", true);
    RegisterHookChain(RG_CBasePlayer_Killed, "AfterKilled", true);
}

public CreatePlayer()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    new option[16]; read_argv(1, option, charsmax(option));
    if (read_argc() > 1 && !equal(option, "extra")) return PLUGIN_HANDLED;
    if (is_user_connected(gBot) && !equal(option, "extra")) return Status();
    // Real ReHLDS fake-client edict with the ordinary CBasePlayer game lifecycle.
    // It has no Minecraft login, pairing, network client or AI implementation.
    gBot = engfunc(EngFunc_CreateFakeClient, "GC_HeadlessNative");
    if (!gBot) { server_print("[GC headless] create failed"); return PLUGIN_HANDLED; }
    new reject[128];
    dllfunc(DLLFunc_ClientConnect, gBot, "GC_HeadlessNative", "127.0.0.1", reject);
    dllfunc(DLLFunc_ClientPutInServer, gBot);
    set_entvar(gBot, var_flags, get_entvar(gBot, var_flags) | FL_FAKECLIENT);
    set_member(gBot, m_iJoiningState, JOINED);
    set_member(gBot, m_bJustConnected, false);
    set_member(gBot, m_iMenu, Menu_OFF);
    rg_set_user_team(gBot, TEAM_CT, MODEL_CT_URBAN);
    rg_round_respawn(gBot);
    return Status();
}

public SelectPlayer()
{
    if (!get_pcvar_num(gEnabled) || read_argc() != 2) return PLUGIN_HANDLED;
    new who[20]; read_argv(1, who, charsmax(who));
    if (who[0] != '#') return PLUGIN_HANDLED;
    new id = find_player_ex(FindPlayer_MatchUserId, str_to_num(who[1]));
    if (!is_user_connected(id) || !(get_entvar(id, var_flags) & FL_FAKECLIENT)) return PLUGIN_HANDLED;
    gBot = id; return Status();
}
public Team()
{
    if (!get_pcvar_num(gEnabled) || !is_user_connected(gBot) || read_argc() != 2) return PLUGIN_HANDLED;
    new arg[8]; read_argv(1, arg, charsmax(arg)); new side = str_to_num(arg);
    if (side != 1 && side != 2) return PLUGIN_HANDLED;
    rg_set_user_team(gBot, TeamName:side, side == 1 ? MODEL_T_TERROR : MODEL_CT_URBAN);
    return Status();
}

public ResetPlayer()
{
    if (!get_pcvar_num(gEnabled) || !is_user_connected(gBot) || read_argc() != 6) return PLUGIN_HANDLED;
    new arg[32], Float:pos[3];
    for (new i = 0; i < 3; i++) {
        read_argv(i + 1, arg, charsmax(arg)); pos[i] = str_to_float(arg);
        if (floatabs(pos[i]) > 8192.0) return PLUGIN_HANDLED;
    }
    read_argv(4, arg, charsmax(arg)); new Float:health = str_to_float(arg);
    read_argv(5, arg, charsmax(arg)); new armor = str_to_num(arg);
    if (health < 1.0 || health > 100.0 || armor < 0 || armor > 100) return PLUGIN_HANDLED;
    if (!is_user_alive(gBot)) rg_round_respawn(gBot);
    new Float:zero[3];
    gc_relink_origin(gBot, pos); set_entvar(gBot, var_velocity, zero);
    set_entvar(gBot, var_health, health);
    rg_set_user_armor(gBot, armor, armor ? ARMOR_VESTHELM : ARMOR_NONE);
    gDamageCalls = gApplied = gDeaths = 0;
    return Status();
}

public FireBullet()
{
    if (!get_pcvar_num(gEnabled) || !is_user_alive(gBot) || read_argc() != 3) return PLUGIN_HANDLED;
    new arg[32]; read_argv(1, arg, charsmax(arg)); new target = str_to_num(arg);
    read_argv(2, arg, charsmax(arg)); new damage = str_to_num(arg);
    if (is_nullent(target) || damage < 1 || damage > 100) return PLUGIN_HANDLED;
    new classname[64]; get_entvar(target, var_classname, classname, charsmax(classname));
    if (!equal(classname, "goldcraft_object")) return PLUGIN_HANDLED;
    new Float:source[3], Float:eye[3], Float:destination[3], Float:direction[3];
    get_entvar(gBot, var_origin, source); get_entvar(gBot, var_view_ofs, eye);
    get_entvar(target, var_origin, destination);
    new Float:length;
    for (new i = 0; i < 3; i++) {
        source[i] += eye[i]; direction[i] = destination[i] - source[i]; length += direction[i] * direction[i];
    }
    length = floatsqroot(length);
    if (length < 1.0 || length > 4096.0) return PLUGIN_HANDLED;
    for (new i = 0; i < 3; i++) direction[i] /= length;
    // Calls ReGameDLL's actual FireBullets3 -> trace -> TraceAttack -> TakeDamage.
    // This checks native projectile handling, not graphical +attack input.
    rg_fire_bullets3(gBot, gBot, source, direction, 0.0, 4096.0, 1, BULLET_PLAYER_45ACP, damage, 1.0, true, 0);
    server_print("[GC headless] fired slot=%d target=%d damage=%d", gBot, target, damage);
    return PLUGIN_HANDLED;
}

public BeforeDamage(id, inflictor, attacker, Float:damage, bits)
{
    if (id == gBot) {
        gDamageCalls++; gBefore = get_entvar(id, var_health); gLastAmount = damage; gLastBits = bits;
        if (!is_nullent(inflictor)) get_entvar(inflictor, var_classname, gLastSource, charsmax(gLastSource));
    }
    return HC_CONTINUE;
}
public AfterDamage(id, inflictor, attacker, Float:damage, bits)
{
    if (id == gBot && Float:get_entvar(id, var_health) < gBefore) gApplied++;
    return HC_CONTINUE;
}
public AfterKilled(id, attacker, gib)
{
    if (id == gBot) gDeaths++;
    return HC_CONTINUE;
}
public Status()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    if (!is_user_connected(gBot)) { server_print("[GC headless] connected=0"); return PLUGIN_HANDLED; }
    server_print("[GC headless] connected=1 slot=%d userid=%d alive=%d health=%.3f armor=%.3f calls=%d applied=%d deaths=%d amount=%.3f bits=%d source=%s",
        gBot, get_user_userid(gBot), is_user_alive(gBot), Float:get_entvar(gBot, var_health), Float:get_entvar(gBot, var_armorvalue),
        gDamageCalls, gApplied, gDeaths, gLastAmount, gLastBits, gLastSource);
    return PLUGIN_HANDLED;
}
