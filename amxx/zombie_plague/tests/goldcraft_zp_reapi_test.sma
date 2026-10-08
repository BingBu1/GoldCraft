// Independent local ReHLDS fixture only; not a normal-play plugin.
// Exercises real ReGameDLL player objects and the complete migrated ZP stack.
#include <amxmodx>
#include <fakemeta>
#include <hamsandwich>
#include <reapi>
#include <cstrike>
#include <cs_player_models_api>
#include <cs_teams_api>
#include <cs_maxspeed_api>
#include <zp50_core>
#include <zp50_gamemodes>
#include <zp50_grenade_frost>
#include <zp50_items>
#include <zp50_class_nemesis>
#include <zp50_class_survivor>

new const REPORT[] = "addons/amxmodx/logs/goldcraft-zp-reapi.jsonl";
new gEnabled, gPlayer[4], gChecks, gFailed, gGordonWrites;
new gSpawn[33], gTeamInfo[33], gScoreInfo[33], gLastGib[33];
new gDamageCalls, bool:gDamageOriginal, Float:gPostDamage;
new gTraceCalls, bool:gTraceOriginal;
new bool:gObserve;

public plugin_init()
{
    register_plugin("GoldCraft ZP ReAPI regression", "0.1", "GoldCraft contributors");
    gEnabled = register_cvar("goldcraft_zp_reapi_test", "0");
    register_srvcmd("gc_zp_reapi_create", "CreatePlayers");
    register_srvcmd("gc_zp_reapi_api", "TestApis");
    register_srvcmd("gc_zp_reapi_combat", "TestCombat");
    register_srvcmd("gc_zp_reapi_finish", "TestAfterProtection");
    register_srvcmd("gc_zp_reapi_round", "TestRound");
    RegisterHookChain(RG_CBasePlayer_Spawn, "AfterSpawn", true);
    RegisterHookChain(RG_CBasePlayer_Killed, "AfterKilled", true);
    RegisterHookChain(RG_CBasePlayer_TakeDamage, "AfterDamage", true);
    RegisterHookChain(RG_CBasePlayer_TraceAttack, "AfterTrace", true);
    register_forward(FM_SetClientKeyValue, "ModelWrite");
    register_message(get_user_msgid("TeamInfo"), "TeamMessage");
    register_message(get_user_msgid("ScoreInfo"), "ScoreMessage");
}

Check(const name[], condition)
{
    gChecks++;
    if (!condition) gFailed++;
    new file = fopen(REPORT, "at");
    if (file) {
        fprintf(file, "{^"name^":^"%s^",^"passed^":%d}^n", name, !!condition);
        fclose(file);
    }
    server_print("[GC ZP ReAPI] %s %s", condition ? "PASS" : "FAIL", name);
}

Summary()
{
    server_print("[GC ZP ReAPI] checks=%d failed=%d gordon=%d actors=%d,%d,%d,%d",
        gChecks, gFailed, gGordonWrites, gPlayer[0], gPlayer[1], gPlayer[2], gPlayer[3]);
    return PLUGIN_HANDLED;
}

public CreatePlayers()
{
    if (!get_pcvar_num(gEnabled) || gPlayer[0]) return PLUGIN_HANDLED;
    new file = fopen(REPORT, "wt");
    if (file) fclose(file);
    gObserve = true;
    set_cvar_num("zp_spawn_protection_time", 0);
    set_cvar_num("zp_deathmatch", 0);
    for (new n = 0; n < sizeof gPlayer; n++) {
        new name[32], reject[128];
        formatex(name, charsmax(name), "GC_ZP_ReAPI_%d", n + 1);
        new id = engfunc(EngFunc_CreateFakeClient, name);
        gPlayer[n] = id;
        if (!id) { Check("create_native_fakeclient", false); return Summary(); }
        dllfunc(DLLFunc_ClientConnect, id, name, "127.0.0.1", reject);
        dllfunc(DLLFunc_ClientPutInServer, id);
        set_entvar(id, var_flags, get_entvar(id, var_flags) | FL_FAKECLIENT);
        set_member(id, m_iJoiningState, JOINED);
        set_member(id, m_bJustConnected, false);
        set_member(id, m_iMenu, Menu_OFF);
        rg_set_user_team(id, n == 0 ? TEAM_TERRORIST : TEAM_CT, n == 0 ? MODEL_T_TERROR : MODEL_CT_URBAN);
        rg_round_respawn(id);
        Check("native_fakeclient_spawned", is_user_alive(id) && is_user_bot(id));
    }
    return Summary();
}

