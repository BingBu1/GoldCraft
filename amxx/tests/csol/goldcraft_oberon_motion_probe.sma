// SPDX-License-Identifier: GPL-3.0-or-later
// Explicit local-server diagnostic. Never changes a monster or GameRules.
// One owned fake CT enters the real goal trigger, then the normal second
// round and wave scheduler spawn Oberon. This is not network acceptance.
#include <amxmodx>
#include <fakemeta>
#include <reapi>
#include <hamsandwich>

#define LOG_FILE "goldcraft_oberon_motion_probe.log"
#define BOSS_MODEL "models/monster/zbs_bossL_big02.mdl"

new g_Enabled, g_Player, g_Userid, g_Serial, g_Boss, g_BossSerial, g_Round;
new g_Session, g_Sample, g_Spawns, g_DamageCalls;
new bool:g_Active, bool:g_Cleaning, bool:g_PlacedGoal, bool:g_PendingHealth, bool:g_Hooked;
new Float:g_Deadline, Float:g_NextMove, Float:g_NextSample, Float:g_FireUntil, Float:g_WalkUntil;
new Float:g_WalkYaw, Float:g_WalkForward, Float:g_PreviousOrigin[3], Float:g_FirstOrigin[3];
new Float:g_LastDamage, Float:g_BossSeenAt;

public plugin_init()
{
    register_plugin("CSOL Oberon motion probe", "0.2.2", "GoldCraft contributors");
    g_Enabled = create_cvar("csol_oberon_probe", "0");
    register_srvcmd("csol_oberon_probe_begin", "Begin");
    register_srvcmd("csol_oberon_probe_status", "Status");
    register_srvcmd("csol_oberon_probe_fire", "Fire");
    register_srvcmd("csol_oberon_probe_walk", "Walk");
    register_srvcmd("csol_oberon_probe_end", "End");
    register_srvcmd("csol_oberon_probe_hulls", "WorldHulls");
    RegisterHookChain(RG_CBasePlayer_Spawn, "Spawned", true);
    register_forward(FM_StartFrame, "Frame", false);
    new id = get_user_msgid("MPToCL");
    if (id) register_message(id, "ScenarioMessage");
}

Emit(const format[], any:...)
{
    new text[1000]; vformat(text, charsmax(text), format, 2);
    server_print("[Oberon probe] session=%d time=%.3f %s", g_Session, get_gametime(), text);
    log_to_file(LOG_FILE, "session=%d time=%.3f %s", g_Session, get_gametime(), text);
}

bool:Owned()
{
    return g_Player >= 1 && g_Player <= MaxClients && g_Userid > 0 &&
        is_user_connected(g_Player) && get_user_userid(g_Player) == g_Userid &&
        pev_valid(g_Player) && pev_serial(g_Player) == g_Serial;
}

bool:Context()
{
    new map[32]; get_mapname(map, charsmax(map));
    return equal(map, "zs_deadend") && MaxClients == 32 && get_cvar_num("csol_game_mode") == 2;
}

bool:Boss(entity)
{
    if (entity <= MaxClients || pev_valid(entity) != 2) return false;
    new classname[32], model[96];
    get_entvar(entity, var_classname, classname, charsmax(classname));
    get_entvar(entity, var_model, model, charsmax(model));
    return equal(classname, "monster_entity") && equal(model, BOSS_MODEL) &&
        get_entvar(entity, var_solid) != SOLID_NOT && Float:get_entvar(entity, var_health) > 0.0;
}

Cleanup()
{
    g_Cleaning = true; g_Active = false; g_FireUntil = g_WalkUntil = 0.0;
    if (Owned())
    {
        new slot = g_Player, userid = g_Userid;
        rh_drop_client(slot, "Oberon motion probe complete");
        Emit("event=cleanup slot=%d userid=%d removed=%d", slot, userid,
            !is_user_connected(slot) || get_user_userid(slot) != userid);
    }
    g_Player = g_Userid = g_Serial = g_Boss = g_BossSerial = 0;
    g_Cleaning = false;
}

