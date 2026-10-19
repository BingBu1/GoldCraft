// Independent local ReHLDS fixture only; not a normal-play plugin.
// Exercises real ReGameDLL player objects and the complete migrated ZP stack.
#include <amxmodx>
#include <goldcraft_amxx>
#include <fakemeta>
#include <hamsandwich>
#include <reapi>
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
new gRespawnCounts[2];
new Float:gDeathAt;
new bool:gGoreObserve;
new gGoreBlood, gGoreFlesh, gGoreSparks, gGoreInvalidGibs;
new TeamName:gNotifiedTeam[33];

public plugin_init()
{
    register_plugin("GoldCraft ZP ReAPI regression", "0.1", "GoldCraft contributors");
    gEnabled = create_cvar("goldcraft_zp_reapi_test", "0");
    register_srvcmd("gc_zp_reapi_create", "CreatePlayers");
    register_srvcmd("gc_zp_reapi_api", "TestApis");
    register_srvcmd("gc_zp_reapi_combat", "TestCombat");
    register_srvcmd("gc_zp_reapi_armor", "TestZombieArmor");
    register_srvcmd("gc_zp_reapi_respawn_begin", "BeginZombieRespawn");
    register_srvcmd("gc_zp_reapi_respawn_early", "CheckRespawnEarly");
    register_srvcmd("gc_zp_reapi_respawn_done", "CheckRespawnDone");
    register_srvcmd("gc_zp_reapi_respawn_policy", "BeginRespawnPolicy");
    register_srvcmd("gc_zp_reapi_respawn_denied", "CheckRespawnDenied");
    register_srvcmd("gc_zp_reapi_head_knockback", "TestHeadKnockback");
    register_srvcmd("gc_zp_reapi_gore", "TestZombieGore");
    register_srvcmd("gc_zp_reapi_finish", "TestAfterProtection");
    register_srvcmd("gc_zp_reapi_round", "TestRound");
    register_srvcmd("gc_zp_reapi_team_refresh", "TestTeamRefresh");
    register_srvcmd("gc_zp_reapi_survivor_prepare", "PrepareSurvivor");
    register_srvcmd("gc_zp_reapi_survivor_pending", "TestSurvivorPending");
    register_srvcmd("gc_zp_reapi_survivor_active", "TestSurvivorActive");
    RegisterHookChain(RG_CBasePlayer_Spawn, "AfterSpawn", true);
    RegisterHookChain(RG_CBasePlayer_Killed, "AfterKilled", true);
    RegisterHookChain(RG_CBasePlayer_TakeDamage, "AfterDamage", true);
    RegisterHookChain(RG_CBasePlayer_TraceAttack, "AfterTrace", true);
    register_forward(FM_SetClientKeyValue, "ModelWrite");
    register_message(get_user_msgid("TeamInfo"), "TeamMessage");
    register_message(get_user_msgid("ScoreInfo"), "ScoreMessage");
    RegisterMessage(SVC_TEMPENTITY, "ObserveGoreMessage", true);
    RegisterHookChain(RG_CGib_Spawn, "ObserveGoreGib", true);
}

public ObserveGoreGib(entity, const model[])
{
    if (!gGoreObserve) return;
    gGoreFlesh++;
    if (!is_entity(entity) || get_entvar(entity, var_movetype) != MOVETYPE_BOUNCE
        || (!equal(model, "models/hgibs.mdl") && !equal(model, "models/germangibs.mdl")))
        gGoreInvalidGibs++;
}

public ObserveGoreMessage(message, destination, receiver)
{
    if (!gGoreObserve || GetMessageArgsNum() < 1) return HC_CONTINUE;
    switch (GetMessageData(MsgArg, 1)) {
        case TE_BLOODSTREAM: gGoreBlood++;
        case TE_STREAK_SPLASH: if (GetMessageArgsNum() == 11) gGoreSparks += GetMessageData(MsgArg, 9);
    }
    return HC_CONTINUE;
}

