// Controlled native-server regression. Never enable on the human's server.
// Uses real ReGameDLL grenade factories, Think/Touch, ZP effects and ReAPI.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <amxmodx>
#include <goldcraft_amxx>
#include <fakemeta>
#include <hamsandwich>
#include <reapi>
#include <xs>
#include <nademodes>
#include <zp50_core>
#include <zp50_gamemodes>
#include <zp50_grenade_fire>
#include <zp50_grenade_frost>

new const REPORT[] = "addons/amxmodx/logs/goldcraft-nademodes.jsonl";
new gEnabled, gPlayer[4], gGrenade, gSerial, gMode, gRace, gInfection;
new gChecks, gFailed, gOwner, gEnemy;
new Float:gOrigin[3], Float:gTarget[3], Float:gAnchor[3];
new gEffects[4];
new bool:gObserve;
new gReused, gReuseSerial, gExpectedMedia[3], gMediaMessages[3], gMediaErrors;
new gUnmatchedMedia[3];
new bool:gInspectBeam;
new gHomingAxis, gHomingSign;

public plugin_init()
{
    register_plugin("GoldCraft Nade Modes regression", "0.1", "GoldCraft contributors");
    gEnabled = create_cvar("goldcraft_nademodes_test", "0");
    register_srvcmd("gc_nm_create", "CreatePlayers");
    register_srvcmd("gc_nm_start", "StartMode");
    register_srvcmd("gc_nm_extra", "ExtraCases");
    register_srvcmd("gc_nm_begin", "BeginCase");
    register_srvcmd("gc_nm_trigger", "TriggerCase");
    register_srvcmd("gc_nm_finish", "FinishCase");
    register_srvcmd("gc_nm_lifecycle", "Lifecycle");
    register_srvcmd("gc_nm_after_round", "AfterRound");
    register_srvcmd("gc_nm_after_disconnect", "AfterDisconnect");
    register_srvcmd("gc_nm_remove_player", "RemovePlayer");
    register_srvcmd("gc_nm_team_hold", "TeamHold");
    register_srvcmd("gc_nm_team_release", "TeamRelease");
    register_srvcmd("gc_nm_homing_team", "HomingTeam");
    register_srvcmd("gc_nm_more_lifecycle", "MoreLifecycle");
    register_srvcmd("gc_nm_delayed", "DelayedFree");
    register_srvcmd("gc_nm_reuse", "ReuseSlot");
    register_srvcmd("gc_nm_after_map", "AfterMap");
    register_srvcmd("gc_nm_before_map", "BeforeMap");
    register_srvcmd("gc_nm_media", "MediaSummary");
    register_srvcmd("gc_nm_beam", "BeamCase");
    register_srvcmd("gc_nm_beam_check", "BeamCheck");
    gExpectedMedia[0] = engfunc(EngFunc_ModelIndex, "sprites/laserbeam.spr");
    gExpectedMedia[1] = engfunc(EngFunc_ModelIndex, "sprites/shockwave.spr");
    gExpectedMedia[2] = engfunc(EngFunc_ModelIndex, "models/chromegibs.mdl");
    RegisterMessage(SVC_TEMPENTITY, "MediaMessage");
    RegisterHam(Ham_Think, "grenade", "BeamThink");
    RegisterHam(Ham_Think, "grenade", "BeamThinkPost", true);
    RegisterHookChain(RH_ED_Free, "BeamFree");
}

public BeamThink(ent)
{
    if (gInspectBeam && ent == gGrenade && pev_serial(ent) == gSerial)
        server_print("[GC NM] beam_pre type=%d trip=%d surface=%d flags=%d now=%.3f think=%.3f dmg=%.3f arm=%.3f owner=%d ownerAlive=%d",
            get_entvar(ent,var_iuser1),get_entvar(ent,var_iuser3),get_entvar(ent,var_iuser4),get_entvar(ent,var_flags),
            get_gametime(),Float:get_entvar(ent,var_nextthink),Float:get_entvar(ent,var_dmgtime),
            Float:get_entvar(ent,var_fuser1),get_entvar(ent,var_owner),is_user_alive(gOwner));
    return HAM_IGNORED;
}

public BeamThinkPost(ent)
{
    if (gInspectBeam && ent == gGrenade && pev_valid(ent)) BeamThink(ent);
    return HAM_IGNORED;
}

