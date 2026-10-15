// Opt-in diagnostics for real TNT on the production GameDLL/ReHLDS runtime.
// No synthetic edits, round-end calls, or geometry replacements are provided.
#include <amxmodx>
#include <fakemeta>
#include <reapi>

new gEnabled, gBot, gEnds, gLastEvent, gLastStatus;
new Float:gLastEnd;

public plugin_init()
{
    register_plugin("GoldCraft TNT runtime fixture", "0.1.0", "GoldCraft contributors");
    gEnabled = create_cvar("mc_tnt_test", "0");
    register_srvcmd("gc_tnt_trace", "Trace");
    register_srvcmd("gc_tnt_create", "CreatePlayer");
    register_srvcmd("gc_tnt_move", "Move");
    register_srvcmd("gc_tnt_round", "RoundState");
    RegisterHookChain(RG_RoundEnd, "RoundAfter", true);
}

public RoundAfter(WinStatus:status, ScenarioEventEndRound:event, Float:delay)
{
    if (get_pcvar_num(gEnabled) && GetHookChainReturn(ATYPE_BOOL)) {
        gEnds++; gLastEvent = _:event; gLastStatus = _:status; gLastEnd = get_gametime();
        server_print("[GC TNT] accepted_end=%d event=%d status=%d time=%.6f delay=%.6f",
            gEnds, gLastEvent, gLastStatus, gLastEnd, delay);
    }
    return HC_CONTINUE;
}

public RoundState()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    server_print("[GC TNT] time=%.6f round_start=%.6f seconds=%d win=%d terminating=%d ends=%d event=%d status=%d end_time=%.6f",
        get_gametime(), Float:get_member_game(m_fRoundStartTime), get_member_game(m_iRoundTimeSecs),
        get_member_game(m_iRoundWinStatus), get_member_game(m_bRoundTerminating),
        gEnds, gLastEvent, gLastStatus, gLastEnd);
    return PLUGIN_HANDLED;
}

stock bool:ReadVector(first, Float:vector[3])
{
    new arg[32];
    for (new i = 0; i < 3; i++) {
        read_argv(first + i, arg, charsmax(arg)); vector[i] = str_to_float(arg);
        if (floatabs(vector[i]) > 8192.0) return false;
    }
    return true;
}

public Trace()
{
    if (!get_pcvar_num(gEnabled) || read_argc() != 8) return PLUGIN_HANDLED;
    new arg[16]; read_argv(1, arg, charsmax(arg)); new hull = str_to_num(arg);
    if (hull < 0 || hull > 3) return PLUGIN_HANDLED;
    new Float:start[3], Float:end[3];
    if (!ReadVector(2, start) || !ReadVector(5, end)) return PLUGIN_HANDLED;
    new trace = create_tr2(), Float:point[3], Float:normal[3], Float:fraction;
    if (!hull) engfunc(EngFunc_TraceLine, start, end, IGNORE_MONSTERS, 0, trace);
    else engfunc(EngFunc_TraceHull, start, end, IGNORE_MONSTERS, hull, 0, trace);
    get_tr2(trace, TR_vecEndPos, point); get_tr2(trace, TR_vecPlaneNormal, normal);
    // The pinned Fakemeta API writes float members through its third argument.
    get_tr2(trace, TR_flFraction, fraction);
    server_print("[GC TNT] hull=%d fraction=%.9f start=%d all=%d hit=%d end=%.6f,%.6f,%.6f normal=%.6f,%.6f,%.6f contents=%d",
        hull, fraction, get_tr2(trace, TR_StartSolid), get_tr2(trace, TR_AllSolid),
        get_tr2(trace, TR_pHit), point[0], point[1], point[2], normal[0], normal[1], normal[2],
        engfunc(EngFunc_PointContents, end));
    free_tr2(trace);
    return PLUGIN_HANDLED;
}

public CreatePlayer()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    if (!is_user_connected(gBot)) {
        gBot = engfunc(EngFunc_CreateFakeClient, "GC_TNT_Movement");
        if (!gBot) return PLUGIN_HANDLED;
        new reject[128];
        dllfunc(DLLFunc_ClientConnect, gBot, "GC_TNT_Movement", "127.0.0.1", reject);
        dllfunc(DLLFunc_ClientPutInServer, gBot);
        set_member(gBot, m_iJoiningState, JOINED);
        set_member(gBot, m_bJustConnected, false);
        set_member(gBot, m_iMenu, Menu_OFF);
    }
    rg_set_user_team(gBot, TEAM_CT, MODEL_CT_URBAN);
    if (!is_user_alive(gBot)) rg_round_respawn(gBot);
    set_entvar(gBot, var_flags, get_entvar(gBot, var_flags) | FL_FAKECLIENT);
    // Combat is outside this movement fixture; do not let nearby ZP Bots kill it.
    set_entvar(gBot, var_takedamage, DAMAGE_NO);
    server_print("[GC TNT] player=%d alive=%d", gBot, is_user_alive(gBot));
    return PLUGIN_HANDLED;
}

public Move()
{
    if (!get_pcvar_num(gEnabled) || read_argc() != 8 || !is_user_alive(gBot)
        || !(get_entvar(gBot, var_flags) & FL_FAKECLIENT)) return PLUGIN_HANDLED;
    new Float:origin[3], Float:angles[3], Float:velocity[3], Float:zero[3], arg[32];
    if (!ReadVector(1, origin)) return PLUGIN_HANDLED;
    read_argv(4, arg, charsmax(arg)); angles[1] = str_to_float(arg);
    read_argv(5, arg, charsmax(arg)); new Float:speed = str_to_float(arg);
    read_argv(6, arg, charsmax(arg)); new frames = str_to_num(arg);
    read_argv(7, arg, charsmax(arg)); new duck = str_to_num(arg);
    if (floatabs(angles[1]) > 360.0 || floatabs(speed) > 400.0 || frames < 1 || frames > 100
        || (duck != 0 && duck != 1)) return PLUGIN_HANDLED;
    velocity[0] = floatcos(angles[1], degrees) * speed;
    velocity[1] = floatsin(angles[1], degrees) * speed;
    set_entvar(gBot, var_velocity, velocity); set_entvar(gBot, var_basevelocity, zero);
    new flags = (get_entvar(gBot, var_flags) | FL_FAKECLIENT) & ~(FL_ONGROUND | FL_FROZEN | FL_DUCKING);
    if (duck) flags |= FL_DUCKING;
    set_entvar(gBot, var_flags, flags);
    set_entvar(gBot, var_movetype, MOVETYPE_WALK); set_entvar(gBot, var_solid, SOLID_SLIDEBOX);
    set_entvar(gBot, var_maxspeed, 400.0);
    new Float:low[3] = {-16.0, -16.0, -36.0}, Float:high[3] = {16.0, 16.0, 36.0};
    if (duck) { low[2] = -18.0; high[2] = 18.0; }
    engfunc(EngFunc_SetSize, gBot, low, high); engfunc(EngFunc_SetOrigin, gBot, origin);
    for (new i = 0; i < frames; i++)
        engfunc(EngFunc_RunPlayerMove, gBot, angles, speed, 0.0, 0.0, duck ? IN_DUCK : 0, 0, 20);
    get_entvar(gBot, var_origin, origin);
    server_print("[GC TNT] player=%d move=%.6f,%.6f,%.6f flags=%d movetype=%d solid=%d",
        gBot, origin[0], origin[1], origin[2], get_entvar(gBot, var_flags),
        get_entvar(gBot, var_movetype), get_entvar(gBot, var_solid));
    return PLUGIN_HANDLED;
}