Stop(const reason[])
{ Emit("event=end reason=%s samples=%d damage_calls=%d acceptance=unreviewed", reason, g_Sample, g_DamageCalls); Cleanup(); }
public plugin_end() { Cleanup(); }
public End() { if (g_Active) Stop("operator_stop"); return PLUGIN_HANDLED; }

public client_disconnected(index)
{
    if (index != g_Player) return;
    if (g_Active && !g_Cleaning) { g_Active = false; Emit("event=end reason=owned_player_disconnected"); }
    g_Player = g_Userid = g_Serial = 0;
}

public Begin()
{
    if (!get_pcvar_num(g_Enabled) || g_Active) return PLUGIN_HANDLED;
    if (!Context() || get_member_game(m_bGameStarted) || get_cvar_num("sypb_quota") != 0)
    { server_print("[Oberon probe] needs fresh empty zs_deadend/mode2/32 slots/SyPB0"); return PLUGIN_HANDLED; }
    for (new index = 1; index <= MaxClients; index++)
        if (is_user_connected(index)) return PLUGIN_HANDLED;
    if (!get_user_msgid("MPToCL") || !IsHamValid(Ham_TakeDamage)) return PLUGIN_HANDLED;
    g_Session = get_systime(); g_Round = g_Sample = g_Spawns = g_DamageCalls = 0;
    g_PlacedGoal = g_PendingHealth = false; g_LastDamage = g_BossSeenAt = 0.0;
    g_NextMove = g_NextSample = g_FireUntil = g_WalkUntil = 0.0;
    g_Active = true; g_Deadline = get_gametime() + 240.0;
    Emit("event=begin mode=2 fake_clients=1 round_transition=goal100 sample_hz=5 no_boss_writes=1");
    g_Player = engfunc(EngFunc_CreateFakeClient, "GC_Oberon_Probe");
    if (g_Player < 1 || g_Player > MaxClients || !is_user_connected(g_Player) || !is_user_bot(g_Player))
    { Stop("fake_allocation_failed"); return PLUGIN_HANDLED; }
    g_Userid = get_user_userid(g_Player);
    if (pev_valid(g_Player) == 2) engfunc(EngFunc_FreeEntPrivateData, g_Player);
    g_Serial = pev_serial(g_Player);
    set_pev(g_Player, pev_flags, pev(g_Player, pev_flags) | FL_FAKECLIENT | FL_CLIENT);
    dllfunc(DLLFunc_ClientPutInServer, g_Player);
    if (!Owned() || pev_valid(g_Player) != 2) { Stop("fake_private_object_failed"); return PLUGIN_HANDLED; }
    set_entvar(g_Player, var_flags, get_entvar(g_Player, var_flags) | FL_FAKECLIENT | FL_CLIENT);
    set_member(g_Player, m_iMenu, Menu_ChooseTeam);
    set_member(g_Player, m_iJoiningState, PICKINGTEAM);
    engclient_cmd(g_Player, "jointeam", "2");
    if (get_member(g_Player, m_iTeam) != TEAM_CT || get_member(g_Player, m_iMenu) != Menu_ChooseAppearance)
    { Stop("normal_team_selection_failed"); return PLUGIN_HANDLED; }
    engclient_cmd(g_Player, "specmode", "3");
    engclient_cmd(g_Player, "joinclass", "1");
    Emit("event=selected slot=%d userid=%d serial=%d", g_Player, g_Userid, g_Serial);
    return PLUGIN_HANDLED;
}