public BeamFree(ent)
{
    if (gInspectBeam && ent == gGrenade) {
        server_print("[GC NM] beam_ed_free time=%.3f type=%d trip=%d dmg=%.3f flags=%d",get_gametime(),
            get_entvar(ent,var_iuser1),get_entvar(ent,var_iuser3),Float:get_entvar(ent,var_dmgtime),get_entvar(ent,var_flags));
        gInspectBeam = false;
    }
    return HC_CONTINUE;
}

public MediaMessage(type, destination, receiver)
{
    if (!get_pcvar_num(gEnabled)) return HC_CONTINUE;
    new slot = -1, arg, count, tail, expectedTail;
    switch (GetMessageData(MsgArg, 1)) {
        case TE_BEAMENTPOINT: { slot = 0; arg = 6; count = 16; tail = 16; expectedTail = 1; }
        case TE_BEAMCYLINDER: { slot = 1; arg = 8; count = 18; tail = 18; expectedTail = 0; }
        case TE_BREAKMODEL: { slot = 2; arg = 12; count = 15; tail = 15; expectedTail = 2; }
    }
    // Count messages that actually reference this plugin's media, including
    // all arguments after the expanded model field. ZP uses other sprites.
    if (slot < 0) return HC_CONTINUE;
    if (GetMessageData(MsgArg, arg) != gExpectedMedia[slot]) {
        if (gUnmatchedMedia[slot]++ < 2)
            server_print("[GC NM] unmatched_media slot=%d value=%d expected=%d args=%d tail=%d", slot,
                GetMessageData(MsgArg, arg), gExpectedMedia[slot], GetMessageArgsNum(), GetMessageData(MsgArg, tail));
        return HC_CONTINUE;
    }
    gMediaMessages[slot]++;
    if (GetMessageArgsNum() != count || GetMessageData(MsgArg, tail) != expectedTail)
        gMediaErrors++;
    return HC_CONTINUE;
}

public MediaSummary()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    new arg[8]; read_argv(1, arg, charsmax(arg));
    Check("grenade_beam_messages_observed", gMediaMessages[0] > 0);
    Check("grenade_ring_messages_observed", gMediaMessages[1] > 0);
    Check("grenade_gib_messages_observed", gMediaMessages[2] > 0);
    Check("media_following_fields_preserved", gMediaErrors == 0);
    if (str_to_num(arg)) {
        Check("actual_beam_model_index_over_16bits", gExpectedMedia[0] >= 65535);
        Check("actual_ring_model_index_over_16bits", gExpectedMedia[1] >= 65535);
        Check("actual_gib_model_index_over_16bits", gExpectedMedia[2] >= 65535);
    }
    server_print("[GC NM] media=%d,%d,%d messages=%d,%d,%d errors=%d",
        gExpectedMedia[0],gExpectedMedia[1],gExpectedMedia[2],gMediaMessages[0],gMediaMessages[1],gMediaMessages[2],gMediaErrors);
    if (Current()) server_print("[GC NM] beam_state type=%d trip=%d now=%.3f think=%.3f dmgtime=%.3f arm=%.3f effects=%d",
        get_entvar(gGrenade,var_iuser1),get_entvar(gGrenade,var_iuser3),get_gametime(),
        Float:get_entvar(gGrenade,var_nextthink),Float:get_entvar(gGrenade,var_dmgtime),
        Float:get_entvar(gGrenade,var_fuser1),get_cvar_num("nademodes_effects"));
    return Summary();
}

Check(const name[], condition)
{
    gChecks++;
    if (!condition) gFailed++;
    new file = fopen(REPORT, "at");
    if (file) {
        fprintf(file, "{^"mode^":%d,^"race^":%d,^"infection^":%d,^"name^":^"%s^",^"passed^":%d}^n",
            gMode, gRace, gInfection, name, !!condition);
        fclose(file);
    }
    server_print("[GC NM] %s mode=%d race=%d infection=%d %s", condition ? "PASS" : "FAIL", gMode, gRace, gInfection, name);
}

Summary()
{
    server_print("[GC NM] checks=%d failed=%d grenade=%d serial=%d", gChecks, gFailed, gGrenade, gSerial);
    return PLUGIN_HANDLED;
}

