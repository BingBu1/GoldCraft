// SPDX-License-Identifier: GPL-3.0-or-later
// Explicit local test: one owned fake CT, actual Replay001 transaction.
// No map change, direct restart call or write to monster/GameRules state.
#include <amxmodx>
#include <fakemeta>
#include <reapi>
#include <csol_scenario_admin>

#define LOG_FILE "goldcraft_scenario_replay_test.log"
new g_Enabled, g_Player, g_Userid, g_Serial, g_Spawns, g_Stage, g_LastSpawns;
new g_MapId, g_Maximum, g_InitialMoney, g_Round;
new bool:g_Active, bool:g_Cleaning, bool:g_Protect, bool:g_Placed;
new Float:g_Deadline, Float:g_NextMove, Float:g_NextSample, Float:g_RequestedAt;
new Float:g_FirstTime, Float:g_InitialWalls;

public plugin_init()
{
    register_plugin("CSOL Replay transaction test", "0.1.0", "GoldCraft contributors");
    g_Enabled = create_cvar("csol_replay_test", "0");
    register_srvcmd("csol_replay_test_begin", "Begin");
    register_srvcmd("csol_replay_test_end", "End");
    RegisterHookChain(RG_CBasePlayer_Spawn, "Spawned", true);
    register_forward(FM_StartFrame, "Frame", false);
    new message = get_user_msgid("MPToCL");
    if (message) register_message(message, "ScenarioMessage");
}

Emit(const format[], any:...)
{
    new text[700]; vformat(text, charsmax(text), format, 2);
    server_print("[Replay test] time=%.3f %s", get_gametime(), text);
    log_to_file(LOG_FILE, "time=%.3f %s", get_gametime(), text);
}

bool:Owned()
{
    return g_Player > 0 && g_Player <= MaxClients && g_Userid > 0 &&
        is_user_connected(g_Player) && get_user_userid(g_Player) == g_Userid &&
        pev_valid(g_Player) == 2 && pev_serial(g_Player) == g_Serial;
}

Cleanup()
{
    g_Active = false; g_Cleaning = true;
    if (Owned()) rh_drop_client(g_Player, "Replay test complete");
    g_Player = g_Userid = g_Serial = 0; g_Cleaning = false;
}
Stop(const reason[])
{ Emit("event=end reason=%s stage=%d spawns=%d", reason, g_Stage, g_Spawns); Cleanup(); }
public End() { if (g_Active) Stop("operator_stop"); return PLUGIN_HANDLED; }
public plugin_end() { Cleanup(); }
public client_disconnected(index)
{
    if (index != g_Player) return;
    if (g_Active && !g_Cleaning) { Emit("event=end reason=owned_disconnect"); g_Active = false; }
    g_Player = g_Userid = g_Serial = 0;
}

public Begin()
{
    if (!get_pcvar_num(g_Enabled) || g_Active) return PLUGIN_HANDLED;
    new map[32], active, requested;
    get_mapname(map, charsmax(map));
    if (!equal(map, "zs_deadend") || MaxClients != 32 || get_cvar_num("csol_game_mode") != 2 ||
        get_member_game(m_bGameStarted) || get_cvar_num("sypb_quota") != 0) return PLUGIN_HANDLED;
    for (new index = 1; index <= MaxClients; index++) if (is_user_connected(index)) return PLUGIN_HANDLED;
    if (csol_scenario_admin_api_version() != 1 || csol_scenario_replay_api_version() != 1 ||
        !csol_scenario_difficulty_info(active, requested, g_Maximum, g_MapId) ||
        active != 1 || requested != 1 || g_Maximum < 2 || g_MapId != 98) return PLUGIN_HANDLED;
    g_Active = true; g_Stage = g_Spawns = g_Round = 0;
    g_FirstTime = get_gametime(); g_Deadline = g_FirstTime + 180.0;
    Emit("event=begin mapid=%d maximum=%d api=1 fake_clients=1", g_MapId, g_Maximum);
    g_Player = engfunc(EngFunc_CreateFakeClient, "GC_Replay_Probe");
    if (g_Player < 1 || !is_user_connected(g_Player) || !is_user_bot(g_Player))
    { Stop("fake_allocation_failed"); return PLUGIN_HANDLED; }
    g_Userid = get_user_userid(g_Player);
    if (pev_valid(g_Player) == 2) engfunc(EngFunc_FreeEntPrivateData, g_Player);
    g_Serial = pev_serial(g_Player);
    set_pev(g_Player, pev_flags, pev(g_Player, pev_flags) | FL_FAKECLIENT | FL_CLIENT);
    dllfunc(DLLFunc_ClientPutInServer, g_Player);
    if (!Owned()) { Stop("fake_private_failed"); return PLUGIN_HANDLED; }
    set_entvar(g_Player, var_flags, get_entvar(g_Player, var_flags) | FL_FAKECLIENT | FL_CLIENT);
    set_member(g_Player, m_iMenu, Menu_ChooseTeam);
    set_member(g_Player, m_iJoiningState, PICKINGTEAM);
    engclient_cmd(g_Player, "jointeam", "2");
    engclient_cmd(g_Player, "specmode", "3");
    engclient_cmd(g_Player, "joinclass", "1");
    return PLUGIN_HANDLED;
}