public TestZombieGore()
{
    if (!get_pcvar_num(gEnabled) || !is_user_alive(gPlayer[0])) return PLUGIN_HANDLED;
    new z = gPlayer[0], h = gPlayer[1], other = gPlayer[2];
    if (zp_gamemodes_get_current() < 0) zp_gamemodes_start(zp_gamemodes_get_id("Infection Mode"), z);
    set_cvar_num("zp_spawn_protection_time", 0);
    set_cvar_num("zp_zombie_hitzones", 0);
    new Float:defense = get_cvar_float("zp_zombie_defense");
    new vanillaGibs = get_cvar_num("violence_hgibs");
    new gibChance = get_cvar_num("zp_zombie_gib_chance");
    set_cvar_num("zp_zombie_gib_chance", 100);
    set_cvar_num("violence_hgibs", 0); // Observe injury gibs without vanilla death gibs.
    set_cvar_float("zp_zombie_defense", 1.0);
    for (new n = 0; n < sizeof gPlayer; n++) Human(gPlayer[n]);
    zp_core_force_infect(z);
    gGoreObserve = true; gGoreBlood = gGoreFlesh = gGoreSparks = gGoreInvalidGibs = 0;
    TraceHit(z, h, 40.0, HIT_HEAD);
    Check("gore_helmet_sparks_zero", gGoreSparks == 0);
    Check("gore_armored_head_bleeds", gGoreBlood == 1);
    Check("gore_helmet_and_armor_retained", get_member(z, m_iKevlar) == ARMOR_VESTHELM && Float:get_entvar(z, var_armorvalue) > 0.0);
    // Same native effect outside a player hit must remain untouched.
    new Float:point[3]; get_entvar(z, var_origin, point);
    message_begin_f(MSG_PVS, SVC_TEMPENTITY, point);
    write_byte(TE_STREAK_SPLASH);
    for (new axis = 0; axis < 3; axis++) write_coord_f(point[axis]);
    write_coord(0); write_coord(0); write_coord(1);
    write_byte(5); write_short(22); write_short(25); write_short(65); message_end();
    Check("gore_unrelated_streak_preserved", gGoreSparks == 22);
    zp_core_force_infect(z); rg_set_user_armor(z, 0, ARMOR_NONE); set_entvar(z, var_health, 10000.0);
    gGoreBlood = gGoreFlesh = 0;
    Damage(z, h, 600.0); Damage(z, h, 399.0);
    Check("gore_no_fragment_before_1000", gGoreFlesh == 0 && gGoreBlood == 2);
    Damage(z, h, 1.0);
    Check("gore_fragment_at_1000", gGoreFlesh == 1);
    Damage(z, h, 2500.0); Check("gore_multiple_thresholds", gGoreFlesh == 3);
    Check("gore_native_gibs_do_not_kill_victim", is_user_alive(z));
    zp_core_force_infect(other); rg_set_user_armor(other, 0, ARMOR_NONE); set_entvar(other, var_health, 10000.0);
    Damage(other, h, 999.0); Check("gore_players_have_separate_counters", gGoreFlesh == 3);
    Damage(z, h, 500.0); Check("gore_fractional_remainder_preserved", gGoreFlesh == 4);
    Damage(other, h, 1.0); Check("gore_second_player_threshold", gGoreFlesh == 5);
    new blood = gGoreBlood, flesh = gGoreFlesh;
    set_entvar(z, var_takedamage, DAMAGE_NO); Damage(z, h, 5000.0);
    Check("gore_rejected_damage_no_effects", gGoreBlood == blood && gGoreFlesh == flesh);
    set_entvar(z, var_takedamage, DAMAGE_AIM);
    Damage(z, h, 999.0); zp_core_force_cure(z); zp_core_force_infect(z);
    rg_set_user_armor(z, 0, ARMOR_NONE); set_entvar(z, var_health, 1250.0);
    Damage(z, h, 1.0); Check("gore_cure_infection_resets_counter", gGoreFlesh == flesh);
    Damage(z, h, 100000.0);
    Check("gore_lethal_overkill_counts_remaining_health", gGoreFlesh == flesh + 1);
    flesh = gGoreFlesh;
    set_cvar_num("zp_zombie_gib_chance", 0);
    Damage(other, h, 1500.0);
    Check("gore_zero_chance_skips_fragment", gGoreFlesh == flesh);
    set_cvar_num("zp_zombie_gib_chance", 100);
    Damage(other, h, 499.0);
    Check("gore_skipped_roll_consumed_with_remainder", gGoreFlesh == flesh);
    Damage(other, h, 1.0);
    Check("gore_next_threshold_rolls_again", gGoreFlesh == flesh + 1);
    Check("gore_native_gib_entities_and_physics", gGoreFlesh > 0 && gGoreInvalidGibs == 0);
    gGoreObserve = false;
    set_cvar_float("zp_zombie_defense", defense);
    set_cvar_num("violence_hgibs", vanillaGibs);
    set_cvar_num("zp_zombie_gib_chance", gibChance);
    return Summary();
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
    cs_set_player_team(id, TEAM_TERRORIST, false);
    Check("team_member_and_counts", get_member(id, m_iTeam) == TEAM_TERRORIST &&
        get_member_game(m_iNumTerrorist) == oldT + 1 && get_member_game(m_iNumCT) == oldCT - 1);
    Check("team_silent_option", gTeamInfo[id] == teamMessages && gScoreInfo[id] == scoreMessages);
    cs_set_player_team(id, TEAM_TERRORIST, false);
    Check("repeated_team_does_not_drift_counts", get_member_game(m_iNumTerrorist) == oldT + 1 && get_member_game(m_iNumCT) == oldCT - 1);
    cs_set_player_team(id, TEAM_CT, true);
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

public TestZombieArmor()
{
    if (!get_pcvar_num(gEnabled) || !is_user_alive(gPlayer[0])) return PLUGIN_HANDLED;
    // This phase explicitly exercises immediate native respawn for armor.
    // Countdown ownership is verified separately by the timed phase below.
    set_cvar_num("zp_zombie_respawn_delay", 0);
    set_cvar_num("zp_human_respawn_delay", 0);
    new z = gPlayer[0], h = gPlayer[1];
    set_cvar_num("zp_spawn_protection_time", 0);
    set_cvar_num("zp_zombie_strip_armor", 1);
    set_cvar_num("zp_zombie_hitzones", 0);
    if (zp_gamemodes_get_current() < 0)
        zp_gamemodes_start(zp_gamemodes_get_id("Infection Mode"), z);
    Check("armor_fixture_infection_mode_active", zp_gamemodes_get_current() == zp_gamemodes_get_id("Infection Mode"));
    Check("zombie_armor_default_2000", get_cvar_num("zp_zombie_armor") == 2000);
    for (new n = 0; n < sizeof gPlayer; n++) Human(gPlayer[n]);
    zp_core_force_infect(z);
    Check("infection_grants_vest_and_helmet", Float:get_entvar(z, var_armorvalue) == 2000.0 && get_member(z, m_iKevlar) == ARMOR_VESTHELM);
    for (new hit = HIT_HEAD; hit <= HIT_CHEST; hit++) {
        new Float:health = Float:get_entvar(z, var_health);
        rg_set_user_armor(z, 0, ARMOR_NONE);
        TraceHit(z, h, 40.0, hit);
        new Float:unarmored = health - Float:get_entvar(z, var_health);
        zp_core_force_infect(z);
        health = Float:get_entvar(z, var_health);
        TraceHit(z, h, 40.0, hit);
        new Float:armored = health - Float:get_entvar(z, var_health);
        Check(hit == HIT_HEAD ? "helmet_reduces_head_damage" : "vest_reduces_chest_damage", armored > 0.0 && armored < unarmored);
        Check("shot_consumes_granted_armor", Float:get_entvar(z, var_armorvalue) > 0.0 && Float:get_entvar(z, var_armorvalue) < 2000.0);
    }
    Human(h);
    Damage(h, z, 20.0);
    Check("ordinary_attack_infection_grants_armor", zp_core_is_zombie(h) && Float:get_entvar(h, var_armorvalue) == 2000.0 && get_member(h, m_iKevlar) == ARMOR_VESTHELM);
    set_cvar_num("zp_human_armor_default", 65);
    zp_core_force_cure(h);
    Check("cure_clears_zombie_grant_preserves_human_default", !zp_core_is_zombie(h) && Float:get_entvar(h, var_armorvalue) == 65.0);
    set_cvar_num("zp_human_armor_default", 0);
    set_cvar_num("zp_zombie_armor", 0);
    zp_core_force_infect(h);
    Check("disabled_grant_keeps_no_armor", Float:get_entvar(h, var_armorvalue) == 0.0 && get_member(h, m_iKevlar) == ARMOR_NONE);
    set_cvar_num("zp_zombie_armor", 2000);
    zp_core_respawn_as_zombie(h, true);
    ExecuteHamB(Ham_Killed, h, z, GIB_NEVER);
    rg_round_respawn(h);
    Check("zombie_respawn_grants_helmet_armor", is_user_alive(h) && zp_core_is_zombie(h) && Float:get_entvar(h, var_armorvalue) == 2000.0 && get_member(h, m_iKevlar) == ARMOR_VESTHELM);
    zp_class_nemesis_set(z);
    Check("nemesis_also_receives_helmet_armor", zp_class_nemesis_get(z) && Float:get_entvar(z, var_armorvalue) == 2000.0 && get_member(z, m_iKevlar) == ARMOR_VESTHELM);
    set_cvar_num("zp_zombie_respawn_delay", 3);
    set_cvar_num("zp_human_respawn_delay", 3);
    return Summary();
}

public BeginZombieRespawn()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    for (new n = 0; n < sizeof gPlayer; n++) Human(gPlayer[n]);
    zp_core_force_infect(gPlayer[0]);
    zp_core_force_infect(gPlayer[2]);
    Check("respawn_delays_default_three", get_cvar_num("zp_zombie_respawn_delay") == 3 && get_cvar_num("zp_human_respawn_delay") == 3);
    // Stock deathmatch would revive both as humans before our three seconds.
    set_cvar_num("zp_deathmatch", 1);
    set_cvar_float("zp_respawn_delay", 0.3);
    gDeathAt = get_gametime();
    for (new n = 0; n < 2; n++) {
        gRespawnCounts[n] = gSpawn[gPlayer[n]];
        ExecuteHamB(Ham_Killed, gPlayer[n], gPlayer[3], GIB_NEVER);
    }
    Check("human_and_zombie_dead", !is_user_alive(gPlayer[0]) && !is_user_alive(gPlayer[1]));
    return Summary();
}