public TestApis()
{
    if (!get_pcvar_num(gEnabled) || !is_user_alive(gPlayer[1])) return PLUGIN_HANDLED;
    new id = gPlayer[1], model[32], arg[8];
    read_argv(1, arg, charsmax(arg));
    new updateIndex = str_to_num(arg);
    rg_set_user_team(id, TEAM_CT, MODEL_CT_URBAN);
    new oldIndex = get_member(id, m_modelIndexPlayer);
    cs_set_player_model(id, "zombie_source");
    get_user_info(id, "model", model, charsmax(model));
    Check("custom_model_immediate", equal(model, "zombie_source"));
    new customIndex = engfunc(EngFunc_ModelIndex, "models/player/zombie_source/zombie_source.mdl");
    Check("custom_hitbox_option_respected", get_member(id, m_modelIndexPlayer) == (updateIndex ? customIndex : oldIndex));
    dllfunc(DLLFunc_ClientUserInfoChanged, id, engfunc(EngFunc_GetInfoKeyBuffer, id));
    get_user_info(id, "model", model, charsmax(model));
    Check("custom_model_survives_userinfo_refresh", equal(model, "zombie_source"));

    new oldT = get_member_game(m_iNumTerrorist), oldCT = get_member_game(m_iNumCT);
    new teamMessages = gTeamInfo[id], scoreMessages = gScoreInfo[id];
    cs_set_player_team(id, CS_TEAM_T, false);
    Check("team_member_and_counts", get_member(id, m_iTeam) == TEAM_TERRORIST &&
        get_member_game(m_iNumTerrorist) == oldT + 1 && get_member_game(m_iNumCT) == oldCT - 1);
    Check("team_silent_option", gTeamInfo[id] == teamMessages && gScoreInfo[id] == scoreMessages);
    cs_set_player_team(id, CS_TEAM_T, false);
    Check("repeated_team_does_not_drift_counts", get_member_game(m_iNumTerrorist) == oldT + 1 && get_member_game(m_iNumCT) == oldCT - 1);
    cs_set_player_team(id, CS_TEAM_CT, true);
    Check("team_notifications", gTeamInfo[id] == teamMessages + 1 && gScoreInfo[id] == scoreMessages + 1);
    Check("amxx_team_cache", get_user_team(id) == 2);
    get_user_info(id, "model", model, charsmax(model));
    Check("team_change_retains_class_model", equal(model, "zombie_source"));
    cs_reset_player_model(id);
    get_user_info(id, "model", model, charsmax(model));
    Check("reset_restores_selected_team_model_immediately", equal(model, "urban"));
    Check("reset_hitbox_option_respected", !updateIndex || get_member(id, m_modelIndexPlayer) == engfunc(EngFunc_ModelIndex, "models/player/urban/urban.mdl"));
    Check("no_transient_gordon_model", !gGordonWrites);

    new bool:frozen = bool:get_member_game(m_bFreezePeriod);
    set_member_game(m_bFreezePeriod, false);
    cs_reset_player_maxspeed(id);
    new Float:baseline = get_entvar(id, var_maxspeed);
    cs_set_player_maxspeed(id, 350.0, false);
    Check("absolute_speed", floatabs(Float:get_entvar(id, var_maxspeed) - 350.0) < 0.01);
    cs_set_player_maxspeed(id, 1.25, true);
    Check("speed_multiplier", floatabs(Float:get_entvar(id, var_maxspeed) - baseline * 1.25) < 0.01);
    rg_reset_maxspeed(id);
    Check("speed_multiplier_does_not_accumulate", floatabs(Float:get_entvar(id, var_maxspeed) - baseline * 1.25) < 0.01);
    set_member_game(m_bFreezePeriod, true);
    rg_reset_maxspeed(id);
    Check("round_freeze_keeps_native_speed", Float:get_entvar(id, var_maxspeed) <= 1.0);
    set_member_game(m_bFreezePeriod, false);
    rg_reset_maxspeed(id);
    Check("custom_speed_returns_after_freeze", floatabs(Float:get_entvar(id, var_maxspeed) - baseline * 1.25) < 0.01);
    cs_reset_player_maxspeed(id);
    Check("speed_reset", floatabs(Float:get_entvar(id, var_maxspeed) - baseline) < 0.01);
    set_member_game(m_bFreezePeriod, frozen);
    return Summary();
}

Human(id)
{
    if (!is_user_alive(id)) rg_round_respawn(id);
    zp_core_force_cure(id);
    set_entvar(id, var_health, 500.0);
    rg_set_user_armor(id, 0, ARMOR_NONE);
    set_member(id, m_LastHitGroup, HIT_GENERIC);
}

Damage(victim, attacker, Float:amount)
{
    gDamageCalls = 0;
    gDamageOriginal = false;
    gPostDamage = -1.0;
    // The real virtual TakeDamage invokes ReGameDLL and all ReAPI hooks.
    ExecuteHamB(Ham_TakeDamage, victim, attacker, attacker, amount, DMG_GENERIC);
}