public CreatePlayers()
{
    if (!get_pcvar_num(gEnabled) || gPlayer[0]) return PLUGIN_HANDLED;
    new file = fopen(REPORT, "wt");
    if (file) fclose(file);
    for (new n = 0; n < sizeof gPlayer; n++) {
        new name[32], reject[128];
        formatex(name, charsmax(name), "GC_NadeModes_%d", n + 1);
        new id = engfunc(EngFunc_CreateFakeClient, name);
        gPlayer[n] = id;
        if (!id) { Check("create_native_fakeclient", false); return Summary(); }
        dllfunc(DLLFunc_ClientConnect, id, name, "127.0.0.1", reject);
        dllfunc(DLLFunc_ClientPutInServer, id);
        set_entvar(id, var_flags, get_entvar(id, var_flags) | FL_FAKECLIENT);
        set_member(id, m_iJoiningState, JOINED);
        set_member(id, m_bJustConnected, false);
        set_member(id, m_iMenu, Menu_OFF);
        rg_set_user_team(id, n == 1 || n == 3 ? TEAM_TERRORIST : TEAM_CT);
        rg_round_respawn(id);
        Check("native_fakeclient_spawned", is_user_alive(id) && is_user_bot(id));
    }
    // Use an actual deterministic BSP spawn; players still spawn normally.
    new anchorSpawn = rg_find_ent_by_class(-1, "info_player_start");
    get_entvar(anchorSpawn > 0 ? anchorSpawn : gPlayer[0], var_origin, gAnchor);
    return Summary();
}

public StartMode()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    Check("actual_zp_gamemode_started", zp_gamemodes_start(zp_gamemodes_get_id("Infection Mode"), gPlayer[1]));
    return Summary();
}

Cleanup()
{
    gInspectBeam = false;
    new ent = -1;
    while ((ent = rg_find_ent_by_class(ent, "grenade"))) {
        if (nm_get_grenade_race(ent) >= 0) engfunc(EngFunc_RemoveEntity, ent);
    }
    gObserve = false;
    arrayset(gEffects, 0, sizeof gEffects);
}

Actors()
{
    for (new n = 0; n < sizeof gPlayer; n++) {
        new id = gPlayer[n];
        if (!is_user_alive(id)) rg_round_respawn(id);
        zp_grenade_fire_set(id, false);
        zp_grenade_frost_set(id, false);
        if (n == 1 || n == 3) zp_core_force_infect(id);
        else zp_core_force_cure(id);
        set_entvar(id, var_health, 5000.0);
        rg_set_user_armor(id, 0, ARMOR_NONE);
        set_entvar(id, var_button, 0);
        set_entvar(id, var_movetype, MOVETYPE_NONE);
        set_entvar(id, var_velocity, Float:{0.0, 0.0, 0.0});
        ExecuteHamB(Ham_Player_PreThink, id);
    }
    gOwner = gPlayer[gInfection ? 1 : 0];
    gEnemy = gPlayer[gInfection ? 0 : 1];
    // Start from a native map spawn, then trace its actual floor.
    xs_vec_copy(gAnchor, gOrigin);
    gOrigin[2] += 48.0;
    new Float:end[3], trace = create_tr2();
    xs_vec_copy(gOrigin, end);
    end[2] -= 4096.0;
    engfunc(EngFunc_TraceLine, gOrigin, end, IGNORE_MONSTERS, gOwner, trace);
    get_tr2(trace, TR_vecEndPos, gOrigin);
    gOrigin[2] += 1.0;
    free_tr2(trace);
    xs_vec_copy(gOrigin, gTarget);
    gTarget[2] += 58.0;
    for (new n = 0; n < sizeof gPlayer; n++) {
        new Float:away[3];
        xs_vec_copy(gOrigin, away);
        away[0] += 600.0 + float(n) * 80.0;
        away[2] += 40.0;
        engfunc(EngFunc_SetOrigin, gPlayer[n], away);
    }
    engfunc(EngFunc_SetOrigin, gEnemy, gTarget);
}