public Spawned(index)
{
    if (!g_Active || index != g_Player || !Owned() ||
        !IsReapiHookOriginalWasCalled(RG_CBasePlayer_Spawn)) return HC_CONTINUE;
    set_entvar(index, var_flags, get_entvar(index, var_flags) | FL_FAKECLIENT | FL_CLIENT);
    g_Spawns++; g_Protect = true; g_Placed = false;
    Emit("event=spawn count=%d stage=%d money=%d", g_Spawns, g_Stage, get_member(index, m_iAccount));
    return HC_CONTINUE;
}

public ScenarioMessage(message, destination, recipient)
{
    if (!g_Active || get_msg_args() != 5 || get_msg_argtype(1) != ARG_BYTE ||
        get_msg_argtype(2) != ARG_BYTE || get_msg_arg_int(1) != 27 || get_msg_arg_int(2) != 3)
        return PLUGIN_CONTINUE;
    g_Round = get_msg_arg_int(3);
    Emit("event=round round=%d total=%d", g_Round, get_msg_arg_int(4));
    return PLUGIN_CONTINUE;
}

WorldSnapshot(&count, &Float:wallHealth)
{
    new entity; count = 0; wallHealth = 0.0;
    while ((entity = rg_find_ent_by_class(entity, "monster_entity")))
        if (pev_valid(entity) == 2 && get_entvar(entity, var_solid) != SOLID_NOT &&
            Float:get_entvar(entity, var_health) > 0.0) count++;
    entity = 0;
    while ((entity = rg_find_ent_by_class(entity, "zbsbreak")))
        if (pev_valid(entity) == 2 && get_entvar(entity, var_solid) != SOLID_NOT &&
            Float:get_entvar(entity, var_health) > 0.0) wallHealth += Float:get_entvar(entity, var_health);
}

PlaceInFirstTrigger()
{
    // Actual zs_deadend BSP *136/id1 bounds: (960,-1968,0)-(1216,-1488,448).
    // Only move this owned fixture player; the real Map scanner activates
    // its original waves. Player traversal/navigation is outside this test.
    new Float:points[3][3] = {{1088.0,-1728.0,36.0},{1088.0,-1600.0,36.0},{1152.0,-1800.0,36.0}};
    for (new index = 0; index < sizeof(points); index++)
    {
        new trace = create_tr2();
        engfunc(EngFunc_TraceHull, points[index], points[index], 0, HULL_HUMAN, g_Player, trace);
        new blocked = get_tr2(trace, TR_StartSolid) || get_tr2(trace, TR_AllSolid);
        free_tr2(trace);
        if (blocked) continue;
        engfunc(EngFunc_SetOrigin, g_Player, points[index]); g_Placed = true;
        Emit("event=fixture_position trigger=1 model=*136 x=%.3f y=%.3f z=%.3f stage=%d",
            points[index][0], points[index][1], points[index][2], g_Stage);
        return;
    }
    Stop("all_trigger_positions_blocked");
}