TraceHit(victim, attacker, Float:amount, hitgroup)
{
    new trace = create_tr2(), Float:end[3];
    get_entvar(victim, var_origin, end);
    set_tr2(trace, TR_pHit, victim);
    set_tr2(trace, TR_iHitgroup, hitgroup);
    set_tr2(trace, TR_vecEndPos, end);
    gTraceCalls = 0; gTraceOriginal = false;
    rg_multidmg_clear();
    // A controlled trace result exercises the actual TraceAttack ABI and native
    // multidamage application. It is not evidence of ray/world collision.
    ExecuteHamB(Ham_TraceAttack, victim, attacker, amount, Float:{1.0, 0.0, 0.0}, trace, DMG_BULLET);
    rg_multidmg_apply(attacker, attacker);
    free_tr2(trace);
}

public TestCombat()
{
    if (!get_pcvar_num(gEnabled) || !is_user_alive(gPlayer[0])) return PLUGIN_HANDLED;
    set_cvar_float("zp_zombie_defense", 0.75);
    set_cvar_num("zp_spawn_protection_time", 0);
    set_cvar_num("zp_human_armor_protect", 1);
    new z = gPlayer[0], h = gPlayer[1], helper = gPlayer[2];
    if (zp_gamemodes_get_current() < 0)
        Check("infection_mode_started", zp_gamemodes_start(zp_gamemodes_get_id("Infection Mode"), z));
    Check("real_infection_mode_active", zp_gamemodes_get_current() == zp_gamemodes_get_id("Infection Mode"));
    for (new n = 0; n < sizeof gPlayer; n++) Human(gPlayer[n]);
    zp_core_force_infect(z);
    set_entvar(z, var_health, 1000.0);
    new Float:health = get_entvar(z, var_health);
    Damage(z, helper, 40.0);
    Check("human_damage_reaches_native_game", floatabs(health - Float:get_entvar(z, var_health) - 30.0) < 0.01 && gDamageOriginal && gDamageCalls == 1);

    health = get_entvar(z, var_health);
    set_cvar_num("zp_zombie_hitzones", 0);
    TraceHit(z, helper, 40.0, HIT_CHEST);
    Check("native_traceattack_and_multidamage", Float:get_entvar(z, var_health) < health && gTraceOriginal && gTraceCalls == 1);
    health = get_entvar(z, var_health);
    set_cvar_num("zp_zombie_hitzones", 1 << HIT_HEAD);
    TraceHit(z, helper, 40.0, HIT_CHEST);
    Check("disallowed_hitzone_blocks_traceattack", Float:get_entvar(z, var_health) == health && !gTraceOriginal);
    TraceHit(z, helper, 40.0, HIT_HEAD);
    Check("allowed_hitzone_reaches_traceattack", Float:get_entvar(z, var_health) < health && gTraceOriginal);
    set_cvar_num("zp_zombie_hitzones", 0);

    health = get_entvar(h, var_health);
    Damage(h, helper, 40.0);
    Check("friendly_damage_blocked", Float:get_entvar(h, var_health) == health && !gDamageOriginal && gPostDamage == 0.0);

    rg_set_user_armor(h, 60, ARMOR_VESTHELM);
    Damage(h, z, 25.0);
    Check("armor_blocks_damage_and_infection", !zp_core_is_zombie(h) && Float:get_entvar(h, var_health) == health && Float:get_entvar(h, var_armorvalue) == 35.0 && !gDamageOriginal && gPostDamage == 0.0);
    rg_set_user_armor(h, 10, ARMOR_VESTHELM);
    Damage(h, z, 25.0);
    Check("armor_breaking_hit_still_blocks_infection", !zp_core_is_zombie(h) && Float:get_entvar(h, var_armorvalue) == 0.0);
    Damage(h, z, 25.0);
    new model[32]; get_user_info(h, "model", model, charsmax(model));
    Check("unarmored_hit_infects_and_changes_team_model", zp_core_is_zombie(h) && get_member(h, m_iTeam) == TEAM_TERRORIST && equal(model, "zombie_source"));
    Check("infection_does_not_apply_native_hit_twice", !gDamageOriginal && gPostDamage == 0.0);
    Human(h);
    get_user_info(h, "model", model, charsmax(model));
    Check("cure_resets_team_model", !zp_core_is_zombie(h) && get_member(h, m_iTeam) == TEAM_CT && !equal(model, "zombie_source") && !equal(model, "gordon"));

    health = get_entvar(z, var_health);
    zp_grenade_frost_set(z, true);
    rg_reset_maxspeed(z);
    Check("frost_limits_real_maxspeed", zp_grenade_frost_get(z) && Float:get_entvar(z, var_maxspeed) <= 1.0);
    Damage(z, helper, 40.0);
    Check("frost_blocks_damage", Float:get_entvar(z, var_health) == health && !gDamageOriginal);
    zp_grenade_frost_set(z, false);
    Check("thaw_restores_class_speed", !zp_grenade_frost_get(z) && Float:get_entvar(z, var_maxspeed) > 1.0);

    Check("madness_purchase", zp_items_force_buy(z, zp_items_get_id("Zombie Madness"), true));
    health = get_entvar(z, var_health);
    Damage(z, helper, 40.0);
    Check("madness_blocks_damage", Float:get_entvar(z, var_health) == health && !gDamageOriginal);
    Human(z); zp_core_force_infect(z);
    health = get_entvar(z, var_health);
    Damage(z, helper, 40.0);
    Check("cure_clears_madness", Float:get_entvar(z, var_health) < health && gDamageOriginal);

    zp_class_nemesis_set(z);
    Check("nemesis_class_applied", zp_class_nemesis_get(z) && zp_core_is_zombie(z));
    set_cvar_num("zp_nemesis_kill_explode", 1);
    ExecuteHamB(Ham_Killed, z, helper, 0);
    Check("nemesis_gib_argument", !is_user_alive(z) && gLastGib[z] == 2);
    new spawned = gSpawn[z]; rg_round_respawn(z);
    Check("one_spawn_callback_for_native_bot", gSpawn[z] == spawned + 1);
    Check("respawn_clears_nemesis", is_user_alive(z) && !zp_class_nemesis_get(z) && !zp_core_is_zombie(z));
    zp_class_survivor_set(h);
    new weapon = get_member(h, m_pActiveItem);
    Check("survivor_class_and_weapon", zp_class_survivor_get(h) && !zp_core_is_zombie(h) && !is_nullent(weapon) && get_member(weapon, m_iId) == WEAPON_M249);
    ExecuteHamB(Ham_Killed, h, helper, 0); rg_round_respawn(h);
    Check("respawn_clears_survivor", is_user_alive(h) && !zp_class_survivor_get(h));

    zp_core_force_infect(z);
    set_cvar_num("zp_spawn_protection_humans", 1);
    set_cvar_float("zp_spawn_protection_time", 1.0);
    rg_round_respawn(h);
    health = get_entvar(h, var_health);
    TraceHit(h, z, 25.0, HIT_CHEST);
    Check("spawn_protection_blocks_traceattack", !zp_core_is_zombie(h) && Float:get_entvar(h, var_health) == health && !gTraceOriginal);
    Damage(h, z, 25.0);
    Check("spawn_protection_blocks_infection", !zp_core_is_zombie(h) && Float:get_entvar(h, var_health) == health && !gDamageOriginal);
    return Summary();
}