public BeginCase()
{
    if (!get_pcvar_num(gEnabled) || !gPlayer[0]) return PLUGIN_HANDLED;
    Cleanup();
    new arg[16];
    read_argv(1, arg, charsmax(arg)); gMode = str_to_num(arg);
    read_argv(2, arg, charsmax(arg)); gRace = str_to_num(arg);
    read_argv(3, arg, charsmax(arg)); gInfection = str_to_num(arg);
    if (!(0 <= gMode <= 6) || !(0 <= gRace <= 2)) return PLUGIN_HANDLED;
    Actors();
    Check("mode_selected_through_public_api", nm_set_player_mode(gOwner, NadeRace:gRace, NadeType:gMode));
    gGrenade = rg_spawn_grenade(WeaponIdType:NadeWeaponId[NadeRace:gRace], gOwner, gOrigin,
        Float:{0.0, 0.0, 0.0}, 1.0, TeamName:get_member(gOwner, m_iTeam));
    Check("actual_factory_registered_race", gGrenade > 0 && nm_get_grenade_race(gGrenade) == gRace);
    if (gGrenade <= 0) return Summary();
    gSerial = pev_serial(gGrenade);
    Check("actual_factory_selected_mode", get_entvar(gGrenade, var_iuser1) == gMode);
    new expected = gInfection ? 1111 : gRace == 0 ? 2222 : gRace == 1 ? 3333 : 4444;
    Check("zp_grenade_tag_preserved", get_entvar(gGrenade, var_flTimeStepSound) == expected);
    set_entvar(gGrenade, var_movetype, MOVETYPE_NONE);
    if (gMode == _:NADE_SATCHEL && gRace == _:GRENADE_SMOKEGREN) {
        // Deliberately airborne remote flare: activation must not await landing.
        new Float:air[3]; xs_vec_copy(gOrigin, air); air[2] += 48.0;
        engfunc(EngFunc_SetOrigin, gGrenade, air);
        set_entvar(gGrenade, var_flags, get_entvar(gGrenade, var_flags) & ~FL_ONGROUND);
        set_entvar(gGrenade, var_velocity, Float:{30.0, 0.0, 0.0});
    } else {
        set_entvar(gGrenade, var_flags, get_entvar(gGrenade, var_flags) | FL_ONGROUND);
    }
    if (gMode == _:NADE_TRIP) ExecuteHamB(Ham_Touch, gGrenade, 0);
    if (gMode == _:NADE_HOMING) {
        new trace = create_tr2(), Float:start[3], Float:candidate[3], Float:fraction, bool:clear;
        new const directions[4][2] = {{1,1},{1,-1},{0,1},{0,-1}};
        xs_vec_copy(gOrigin, start); start[2] += 2.0;
        for (new n = 0; n < sizeof directions; n++) {
            xs_vec_copy(gTarget, candidate);
            candidate[2] = gOrigin[2] + 40.0;
            candidate[directions[n][0]] += float(directions[n][1]) * 80.0;
            engfunc(EngFunc_TraceLine, start, candidate, IGNORE_MONSTERS, gGrenade, trace);
            get_tr2(trace, TR_flFraction, fraction);
            if (fraction != 1.0 || get_tr2(trace,TR_StartSolid)) continue;
            engfunc(EngFunc_TraceHull, candidate, candidate, IGNORE_MONSTERS, HULL_HUMAN, gEnemy, trace);
            if (get_tr2(trace,TR_StartSolid) || get_tr2(trace,TR_AllSolid)) continue;
            xs_vec_copy(candidate, gTarget);
            gHomingAxis = directions[n][0]; gHomingSign = directions[n][1]; clear = true;
            break;
        }
        free_tr2(trace);
        Check("homing_fixture_has_clear_native_ray_and_hull", clear);
        engfunc(EngFunc_SetOrigin, gEnemy, gTarget);
        new Float:initial[3]; initial[1 - gHomingAxis] = 10.0;
        set_entvar(gGrenade, var_velocity, initial);
        if (gRace == _:GRENADE_SMOKEGREN)
            set_entvar(gGrenade, var_movetype, MOVETYPE_BOUNCE);
    }
    gObserve = true;
    return Summary();
}

bool:Current()
{
    return pev_valid(gGrenade) && pev_serial(gGrenade) == gSerial;
}