public Spawned(index)
{
    if (!g_Active || index != g_Player || !Owned() || pev_valid(index) != 2 ||
        !IsReapiHookOriginalWasCalled(RG_CBasePlayer_Spawn)) return HC_CONTINUE;
    // CBasePlayer::Spawn clears FL_FAKECLIENT; restore only the exact owned
    // connection, matching the existing ready/respawn diagnostic fixtures.
    set_entvar(index, var_flags, get_entvar(index, var_flags) | FL_FAKECLIENT | FL_CLIENT);
    g_Spawns++; g_PendingHealth = true;
    Emit("event=natural_spawn slot=%d count=%d round=%d", index, g_Spawns, g_Round);
    return HC_CONTINUE;
}

public ScenarioMessage(message, destination, recipient)
{
    if (!g_Active || get_msg_args() < 2 || get_msg_argtype(1) != ARG_BYTE || get_msg_argtype(2) != ARG_BYTE)
        return PLUGIN_CONTINUE;
    if (get_msg_arg_int(1) == 27 && get_msg_arg_int(2) == 3 && get_msg_args() == 5 &&
        get_msg_argtype(3) == ARG_BYTE && get_msg_argtype(4) == ARG_BYTE && get_msg_argtype(5) == ARG_LONG)
    {
        new round = get_msg_arg_int(3);
        if (round != g_Round) Emit("event=round_message round=%d total=%d recipient=%d destination=%d", round, get_msg_arg_int(4), recipient, destination);
        g_Round = round;
    }
    return PLUGIN_CONTINUE;
}

PlaceInGoal()
{
    // CollectMap snapshots BSP *138's goal100 bounds and removes its marker.
    // This point is inside the verified raw BSP box (3072,-304,0)-(3456,64,64).
    // Let the live MapSession scanner consume its preserved bounds normally.
    new Float:origin[3]; origin[0] = 3264.0; origin[1] = -120.0; origin[2] = 36.0;
    new trace = create_tr2();
    engfunc(EngFunc_TraceHull, origin, origin, 0, HULL_HUMAN, g_Player, trace);
    new blocked = get_tr2(trace, TR_StartSolid) || get_tr2(trace, TR_AllSolid);
    free_tr2(trace);
    if (blocked) { Stop("goal100_player_hull_blocked"); return; }
    Emit("event=mutation operation=owned_player_goal_setup bsp_model=*138 origin=3264,-120,36 rule_write=0");
    engfunc(EngFunc_SetOrigin, g_Player, origin);
    g_PlacedGoal = true;
}

public Fire()
{
    if (!g_Active || !Owned() || !is_user_alive(g_Player) || g_Round != 2 || !Boss(g_Boss) ||
        pev_serial(g_Boss) != g_BossSerial || get_member_game(m_bFreezePeriod)) return PLUGIN_HANDLED;
    engclient_cmd(g_Player, "weapon_usp");
    g_FireUntil = get_gametime() + 1.0;
    Emit("event=command operation=owned_native_usp_fire seconds=1 boss=%d", g_Boss);
    return PLUGIN_HANDLED;
}

public Walk()
{
    if (!g_Active || !Owned() || g_Round != 2 || read_argc() != 4) return PLUGIN_HANDLED;
    new value[24]; read_argv(1, value, charsmax(value)); new Float:yaw = str_to_float(value);
    read_argv(2, value, charsmax(value)); new Float:move = str_to_float(value);
    read_argv(3, value, charsmax(value)); new Float:seconds = str_to_float(value);
    if (yaw < -360.0 || yaw > 360.0 || move < -250.0 || move > 250.0 || seconds <= 0.0 || seconds > 8.0)
        return PLUGIN_HANDLED;
    g_WalkYaw = yaw; g_WalkForward = move; g_WalkUntil = get_gametime() + seconds;
    Emit("event=command operation=owned_RunPlayerMove yaw=%.2f forward=%.2f seconds=%.2f", yaw, move, seconds);
    return PLUGIN_HANDLED;
}