public TestAfterProtection()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    new h = gPlayer[1], z = gPlayer[0];
    Damage(h, z, 25.0);
    Check("spawn_protection_expires", zp_core_is_zombie(h));
    Check("model_updates_never_used_gordon", !gGordonWrites);
    return Summary();
}

public TestRound()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    new model[32];
    for (new n = 0; n < sizeof gPlayer; n++) {
        new id = gPlayer[n]; get_user_info(id, "model", model, charsmax(model));
        Check("actual_round_respawn_restores_human_model", is_user_alive(id) && !zp_core_is_zombie(id) && !equal(model, "zombie_source") && !equal(model, "gordon"));
    }
    Check("round_model_updates_never_used_gordon", !gGordonWrites);
    return Summary();
}

public AfterSpawn(id) { gSpawn[id]++; }
public AfterKilled(id, attacker, gib) { gLastGib[id] = gib; }
public AfterDamage(id, inflictor, attacker, Float:damage, bits)
{
    if (gObserve) {
        gDamageCalls++;
        gDamageOriginal = IsReapiHookOriginalWasCalled(RG_CBasePlayer_TakeDamage);
        gPostDamage = damage;
    }
}
public AfterTrace(id, attacker, Float:damage, Float:direction[3], trace, bits)
{
    if (gObserve) {
        gTraceCalls++;
        gTraceOriginal = IsReapiHookOriginalWasCalled(RG_CBasePlayer_TraceAttack);
    }
}
public ModelWrite(id, const buffer[], const key[], const value[])
{
    if (gObserve && equal(key, "model") && equal(value, "gordon")) gGordonWrites++;
    return FMRES_IGNORED;
}
public TeamMessage() { if (gObserve) gTeamInfo[get_msg_arg_int(1)]++; }
public ScoreMessage() { if (gObserve) gScoreInfo[get_msg_arg_int(1)]++; }