public TriggerCase()
{
    if (!get_pcvar_num(gEnabled) || !gObserve) return PLUGIN_HANDLED;
    if (gMode == _:NADE_MOTION || gMode == _:NADE_SATCHEL || gMode == _:NADE_IMPACT) {
        Check("waits_for_real_trigger", Current() && Float:get_entvar(gGrenade, var_dmgtime) > get_gametime());
    }
    switch (gMode) {
        case NADE_IMPACT: if (Current()) ExecuteHamB(Ham_Touch, gGrenade, 0);
        case NADE_MOTION: set_entvar(gEnemy, var_velocity, Float:{280.0, 0.0, 0.0});
        case NADE_SATCHEL: set_entvar(gOwner, var_button, IN_USE);
        case NADE_HOMING: {
            new Float:velocity[3];
            if (Current()) get_entvar(gGrenade, var_velocity, velocity);
            Check("homing_selected_enemy_and_steered", Current() && get_entvar(gGrenade, var_iuser2) == gEnemy && velocity[gHomingAxis] * float(gHomingSign) > 0.0);
            server_print("[GC NM] homing_target axis=%d sign=%d velocity=%.3f,%.3f,%.3f",gHomingAxis,gHomingSign,velocity[0],velocity[1],velocity[2]);
        }
    }
    return Summary();
}

public FinishCase()
{
    if (!get_pcvar_num(gEnabled) || !gObserve) return PLUGIN_HANDLED;
    if (gInfection) Check("actual_zp_infection_effect", gEffects[3] && zp_core_is_zombie(gEnemy));
    else if (gRace == 0) Check("actual_zp_fire_effect", gEffects[0] || zp_grenade_fire_get(gEnemy));
    else if (gRace == 1) Check("actual_zp_frost_effect", gEffects[1] || zp_grenade_frost_get(gEnemy));
    else Check("actual_zp_flare_duration", Current() && get_entvar(gGrenade, var_flSwimTime) > 0);
    if (gRace != 2) Check("grenade_consumed_by_zp", !Current() || (get_entvar(gGrenade, var_flags) & FL_KILLME));
    set_entvar(gOwner, var_button, 0);
    set_entvar(gEnemy, var_velocity, Float:{0.0, 0.0, 0.0});
    gObserve = false;
    return Summary();
}

public zp_fw_grenade_fire_pre(id) { if (gObserve && id == gEnemy) gEffects[0]++; }
public zp_fw_grenade_frost_pre(id) { if (gObserve && id == gEnemy) gEffects[1]++; }
public zp_fw_core_infect_post(id, attacker) { if (gObserve && id == gEnemy) gEffects[3]++; }

public Lifecycle()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    Cleanup();
    gMode = 5; gRace = 0; gInfection = 0;
    Actors();
    nm_set_player_mode(gOwner, GRENADE_EXPLOSIVE, NADE_SATCHEL);
    set_cvar_num("nademodes_material_system", 2);
    gGrenade = rg_spawn_grenade(WEAPON_HEGRENADE, gOwner, gOrigin, Float:{0.0,0.0,0.0}, 1.0, TEAM_CT);
    gSerial = pev_serial(gGrenade);
    Check("amxx19_cvar_change_applies_without_poll", get_entvar(gGrenade, var_takedamage) == DAMAGE_YES);
    Check("integer_hitpoints_are_numeric_not_float_bits", floatabs(Float:get_entvar(gGrenade, var_health) - 100.0) < 0.01);
    new bomb = rg_plant_bomb(gPlayer[1], gOrigin);
    Check("actual_c4_never_claimed", bomb > 0 && get_member(bomb, m_Grenade_bIsC4) && nm_get_grenade_race(bomb) == -1);
    if (bomb > 0) engfunc(EngFunc_RemoveEntity, bomb);
    set_cvar_num("nademodes_material_system", 0);
    return Summary();
}