public Frame()
{
    if (!g_Active || g_Cleaning) return FMRES_IGNORED;
    if (!get_pcvar_num(g_Enabled) || !Context()) { Stop("context_or_enable_changed"); return FMRES_IGNORED; }
    for (new index = 1; index <= MaxClients; index++)
        if (is_user_connected(index) && index != g_Player)
        { Stop("other_client_joined"); return FMRES_IGNORED; }
    if (!Owned()) { Stop("owned_identity_lost"); return FMRES_IGNORED; }
    new Float:now = get_gametime();
    if (now >= g_Deadline) { Stop("240_second_capture_limit"); return FMRES_IGNORED; }
    if (g_PendingHealth && is_user_alive(g_Player))
    {
        // Keep actual takedamage, native skill effects and trace targetability.
        // High HP is explicit test-player setup, not damage-amount acceptance.
        Emit("event=mutation operation=owned_player_survival_health old=%.2f new=100000 damage_mode=%d",
            Float:get_entvar(g_Player, var_health), get_entvar(g_Player, var_takedamage));
        set_entvar(g_Player, var_health, 100000.0); g_PendingHealth = false;
    }
    if (g_Round == 1 && !g_PlacedGoal && is_user_alive(g_Player) &&
        !get_member_game(m_bFreezePeriod) && !get_member_game(m_bRoundTerminating)) PlaceInGoal();
    if (!g_Active) return FMRES_IGNORED;
    if (now >= g_NextMove && pev_valid(g_Player) == 2)
    {
        g_NextMove = now + 0.01;
        new Float:angles[3], Float:move, buttons;
        get_entvar(g_Player, var_v_angle, angles);
        if (now < g_WalkUntil) { angles[0] = 0.0; angles[1] = g_WalkYaw; move = g_WalkForward; }
        if (now < g_FireUntil && Boss(g_Boss) && pev_serial(g_Boss) == g_BossSerial)
        {
            new Float:from[3], Float:view[3], Float:to[3];
            get_entvar(g_Player, var_origin, from); get_entvar(g_Player, var_view_ofs, view);
            get_entvar(g_Boss, var_origin, to);
            to[2] += 60.0;
            for (new axis = 0; axis < 3; axis++) to[axis] -= from[axis] + view[axis];
            engfunc(EngFunc_VecToAngles, to, angles); angles[0] = -angles[0]; buttons = IN_ATTACK;
        }
        engfunc(EngFunc_RunPlayerMove, g_Player, angles, move, 0.0, 0.0, buttons, 0, 10);
    }
    if (now >= g_NextSample) { g_NextSample = now + 0.2; Status(); }
    return FMRES_IGNORED;
}