public CheckRespawnEarly()
{
    Check("no_early_or_stock_deathmatch_respawn", get_gametime() - gDeathAt < 3.0 && !is_user_alive(gPlayer[0]) && !is_user_alive(gPlayer[1]));
    return Summary();
}

public CheckRespawnDone()
{
    Check("respawn_after_three_seconds", get_gametime() - gDeathAt >= 3.0);
    for (new n = 0; n < 2; n++) {
        new id = gPlayer[n];
        Check(n ? "dead_human_returns_as_zombie" : "dead_zombie_returns_as_zombie", is_user_alive(id) && zp_core_is_zombie(id) && get_member(id, m_iTeam) == TEAM_TERRORIST);
        Check("respawn_gets_2000_and_helmet", Float:get_entvar(id, var_armorvalue) == 2000.0 && get_member(id, m_iKevlar) == ARMOR_VESTHELM);
        Check("respawn_once_only", gSpawn[id] == gRespawnCounts[n] + 1);
    }
    return Summary();
}

public BeginRespawnPolicy()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    new argument[16]; read_argv(1, argument, charsmax(argument));
    new mode = str_to_num(argument);
    new const names[][] = {"Infection Mode", "Nemesis Mode", "Survivor Mode", "Swarm Mode", "Multiple Infection Mode", "Plague Mode", "Armageddon Mode"};
    if (mode < 0 || mode >= sizeof names) return PLUGIN_HANDLED;
    for (new n = 0; n < sizeof gPlayer; n++) Human(gPlayer[n]);
    set_cvar_num("zp_deathmatch", 0);
    Check("respawn_policy_actual_mode_started", zp_gamemodes_start(zp_gamemodes_get_id(names[mode]), gPlayer[2]));
    for (new n = 0; n < sizeof gPlayer; n++) Human(gPlayer[n]);
    zp_core_force_infect(gPlayer[0]); zp_core_force_infect(gPlayer[2]);
    for (new n = 0; n < 2; n++) {
        gRespawnCounts[n] = gSpawn[gPlayer[n]];
        ExecuteHamB(Ham_Killed, gPlayer[n], gPlayer[3], GIB_NEVER);
    }
    return Summary();
}