public ExtraCases()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    Cleanup();
    gMode = 5; gRace = 0; gInfection = 0;
    Actors();
    set_cvar_num("nademodes_limit_system", 2);
    set_cvar_num("nademodes_satchel_limit", 1);
    Check("mode_available_before_limit", nm_set_player_mode(gOwner, GRENADE_EXPLOSIVE, NADE_SATCHEL));
    new ent = rg_spawn_grenade(WEAPON_HEGRENADE, gOwner, gOrigin, Float:{0.0,0.0,0.0}, 1.0, TEAM_CT);
    Check("cross_race_limit_enforced", !nm_set_player_mode(gOwner, GRENADE_FLASHBANG, NADE_SATCHEL));
    engfunc(EngFunc_RemoveEntity, ent);
    Check("external_entity_free_releases_quota", nm_set_player_mode(gOwner, GRENADE_FLASHBANG, NADE_SATCHEL));
    Check("freed_slot_not_treated_as_grenade", nm_get_grenade_race(ent) == -1);
    set_cvar_num("nademodes_limit_system", 0);

    set_cvar_num("nademodes_material_system", 2);
    nm_set_player_mode(gOwner, GRENADE_EXPLOSIVE, NADE_SATCHEL);
    ent = rg_spawn_grenade(WEAPON_HEGRENADE, gOwner, gOrigin, Float:{0.0,0.0,0.0}, 1.0, TEAM_CT);
    ExecuteHamB(Ham_TakeDamage, ent, gEnemy, gEnemy, 35.0, DMG_BULLET);
    Check("actual_grenade_nonlethal_damage", floatabs(Float:get_entvar(ent, var_health) - 65.0) < 0.01);
    set_cvar_num("nademodes_grenade_death", 0);
    ExecuteHamB(Ham_TakeDamage, ent, gEnemy, gEnemy, 200.0, DMG_BULLET);
    Check("actual_grenade_destroyed_into_gibs", !pev_valid(ent) || (get_entvar(ent, var_flags) & FL_KILLME));
    set_cvar_num("nademodes_grenade_death", 1);
    set_cvar_num("nademodes_material_system", 0);

    // RunPlayerMove reaches native CmdStart. These are fake-client commands,
    // not input sent to the user's B window or a direct changemode() call.
    new weapon = rg_give_item(gOwner, "weapon_hegrenade");
    gc_set_bpammo(gOwner, WEAPON_HEGRENADE, 3);
    // GiveNamedItemEx may consume the new entity when the inventory already
    // has this grenade. Select the owned item rather than the duplicate.
    weapon = rg_find_weapon_bpack_by_name(gOwner, "weapon_hegrenade");
    Check("native_grenade_weapon_selected", weapon > 0 && rg_switch_weapon(gOwner, weapon));
    ExecuteHamB(Ham_Player_PreThink, gOwner);
    nm_set_player_mode(gOwner, GRENADE_EXPLOSIVE, NADE_NORMAL);
    new Float:angles[3];
    engfunc(EngFunc_RunPlayerMove, gOwner, angles, 0.0, 0.0, 0.0, 0, 0, 20);
    engfunc(EngFunc_RunPlayerMove, gOwner, angles, 0.0, 0.0, 0.0, IN_ATTACK2, 0, 20);
    Check("native_mouse2_command_cycles_mode", nm_get_player_mode(gOwner, GRENADE_EXPLOSIVE) == _:NADE_PROXIMITY);
    engfunc(EngFunc_RunPlayerMove, gOwner, angles, 0.0, 0.0, 0.0, IN_ATTACK2, 0, 20);
    Check("held_mouse2_does_not_repeat", nm_get_player_mode(gOwner, GRENADE_EXPLOSIVE) == _:NADE_PROXIMITY);
    engfunc(EngFunc_RunPlayerMove, gOwner, angles, 0.0, 0.0, 0.0, 0, 0, 20);
    engfunc(EngFunc_RunPlayerMove, gOwner, angles, 0.0, 0.0, 0.0, IN_ATTACK2, 0, 20);
    new selectedMode = nm_get_player_mode(gOwner, GRENADE_EXPLOSIVE);
    Check("released_mouse2_cycles_again", selectedMode == _:NADE_IMPACT);
    server_print("[GC NM] cmdstart_after_second_press mode=%d weapon=%d alive=%d", selectedMode,
        get_member(get_member(gOwner,m_pActiveItem),m_iId),is_user_alive(gOwner));
    engfunc(EngFunc_RunPlayerMove, gOwner, angles, 0.0, 0.0, 0.0, IN_USE, 0, 20);
    Check("native_use_bit_preserved", (get_entvar(gOwner, var_button) & IN_USE) != 0);
    engfunc(EngFunc_RunPlayerMove, gOwner, angles, 0.0, 0.0, 0.0, 0, 0, 20);
    return Summary();
}

public TeamHold()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    // Create and change teams in one command, before the first 100 ms think.
    // RCON's response drain lasts 250 ms, so two commands would be too late.
    BeginCase();
    if (!gObserve) return PLUGIN_HANDLED;
    zp_core_force_cure(gEnemy);
    ExecuteHamB(Ham_Player_PreThink, gEnemy);
    Check("target_really_changed_to_owner_team", get_member(gEnemy, m_iTeam) == get_member(gOwner, m_iTeam));
    return Summary();
}