public Status()
{
    if (!g_Active) return PLUGIN_HANDLED;
    g_Sample++;
    if (!Boss(g_Boss) || pev_serial(g_Boss) != g_BossSerial)
    {
        g_Boss = 0;
        new entity;
        while ((entity = rg_find_ent_by_class(entity, "monster_entity")))
            if (Boss(entity)) { g_Boss = entity; break; }
        if (g_Boss)
        {
            g_BossSerial = pev_serial(g_Boss); g_BossSeenAt = get_gametime();
            get_entvar(g_Boss, var_origin, g_FirstOrigin); get_entvar(g_Boss, var_origin, g_PreviousOrigin);
            Emit("event=boss_observed entity=%d serial=%d round=%d native_wave_spawn=1", g_Boss, g_BossSerial, g_Round);
            if (!g_Hooked)
            {
                RegisterHamFromEntity(Ham_TakeDamage, g_Boss, "BossDamaged", true);
                g_Hooked = true;
            }
        }
    }
    if (!g_Boss)
    {
        if (!(g_Sample % 5)) Emit("event=waiting round=%d started=%d freeze=%d terminating=%d player_alive=%d goal_placed=%d",
            g_Round, get_member_game(m_bGameStarted), get_member_game(m_bFreezePeriod),
            get_member_game(m_bRoundTerminating), Owned() && is_user_alive(g_Player), g_PlacedGoal);
        return PLUGIN_HANDLED;
    }
    new Float:origin[3], Float:velocity[3], Float:mins[3], Float:maxs[3], Float:player[3], Float:delta[3];
    get_entvar(g_Boss, var_origin, origin); get_entvar(g_Boss, var_velocity, velocity);
    get_entvar(g_Boss, var_mins, mins); get_entvar(g_Boss, var_maxs, maxs);
    if (Owned()) get_entvar(g_Player, var_origin, player);
    for (new axis = 0; axis < 3; axis++) delta[axis] = origin[axis] - g_PreviousOrigin[axis];
    Emit("event=boss sample=%d entity=%d serial=%d round=%d age=%.3f origin=%.3f,%.3f,%.3f velocity=%.3f,%.3f,%.3f delta=%.3f,%.3f,%.3f displacement=%.3f,%.3f,%.3f movetype=%d solid=%d flags=%d ground=%d sequence=%d frame=%.3f health=%.3f maxspeed=%.3f bbox=%.1f,%.1f,%.1f:%.1f,%.1f,%.1f after_hit=%.3f player=%.3f,%.3f,%.3f player_health=%.3f",
        g_Sample, g_Boss, g_BossSerial, g_Round, get_gametime() - g_BossSeenAt,
        origin[0], origin[1], origin[2], velocity[0], velocity[1], velocity[2], delta[0], delta[1], delta[2],
        origin[0]-g_FirstOrigin[0], origin[1]-g_FirstOrigin[1], origin[2]-g_FirstOrigin[2],
        get_entvar(g_Boss, var_movetype), get_entvar(g_Boss, var_solid), get_entvar(g_Boss, var_flags),
        get_entvar(g_Boss, var_groundentity), get_entvar(g_Boss, var_sequence), Float:get_entvar(g_Boss, var_frame),
        Float:get_entvar(g_Boss, var_health), Float:get_entvar(g_Boss, var_maxspeed), mins[0], mins[1], mins[2],
        maxs[0], maxs[1], maxs[2], g_LastDamage > 0.0 ? get_gametime()-g_LastDamage : -1.0,
        player[0], player[1], player[2], Owned() ? (Float:get_entvar(g_Player, var_health)) : 0.0);
    for (new axis = 0; axis < 3; axis++) g_PreviousOrigin[axis] = origin[axis];
    if (!(g_Sample % 5))
    {
        new Float:target[3];
        for (new axis = 0; axis < 3; axis++) target[axis] = origin[axis];
        target[2] -= 72.0;
        MonsterTrace("boss_down", g_Boss, origin, target);
        for (new axis = 0; axis < 3; axis++) target[axis] = origin[axis] + velocity[axis] * 0.2;
        MonsterTrace("boss_motion", g_Boss, origin, target);
    }
    return PLUGIN_HANDLED;
}

EmitTraceResult(const label[], trace)
{
    new Float:fraction, Float:end[3], Float:normal[3], map[32];
    get_tr2(trace, TR_flFraction, fraction);
    get_tr2(trace, TR_vecEndPos, end); get_tr2(trace, TR_vecPlaneNormal, normal);
    get_mapname(map, charsmax(map));
    Emit("event=trace label=%s map=%s centering=%d startsolid=%d allsolid=%d hit=%d fraction=%.6f end=%.3f,%.3f,%.3f normal=%.3f,%.3f,%.3f",
        label, map, get_cvar_num("sv_rehlds_hull_centering"), get_tr2(trace, TR_StartSolid),
        get_tr2(trace, TR_AllSolid), get_tr2(trace, TR_pHit), fraction,
        end[0], end[1], end[2], normal[0], normal[1], normal[2]);
}