public CheckRespawnDenied()
{
    for (new n = 0; n < 2; n++)
        Check(n ? "mode_veto_blocks_dead_human" : "mode_veto_blocks_dead_zombie",
            !is_user_alive(gPlayer[n]) && gSpawn[gPlayer[n]] == gRespawnCounts[n]);
    return Summary();
}

public TestHeadKnockback()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    new z = gPlayer[0], h = gPlayer[3];
    Human(h); zp_core_force_infect(z);
    new weapon = rg_give_item(h, "weapon_ak47", GT_REPLACE);
    Check("knockback_uses_actual_rifle", !is_nullent(weapon) && rg_switch_weapon(h, weapon));
    new Float:origin[3]; get_entvar(h, var_origin, origin); origin[0] += 80.0;
    engfunc(EngFunc_SetOrigin, z, origin);
    set_entvar(z, var_flags, (get_entvar(z, var_flags) | FL_ONGROUND) & ~FL_DUCKING);
    set_cvar_num("zp_knockback_obey_class", 0);
    set_cvar_num("zp_knockback_power", 0);
    set_cvar_num("zp_knockback_damage", 1);
    Check("headshot_extra_disabled_by_default", get_cvar_num("zp_zombie_headshot_extra_knockback") == 0);
    new Float:speeds[4], Float:loss[4], Float:velocity[3];
    for (new n = 0; n < 4; n++) {
        set_cvar_num("zp_zombie_headshot_extra_knockback", n % 2);
        set_entvar(z, var_velocity, Float:{0.0, 0.0, 0.0});
        set_entvar(z, var_health, 10000.0);
        rg_set_user_armor(z, 2000, ARMOR_VESTHELM);
        TraceHit(z, h, 40.0, n < 2 ? HIT_HEAD : HIT_CHEST);
        get_entvar(z, var_velocity, velocity);
        speeds[n] = vector_length(velocity);
        loss[n] = 10000.0 - Float:get_entvar(z, var_health);
    }
    Check("headshot_extra_removed_normal_push_kept", speeds[0] > 0.0 && speeds[1] > speeds[0] + 1.0);
    Check("headshot_damage_unchanged", loss[0] > 0.0 && floatabs(loss[0] - loss[1]) < 0.01);
    Check("chest_knockback_unchanged", floatabs(speeds[2] - speeds[3]) < 0.01);
    server_print("[GC ZP ReAPI] headImpulseOff=%.2f on=%.2f chestOff=%.2f on=%.2f", speeds[0], speeds[1], speeds[2], speeds[3]);
    set_cvar_num("zp_zombie_headshot_extra_knockback", 0);
    return Summary();
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
public TeamMessage()
{
    if (!gObserve) return;
    new id = get_msg_arg_int(1), team[16];
    gTeamInfo[id]++;
    get_msg_arg_string(2, team, charsmax(team));
    gNotifiedTeam[id] = equal(team, "CT") ? TEAM_CT : equal(team, "TERRORIST") ? TEAM_TERRORIST : TEAM_UNASSIGNED;
}
public ScoreMessage() { if (gObserve) gScoreInfo[get_msg_arg_int(1)]++; }