public TeamRelease()
{
    if (!get_pcvar_num(gEnabled) || !gObserve) return PLUGIN_HANDLED;
    Check("proximity_does_not_trigger_on_same_team", Current() && Float:get_entvar(gGrenade, var_dmgtime) > get_gametime() && !gEffects[0]);
    zp_core_force_infect(gEnemy);
    ExecuteHamB(Ham_Player_PreThink, gEnemy);
    Check("target_really_changed_back_to_enemy", get_member(gEnemy, m_iTeam) != get_member(gOwner, m_iTeam));
    return Summary();
}

public HomingTeam()
{
    if (!get_pcvar_num(gEnabled) || !gObserve) return PLUGIN_HANDLED;
    Check("homing_has_actual_target_before_team_change", Current() && get_entvar(gGrenade, var_iuser2) == gEnemy);
    zp_core_force_cure(gEnemy);
    ExecuteHamB(Ham_Player_PreThink, gEnemy);
    if (Current()) ExecuteHamB(Ham_Think, gGrenade);
    Check("homing_releases_target_that_became_teammate", Current() && get_entvar(gGrenade, var_iuser2) != gEnemy);
    return Summary();
}

SpawnRemote()
{
    nm_set_player_mode(gOwner, GRENADE_EXPLOSIVE, NADE_SATCHEL);
    gGrenade = rg_spawn_grenade(WEAPON_HEGRENADE, gOwner, gOrigin, Float:{0.0,0.0,0.0}, 1.0, TEAM_CT);
    gSerial = pev_serial(gGrenade);
    return gGrenade;
}

public MoreLifecycle()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    Cleanup(); gInfection = 0; Actors();
    SpawnRemote();
    ExecuteHamB(Ham_Killed, gOwner, gEnemy, 0);
    Check("actual_killed_cleans_custom_trap", !is_user_alive(gOwner) && nm_get_grenade_race(gGrenade) == -1);
    rg_round_respawn(gOwner);
    zp_core_force_cure(gOwner);
    Check("actual_respawn_allows_new_trap", is_user_alive(gOwner) && nm_set_player_mode(gOwner, GRENADE_EXPLOSIVE, NADE_SATCHEL));
    SpawnRemote();
    zp_core_force_infect(gOwner);
    Check("actual_infection_cleans_human_trap", zp_core_is_zombie(gOwner) && nm_get_grenade_race(gGrenade) == -1);
    zp_core_force_cure(gOwner);
    return Summary();
}

public DelayedFree()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    Cleanup(); gInfection = 0; Actors();
    nm_set_player_mode(gOwner, GRENADE_EXPLOSIVE, NADE_TRIP);
    gGrenade = rg_spawn_grenade(WEAPON_HEGRENADE, gOwner, gOrigin, Float:{0.0,0.0,0.0}, 1.0, TEAM_CT);
    gSerial = pev_serial(gGrenade);
    set_entvar(gGrenade, var_movetype, MOVETYPE_NONE);
    ExecuteHamB(Ham_Touch, gGrenade, 0);
    ExecuteHamB(Ham_Think, gGrenade);
    Check("actual_trip_charge_task_queued", task_exists(100000 + gGrenade, 1));
    gReused = gGrenade; gReuseSerial = gSerial;
    engfunc(EngFunc_RemoveEntity, gGrenade);
    Check("actual_entity_free_cancels_charge_task", !task_exists(100000 + gReused, 1));
    return Summary();
}