Request(next)
{
    new active, requested, maximum, mapid;
    if (!csol_scenario_difficulty_info(active, requested, maximum, mapid) ||
        csol_scenario_replay(mapid + 1) || csol_scenario_difficulty_request(mapid, maximum + 1) ||
        !csol_scenario_difficulty_request(mapid, next)) { Stop("request_validation_failed"); return; }
    set_member(g_Player, m_iAccount, 123);
    g_LastSpawns = g_Spawns; g_RequestedAt = get_gametime();
    new bool:accepted = csol_scenario_replay(mapid);
    new bool:duplicate = csol_scenario_replay(mapid);
    if (!csol_scenario_difficulty_info(active, requested, maximum, mapid))
    { Stop("queued_info_failed"); return; }
    Emit("event=request next=%d active=%d requested=%d accepted=%d duplicate=%d spawns=%d",
        next, active, requested, accepted, duplicate, g_Spawns);
    if (!accepted || duplicate || requested != next || g_Spawns != g_LastSpawns)
    { Stop("queue_contract_failed"); return; }
    g_Stage++;
}

public Frame()
{
    if (!g_Active || g_Cleaning) return FMRES_IGNORED;
    if (!get_pcvar_num(g_Enabled) || !Owned()) { Stop("ownership_or_enable_changed"); return FMRES_IGNORED; }
    for (new index = 1; index <= MaxClients; index++)
        if (is_user_connected(index) && index != g_Player) { Stop("other_client_joined"); return FMRES_IGNORED; }
    new Float:now = get_gametime();
    if (now < g_FirstTime || now >= g_Deadline) { Stop("clock_or_timeout"); return FMRES_IGNORED; }
    if (g_Protect && is_user_alive(g_Player))
    {
        // Only fixture survival is altered. Real monster and wall damage remains native.
        set_entvar(g_Player, var_takedamage, DAMAGE_NO); g_Protect = false;
    }
    if (!g_Placed && is_user_alive(g_Player) && !get_member_game(m_bFreezePeriod))
    {
        PlaceInFirstTrigger();
        if (!g_Active) return FMRES_IGNORED;
    }
    if (now >= g_NextMove)
    {
        g_NextMove = now + 0.01;
        new Float:angles[3]; get_entvar(g_Player, var_v_angle, angles);
        engfunc(EngFunc_RunPlayerMove, g_Player, angles, 0.0, 0.0, 0.0, 0, 0, 10);
    }
    if (now < g_NextSample) return FMRES_IGNORED;
    g_NextSample = now + 0.5;
    new active, requested, maximum, mapid, monsters; new Float:walls;
    if (!csol_scenario_difficulty_info(active, requested, maximum, mapid) || mapid != g_MapId)
    { Stop("active_info_failed"); return FMRES_IGNORED; }
    WorldSnapshot(monsters, walls);
    Emit("event=sample stage=%d active=%d requested=%d freeze=%d spawns=%d monsters=%d walls=%.3f money=%d",
        g_Stage, active, requested, get_member_game(m_bFreezePeriod), g_Spawns, monsters, walls,
        get_member(g_Player, m_iAccount));
    if (g_Stage == 0 && g_Round == 1 && is_user_alive(g_Player) &&
        !get_member_game(m_bFreezePeriod) && monsters > 0)
    {
        g_InitialMoney = get_member(g_Player, m_iAccount); g_InitialWalls = walls;
        Request(g_Maximum);
    }
    else if (g_Stage == 1 || g_Stage == 3)
    {
        new expected = g_Stage == 1 ? g_Maximum : 1;
        if (active != expected || g_Spawns <= g_LastSpawns) return FMRES_IGNORED;
        if (g_Spawns != g_LastSpawns + 1 || get_member(g_Player, m_iAccount) != g_InitialMoney ||
            g_Round != 1 || !is_user_alive(g_Player) || !get_member_game(m_bFreezePeriod))
        { Stop("reset_contract_failed"); return FMRES_IGNORED; }
        Emit("event=committed difficulty=%d spawns=%d delay=%.3f money=%d walls=%.3f initialwalls=%.3f",
            active, g_Spawns, now - g_RequestedAt, get_member(g_Player, m_iAccount), walls, g_InitialWalls);
        if (g_Stage == 3 && floatabs(walls - g_InitialWalls) > 0.01)
        { Stop("wall_health_not_restored"); return FMRES_IGNORED; }
        g_Stage++;
    }
    else if ((g_Stage == 2 || g_Stage == 4) && !get_member_game(m_bFreezePeriod) && monsters > 0)
    {
        if (g_Stage == 2) Request(1);
        else { Emit("event=success low_high_low=1 same_map=1 resumed_waves=1 native_api=1"); Stop("completed"); }
    }
    return FMRES_IGNORED;
}