MonsterTrace(const label[], entity, const Float:from[3], const Float:to[3])
{
    new trace = create_tr2();
    // The actual engine function reads this entity's bbox and calls SV_Move.
    // It never moves or changes the observed entity.
    engfunc(EngFunc_TraceMonsterHull, entity, from, to, 0, entity, trace);
    EmitTraceResult(label, trace); free_tr2(trace);
}

public WorldHulls()
{
    Emit("event=world_probe_requested enabled=%d maxclients=%d", get_pcvar_num(g_Enabled), MaxClients);
    if (!get_pcvar_num(g_Enabled) || MaxClients != 32)
    { Emit("event=world_probe_rejected reason=disabled_or_capacity"); return PLUGIN_HANDLED; }
    for (new index = 1; index <= MaxClients; index++)
        if (is_user_connected(index) && (!Owned() || index != g_Player))
        { Emit("event=world_probe_rejected reason=other_client index=%d bot=%d", index, is_user_bot(index)); return PLUGIN_HANDLED; }
    // AMXX's pinned HLTypeConversion rejects non-player edicts without a
    // private object. Named info_target supplies that object; it is never
    // spawned and only supplies a controlled bbox to the read-only trace.
    new entity = engfunc(EngFunc_CreateNamedEntity, engfunc(EngFunc_AllocString, "info_target"));
    if (entity <= MaxClients || !pev_valid(entity))
    { Emit("event=world_probe_rejected reason=allocation entity=%d", entity); return PLUGIN_HANDLED; }
    new Float:mins[3] = {-55.0, -55.0, -32.0}, Float:maxs[3] = {55.0, 55.0, 160.0};
    engfunc(EngFunc_SetSize, entity, mins, maxs);
    new spawn, count, Float:from[3], Float:to[3], label[48];
    while ((spawn = rg_find_ent_by_class(spawn, "info_player_start")) && count < 8)
    {
        count++; get_entvar(spawn, var_origin, from);
        for (new axis = 0; axis < 3; axis++) to[axis] = from[axis];
        to[2] -= 512.0;
        Emit("event=world_probe spawn=%d index=%d from=%.3f,%.3f,%.3f bbox=-55,-55,-32:55,55,160", spawn, count, from[0], from[1], from[2]);
        for (new hull = HULL_POINT; hull <= HULL_LARGE; hull++)
        {
            new trace = create_tr2();
            engfunc(EngFunc_TraceHull, from, to, 1, hull, entity, trace);
            formatex(label, charsmax(label), "spawn%d_hull%d_down", count, hull);
            EmitTraceResult(label, trace); free_tr2(trace);
        }
        for (new axis = 0; axis < 2; axis++)
            for (new direction = -1; direction <= 1; direction += 2)
            {
                for (new i = 0; i < 3; i++) to[i] = from[i];
                to[axis] += float(direction) * 256.0;
                formatex(label, charsmax(label), "spawn%d_axis%d_dir%d", count, axis, direction);
                MonsterTrace(label, entity, from, to);
            }
    }
    engfunc(EngFunc_RemoveEntity, entity);
    Emit("event=world_probe_end spawns=%d temporary_entity_removed=1", count);
    return PLUGIN_HANDLED;
}

public BossDamaged(entity, inflictor, attacker, Float:damage, bits)
{
    if (!g_Active || entity != g_Boss || pev_serial(entity) != g_BossSerial || !Boss(entity)) return HAM_IGNORED;
    new accepted; GetOrigHamReturnInteger(accepted);
    g_DamageCalls++; g_LastDamage = get_gametime();
    Emit("event=boss_damage_post entity=%d attacker=%d owned_attacker=%d damage=%.3f bits=%d accepted=%d original=%d health=%.3f sequence=%d",
        entity, attacker, Owned() && attacker == g_Player, damage, bits, accepted,
        GetHamReturnStatus() < HAM_SUPERCEDE, Float:get_entvar(entity, var_health), get_entvar(entity, var_sequence));
    return HAM_IGNORED;
}