public BeamCase()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    Cleanup(); gInfection = 0; Actors();
    new Float:away[3]; xs_vec_copy(gTarget, away); away[0] += 800.0;
    engfunc(EngFunc_SetOrigin, gEnemy, away);
    Check("beam_fixture_trip_selected", nm_set_player_mode(gOwner, GRENADE_EXPLOSIVE, NADE_TRIP));
    gGrenade = rg_spawn_grenade(WEAPON_HEGRENADE, gOwner, gOrigin, Float:{0.0,0.0,0.0}, 1.0, TEAM_CT);
    gSerial = pev_serial(gGrenade);
    gInspectBeam = true;
    set_entvar(gGrenade, var_movetype, MOVETYPE_NONE);
    set_entvar(gGrenade, var_flags, get_entvar(gGrenade, var_flags) | FL_ONGROUND);
    ExecuteHamB(Ham_Touch, gGrenade, 0);
    ExecuteHamB(Ham_Think, gGrenade);
    Check("beam_fixture_started_with_clear_path", Current());
    Check("world_trace_attachment_has_no_reusable_edict", get_entvar(gGrenade,var_iuser4) == 0);
    server_print("[GC NM] round_state end=%.3f mode=%d", Float:get_member_game(m_flRestartRoundTime), zp_gamemodes_get_current());
    server_print("[GC NM] beam_created type=%d trip=%d now=%.3f think=%.3f dmgtime=%.3f origin=%.3f,%.3f,%.3f",
        get_entvar(gGrenade,var_iuser1),get_entvar(gGrenade,var_iuser3),get_gametime(),
        Float:get_entvar(gGrenade,var_nextthink),Float:get_entvar(gGrenade,var_dmgtime),gOrigin[0],gOrigin[1],gOrigin[2]);
    return Summary();
}

public ReuseSlot()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    // ED_Alloc will reuse released edicts after its real 0.5-second grace time.
    // Hold earlier holes until the exact previous number is allocated.
    new held[1024], count, found;
    while (count < sizeof held) {
        new ent = rg_create_entity("info_target");
        if (!ent) break;
        held[count++] = ent;
        if (ent == gReused) { found = ent; break; }
        if (ent > gReused) break;
    }
    Check("engine_reuses_actual_freed_edict_number", found == gReused && found > 0);
    Check("reused_edict_has_new_serial", found > 0 && pev_serial(found) != gReuseSerial);
    Check("reused_non_grenade_keeps_own_identity", found > 0 && nm_get_grenade_race(found) == -1);
    Check("reused_edict_has_no_old_charge_task", !task_exists(100000 + gReused, 1));
    for (new n = 0; n < count; n++) engfunc(EngFunc_RemoveEntity, held[n]);
    return Summary();
}

public BeamCheck()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    Check("trip_survives_without_enemy_crossing", Current());
    if (Current()) server_print("[GC NM] beam_wait type=%d trip=%d now=%.3f think=%.3f dmgtime=%.3f arm=%.3f armcvar=%.3f draw=%.3f effects=%d messages=%d",
        get_entvar(gGrenade,var_iuser1),get_entvar(gGrenade,var_iuser3),get_gametime(),
        Float:get_entvar(gGrenade,var_nextthink),Float:get_entvar(gGrenade,var_dmgtime),
        Float:get_entvar(gGrenade,var_fuser1),get_cvar_float("nademodes_trip_grenade_arm_time"),
        Float:get_entvar(gGrenade,var_fuser4),get_cvar_num("nademodes_effects"),gMediaMessages[0]);
    return Summary();
}

public AfterMap()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    new ent = -1, active;
    while ((ent = rg_find_ent_by_class(ent, "grenade")))
        if (nm_get_grenade_race(ent) >= 0) active++;
    Check("actual_map_reload_has_no_old_grenade_records", active == 0);
    return Summary();
}

public BeforeMap()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    gOwner = gPlayer[2];
    if (!is_user_alive(gOwner)) rg_round_respawn(gOwner);
    zp_core_force_cure(gOwner);
    SpawnRemote();
    Check("map_reload_starts_with_live_trap", Current() && nm_get_grenade_race(gGrenade) == 0);
    set_cvar_float("nademodes_trip_scan_interval", 0.03125);
    set_cvar_float("nademodes_homing_velocity_deviation", 55.125);
    return Summary();
}

public AfterRound()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    Check("actual_round_cleans_old_nade", !Current() || nm_get_grenade_race(gGrenade) == -1 || (get_entvar(gGrenade, var_flags) & FL_KILLME));
    return Summary();
}

public RemovePlayer()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    server_cmd("kick #%d", get_user_userid(gOwner));
    server_exec();
    return PLUGIN_HANDLED;
}

public AfterDisconnect()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    Check("actual_disconnect_cleans_owner_nade", !Current() || nm_get_grenade_race(gGrenade) == -1 || (get_entvar(gGrenade, var_flags) & FL_KILLME));
    Check("old_entity_handle_not_reported_as_grenade", nm_get_grenade_race(gGrenade) == -1);
    return Summary();
}