// A round-end balance deliberately changes teams silently. The later infection
// must publish the authoritative team even when it already equals Terrorist.
public TestTeamRefresh()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    new id = gPlayer[1];
    cs_set_player_team(id, TEAM_TERRORIST);
    cs_set_player_team(id, TEAM_CT);
    new oldT = get_member_game(m_iNumTerrorist), oldCT = get_member_game(m_iNumCT);
    new messages = gTeamInfo[id], scores = gScoreInfo[id];
    cs_set_player_team(id, TEAM_TERRORIST, false);
    Check("silent_balance_preserves_last_client_team", get_member(id, m_iTeam) == TEAM_TERRORIST && gNotifiedTeam[id] == TEAM_CT);
    cs_set_player_team(id, TEAM_TERRORIST, true);
    Check("same_team_refresh_notifies_client", gTeamInfo[id] == messages + 1 && gScoreInfo[id] == scores + 1 && gNotifiedTeam[id] == TEAM_TERRORIST);
    Check("same_team_refresh_preserves_counts", get_member_game(m_iNumTerrorist) == oldT + 1 && get_member_game(m_iNumCT) == oldCT - 1);
    return Summary();
}

public PrepareSurvivor()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    set_cvar_num("zp_multi_min_players", 33);
    set_cvar_num("zp_swarm_min_players", 33);
    set_cvar_num("zp_nemesis_min_players", 33);
    set_cvar_num("zp_survivor_min_players", 0);
    set_cvar_num("zp_survivor_chance", 1);
    set_cvar_num("zp_prevent_consecutive_modes", 0);
    set_cvar_float("zp_gamemode_delay", 60.0);
    return Summary();
}

public TestSurvivorPending()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    Check("survivor_chosen_but_not_started", zp_gamemodes_get_current() == ZP_NO_GAME_MODE && zp_gamemodes_get_chosen() == zp_gamemodes_get_id("Survivor Mode"));
    new id = gPlayer[1], model[32];
    rg_set_user_team(id, TEAM_CT, MODEL_CT_URBAN);
    rg_round_respawn(id);
    get_user_info(id, "model", model, charsmax(model));
    Check("pending_survivor_spawn_stays_human", is_user_alive(id) && !zp_core_is_zombie(id) && !equal(model, "zombie_source"));
    Check("pending_survivor_spawn_keeps_normal_health", Float:get_entvar(id, var_health) == 100.0);
    return Summary();
}

public TestSurvivorActive()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    Check("survivor_mode_starts", zp_gamemodes_start(zp_gamemodes_get_id("Survivor Mode"), gPlayer[0]));
    for (new n = 1; n < sizeof gPlayer; n++) {
        new id = gPlayer[n];
        Check("active_survivor_zombie_team_matches", zp_core_is_zombie(id) && get_member(id, m_iTeam) == TEAM_TERRORIST && gNotifiedTeam[id] == TEAM_TERRORIST);
    }
    new id = gPlayer[1];
    rg_round_respawn(id);
    Check("active_survivor_respawn_remains_zombie", zp_core_is_zombie(id) && get_member(id, m_iTeam) == TEAM_TERRORIST);
    return Summary();
}
