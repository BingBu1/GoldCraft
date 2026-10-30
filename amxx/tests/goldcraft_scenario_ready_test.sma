// SPDX-License-Identifier: GPL-3.0-or-later
// Temporary local-server observations through the pinned public ReAPI 5.29.
// This plugin never supplies spawn points or starts rounds. Capacity fixtures
// select a real character only for the fake clients created by this plugin.
#include <amxmodx>
#include <fakemeta>
#include <reapi>
#include <hamsandwich>

new g_Enabled, g_Restarts;
new g_OwnedUserid[33];
new g_OwnedSerial[33];
new bool:g_CapacityBusy;
new bool:g_ReportedInitialization;
new bool:g_Live, g_LiveSequence, g_LiveDamageDepth, g_LiveOverflow;
new g_LiveSession[49];
new bool:g_LiveHooks[4];
new bool:g_LiveMessageHooks[3];
new g_LiveDamage[64][7]; // event, entity, attacker, userid, weapon, solid, kind
new Float:g_LiveHealth[64], Float:g_LiveFrags[64];
new g_LiveTarget[64][64];
const COMBAT_SAMPLE_TASK = 672839;
new g_CombatVictim, g_CombatSample;
new g_CombatSession[49];
new g_WalkSlot, g_WalkUserid, g_WalkSerial, g_WalkCommands;
new Float:g_WalkYaw, Float:g_WalkDeadline, Float:g_WalkLast;
new g_WalkSession[49];

public plugin_init()
{
    register_plugin("CSOL selection live checks", "0.3.2", "GoldCraft contributors");
    g_Enabled = create_cvar("csol_ready_test", "0");
    register_srvcmd("csol_ready_status", "Status");
    register_srvcmd("csol_ready_force_spawn", "ForceSpawn");
    register_srvcmd("csol_ready_client", "ClientCommand");
    register_srvcmd("csol_ready_fill", "FillCapacity");
    register_srvcmd("csol_ready_clear", "ClearCapacity");
    register_srvcmd("csol_live_begin", "LiveBegin");
    register_srvcmd("csol_live_status", "LiveStatus");
    register_srvcmd("csol_live_end", "LiveEnd");
    register_srvcmd("csol_combat_bot", "CombatBot");
    register_srvcmd("csol_combat_weapon", "CombatWeapon");
    register_srvcmd("csol_combat_hit", "CombatHit");
    register_srvcmd("csol_combat_snapshot", "CombatSnapshot");
    register_srvcmd("csol_combat_walk", "CombatWalk");
    register_srvcmd("csol_combat_wall", "CombatWall");
    register_srvcmd("csol_combat_cannon", "CombatCannon");
    register_forward(FM_StartFrame, "CombatWalkFrame", false);
    RegisterHookChain(RG_CSGameRules_RestartRound, "RoundRestarted", true);
    RegisterHookChain(RG_CBasePlayer_Spawn, "LivePlayerSpawn", true);
    RegisterHookChain(RG_CBasePlayer_Killed, "LivePlayerKilled", true);
    RegisterHookChain(RG_CBasePlayer_TakeDamage, "LivePlayerDamageBefore", false);
    RegisterHookChain(RG_CBasePlayer_TakeDamage, "LivePlayerDamageAfter", true);
}

public RoundRestarted() { g_Restarts++; LiveEmit("event=restart_callback restarts=%d", g_Restarts); }

public Status()
{
    if (!get_pcvar_num(g_Enabled)) return PLUGIN_HANDLED;
    new monsters, active, entity;
    while ((entity = rg_find_ent_by_class(entity, "monster_entity")))
    {
        monsters++;
        if (get_entvar(entity, var_solid) != SOLID_NOT) active++;
    }
    server_print("[CSOL ready] ct_capacity=%d t_capacity=%d started=%d freeze=%d terminating=%d restarts=%d monsters=%d active=%d",
        get_member_game(m_iSpawnPointCount_CT), get_member_game(m_iSpawnPointCount_Terrorist),
        get_member_game(m_bGameStarted), get_member_game(m_bFreezePeriod),
        get_member_game(m_bRoundTerminating), g_Restarts, monsters, active);
    for (new index = 1; index <= MaxClients; index++)
    {
        if (!is_user_connected(index)) continue;
        new Float:origin[3]; get_entvar(index, var_origin, origin);
        server_print("[CSOL ready] slot=%d team=%d joining=%d menu=%d just_connected=%d alive=%d observer=%d solid=%d damage=%d origin=%.3f,%.3f,%.3f",
            index, get_member(index, m_iTeam), get_member(index, m_iJoiningState),
            get_member(index, m_iMenu), get_member(index, m_bJustConnected), is_user_alive(index),
            get_entvar(index, var_iuser1), get_entvar(index, var_solid), get_entvar(index, var_takedamage),
            origin[0], origin[1], origin[2]);
    }
    return PLUGIN_HANDLED;
}

public ForceSpawn()
{
    if (!get_pcvar_num(g_Enabled) || read_argc() != 2) return PLUGIN_HANDLED;
    new argument[12]; read_argv(1, argument, charsmax(argument));
    new index = str_to_num(argument);
    if (index < 1 || index > MaxClients || !is_user_connected(index)) return PLUGIN_HANDLED;
    LiveEmit("event=mutation operation=forced_respawn slot=%d", index);
    rg_round_respawn(index);
    return Status();
}

// Exercise the real GameDLL command path; this is not physical menu input.
public ClientCommand()
{
    if (!get_pcvar_num(g_Enabled) || read_argc() < 3 || read_argc() > 4) return PLUGIN_HANDLED;
    new slot[12], command[32], argument[16];
    read_argv(1, slot, charsmax(slot));
    read_argv(2, command, charsmax(command));
    read_argv(3, argument, charsmax(argument));
    new index = str_to_num(slot);
    if (index < 1 || index > MaxClients || !is_user_connected(index)) return PLUGIN_HANDLED;
    if (!equal(command, "csol_character_select") && !equal(command, "csol_character_cancel") &&
        !equal(command, "jointeam") && !equal(command, "joinclass") && !equal(command, "chooseteam"))
        return PLUGIN_HANDLED;
    LiveEmit("event=mutation operation=injected_client_command slot=%d command=%s", index, command);
    engclient_cmd(index, command, argument);
    return Status();
}

bool:OwnedConnection(index)
{
    return index >= 1 && index <= MaxClients && g_OwnedUserid[index] > 0 &&
        is_user_connected(index) && is_user_bot(index) &&
        get_user_userid(index) == g_OwnedUserid[index] &&
        pev_valid(index) != 0 && pev_serial(index) == g_OwnedSerial[index];
}

bool:OwnedClient(index)
{
    return OwnedConnection(index) && (pev(index, pev_flags) & FL_FAKECLIENT) != 0;
}

public client_disconnected(index)
{
    g_OwnedUserid[index] = 0;
    g_OwnedSerial[index] = 0;
}

CapacityIdentity(const stage[], index)
{
    if (index < 1 || index > MaxClients)
    {
        server_print("[CSOL capacity] stage=%s invalid_slot=%d", stage, index);
        return;
    }
    new valid = pev_valid(index);
    server_print("[CSOL capacity] stage=%s slot=%d owned_userid=%d userid=%d owned_serial=%d serial=%d connected=%d bot=%d flags=%d private=%d",
        stage, index, g_OwnedUserid[index], get_user_userid(index), g_OwnedSerial[index],
        valid ? pev_serial(index) : 0, is_user_connected(index), is_user_bot(index), valid ? pev(index, pev_flags) : 0, valid == 2);
    if (valid == 2)
        server_print("[CSOL capacity] stage=%s slot=%d team=%d joining=%d menu=%d just_connected=%d",
            stage, index, get_member(index, m_iTeam), get_member(index, m_iJoiningState),
            get_member(index, m_iMenu), get_member(index, m_bJustConnected));
}

// CreateFakeClient has already run AMXX's Bot userinfo/authorization forwards.
// ClientPutInServer still supplies the actual SDK CBasePlayer/CCSPlayer, Spawn,
// inventory, GoldCraft connection generation and initial observer state.
// Do not call DLLFunc_ClientConnect: pinned Fakemeta passes a literal empty
// string as the native writable reject buffer and reads an undocumented length.
CreateCapacityClient(const name[], character = 0)
{
    new index = engfunc(EngFunc_CreateFakeClient, name);
    if (!index) return 0;
    if (index < 1 || index > MaxClients || !is_user_connected(index) || !is_user_bot(index))
    {
        server_print("[CSOL capacity] FAIL fake-client allocation/AMXX initialization slot=%d", index);
        CapacityIdentity("allocation_failed", index);
        return 0;
    }
    g_OwnedUserid[index] = get_user_userid(index);
    // Like SDK CreateBot, discard a previous occupant's private object before
    // constructing the real player. Never free an existing connected client.
    if (pev_valid(index) == 2) engfunc(EngFunc_FreeEntPrivateData, index);
    g_OwnedSerial[index] = pev_serial(index);
    // The edict exists here but its new SDK private object does not yet.
    set_pev(index, pev_flags, pev(index, pev_flags) | FL_FAKECLIENT | FL_CLIENT);
    dllfunc(DLLFunc_ClientPutInServer, index);
    if (!g_ReportedInitialization) CapacityIdentity("after_put_before_bot_flag", index);
    if (pev_valid(index) != 2 || !is_user_connected(index) ||
        get_user_userid(index) != g_OwnedUserid[index])
    {
        CapacityIdentity("put_failed", index);
        return 0;
    }
    // CBasePlayer::Spawn clears FL_FAKECLIENT; CBot::Spawn restores it after
    // its base Spawn. Restore this fixture's already allocated engine Bot too.
    set_pev(index, pev_flags, pev(index, pev_flags) | FL_FAKECLIENT | FL_CLIENT);
    if (!g_ReportedInitialization)
    {
        CapacityIdentity("after_bot_flag_restored", index);
        g_ReportedInitialization = true;
    }
    if (!OwnedClient(index)) { CapacityIdentity("ownership_failed", index); return 0; }
    // This is the native Bot menu entry, not a forced JOINED/team/Spawn write.
    set_member(index, m_iMenu, Menu_ChooseTeam);
    set_member(index, m_iJoiningState, PICKINGTEAM);
    engclient_cmd(index, "jointeam", "2");
    if (get_member(index, m_iTeam) != TEAM_CT || get_member(index, m_iMenu) != Menu_ChooseAppearance)
    { CapacityIdentity("team_failed", index); return 0; }
    // Bots use the existing native class entry, which assigns a verified
    // scenario character. The extended UI command requires a human client.
    // A fake client has no HUD initialization frames before this synchronous
    // test. Set its normal camera preference through the native command.
    engclient_cmd(index, "specmode", "3");
    if (character)
    {
        new flags = get_entvar(index, var_flags), protocol[32], selected[16];
        get_user_info(index, "csol_protocol", protocol, charsmax(protocol)); num_to_str(character, selected, charsmax(selected));
        set_user_info(index, "csol_protocol", "1"); set_entvar(index, var_flags, flags & ~FL_FAKECLIENT);
        LiveEmit("event=mutation operation=scoped_character_selection stage=begin slot=%d character=%d", index, character);
        engclient_cmd(index, "csol_character_select", selected);
        set_entvar(index, var_flags, (get_entvar(index, var_flags) & ~FL_FAKECLIENT) | (flags & FL_FAKECLIENT));
        set_user_info(index, "csol_protocol", protocol);
        LiveEmit("event=mutation operation=scoped_character_selection stage=end slot=%d flags=%d", index, get_entvar(index, var_flags));
    }
    else engclient_cmd(index, "joinclass", "1");
    new Float:angles[3];
    get_entvar(index, var_v_angle, angles);
    engfunc(EngFunc_RunPlayerMove, index, angles, 0.0, 0.0, 0.0, 0, 0, 10);
    new bool:joined = OwnedClient(index) && get_member(index, m_iJoiningState) == JOINED &&
        get_member(index, m_iTeam) == TEAM_CT && !is_user_alive(index) &&
        get_entvar(index, var_solid) == SOLID_NOT && get_entvar(index, var_iuser1) != 0;
    if (!joined) CapacityIdentity("selection_failed", index);
    return joined ? index : 0;
}

public FillCapacity()
{
    if (!get_pcvar_num(g_Enabled) || g_CapacityBusy) return PLUGIN_HANDLED;
    if (MaxClients != 32 || get_cvar_num("csol_game_mode") != 2 ||
        get_member_game(m_iSpawnPointCount_CT) != 32 || get_member_game(m_bGameStarted))
    {
        server_print("[CSOL capacity] FAIL requires 32-slot scenario selection with unopened combat gate");
        return PLUGIN_HANDLED;
    }
    g_CapacityBusy = true;
    LiveEmit("event=mutation operation=capacity_fill");
    g_ReportedInitialization = false;
    new protectedUserid[33], initial, created, connected, ct, owned, alive;
    for (new index = 1; index <= MaxClients; index++)
    {
        if (!is_user_connected(index)) continue;
        initial++;
        if (!OwnedClient(index)) protectedUserid[index] = get_user_userid(index);
    }
    new bool:passed = true;
    for (new n = initial; n < MaxClients; n++)
    {
        new name[32]; formatex(name, charsmax(name), "CSOL_Capacity_%02d", n + 1);
        if (!CreateCapacityClient(name)) { passed = false; break; }
        created++;
    }
    for (new index = 1; index <= MaxClients; index++)
    {
        if (protectedUserid[index] && (!is_user_connected(index) ||
            get_user_userid(index) != protectedUserid[index])) passed = false;
        if (!is_user_connected(index)) continue;
        connected++;
        if (get_member(index, m_iTeam) == TEAM_CT) ct++;
        if (OwnedClient(index)) owned++;
        if (is_user_alive(index)) alive++;
    }
    new overflow = -1;
    if (connected == MaxClients)
    {
        // The real engine must return NULL, not overwrite an occupied slot.
        overflow = engfunc(EngFunc_CreateFakeClient, "CSOL_Capacity_Overflow");
        if (overflow) passed = false;
    }
    if (connected != 32 || ct != 32 || alive || overflow != 0 ||
        get_member_game(m_bGameStarted) || g_Restarts) passed = false;
    server_print("[CSOL capacity] %s initial=%d created=%d connected=%d ct=%d owned=%d alive=%d overflow=%d restarts=%d",
        passed ? "PASS" : "FAIL", initial, created, connected, ct, owned, alive, overflow, g_Restarts);
    g_CapacityBusy = false;
    return Status();
}

public ClearCapacity()
{
    if (!get_pcvar_num(g_Enabled) || g_CapacityBusy) return PLUGIN_HANDLED;
    g_CapacityBusy = true;
    LiveEmit("event=mutation operation=capacity_clear");
    new removed, failed;
    for (new index = 1; index <= MaxClients; index++)
    {
        if (!OwnedClient(index)) { g_OwnedUserid[index] = 0; continue; }
        new userid = g_OwnedUserid[index];
        // ReAPI calls the real SV_DropClient chain, including GameDLL cleanup
        // and AMXX disconnect/remove forwards. A raw DLL disconnect cannot
        // release the engine client slot and must not be used here.
        rh_drop_client(index, "CSOL local capacity validation complete");
        if (is_user_connected(index) && get_user_userid(index) == userid) failed++;
        else { g_OwnedUserid[index] = 0; removed++; }
    }
    server_print("[CSOL capacity] cleanup removed=%d failed=%d", removed, failed);
    g_CapacityBusy = false;
    return Status();
}

// Explicitly mutating integration fixture. It uses one plugin-owned fake
// client and actual GameDLL damage methods. It is never physical firing,
// client prediction, aim/visibility validation, or a passive observation.
public CombatBot()
{
    if (!LiveActive() || g_CapacityBusy || read_argc() > 2 ||
        get_cvar_num("csol_game_mode") != 2 || MaxClients != 32 ||
        get_member_game(m_bGameStarted)) return PLUGIN_HANDLED;
    for (new index = 1; index <= MaxClients; index++)
        if (OwnedClient(index)) { server_print("[CSOL combat] rejected=already_owns_client slot=%d", index); return PLUGIN_HANDLED; }
    LiveEmit("event=mutation operation=single_combat_client");
    g_CapacityBusy = true;
    new argument[16]; read_argv(1, argument, charsmax(argument)); new character = str_to_num(argument);
    if (character != 0 && character != 101 && character != 139) { g_CapacityBusy = false; return PLUGIN_HANDLED; }
    new index = CreateCapacityClient("CSOL_Combat_Probe", character);
    g_CapacityBusy = false;
    LiveEmit("event=combat_client slot=%d userid=%d created=%d", index, LiveUserid(index), index != 0);
    return Status();
}

public CombatWeapon()
{
    if (!LiveActive() || read_argc() != 3)
    { server_print("[CSOL combat] rejected=weapon_command session_active=%d argc=%d", LiveActive(), read_argc()); return PLUGIN_HANDLED; }
    new argument[12], name[32]; read_argv(1, argument, charsmax(argument)); read_argv(2, name, charsmax(name));
    new index = str_to_num(argument);
    if (!OwnedClient(index) || !is_user_alive(index))
    { CapacityIdentity("weapon_rejected_identity_or_dead", index); return PLUGIN_HANDLED; }
    if (!equal(name, "weapon_m4a1") && !equal(name, "weapon_ak47") && !equal(name, "weapon_glock18") && !equal(name, "weapon_cannon"))
    { server_print("[CSOL combat] rejected=weapon_not_allowed"); return PLUGIN_HANDLED; }
    LiveEmit("event=mutation operation=give_test_weapon slot=%d userid=%d name=%s", index, LiveUserid(index), name);
    // Extended inventory rejects FL_FAKECLIENT by design. For this owned
    // fixture only, simulate a negotiated human during the synchronous give;
    // restore both fields before returning to any engine frame.
    new flags, protocol[32], bool:simulate = equal(name, "weapon_cannon") != 0;
    if (simulate)
    {
        flags = get_entvar(index, var_flags); get_user_info(index, "csol_protocol", protocol, charsmax(protocol));
        set_user_info(index, "csol_protocol", "1"); set_entvar(index, var_flags, flags & ~FL_FAKECLIENT);
        LiveEmit("event=mutation operation=scoped_human_protocol stage=begin slot=%d saved_flags=%d", index, flags);
    }
    new item = rg_give_item(index, name, GT_APPEND);
    if (simulate)
    {
        set_entvar(index, var_flags, (get_entvar(index, var_flags) & ~FL_FAKECLIENT) | (flags & FL_FAKECLIENT));
        set_user_info(index, "csol_protocol", protocol);
        LiveEmit("event=mutation operation=scoped_human_protocol stage=end slot=%d flags=%d", index, get_entvar(index, var_flags));
    }
    new switched;
    if (item > MaxClients && pev_valid(item) == 2 && OwnedClient(index)) switched = rg_switch_weapon(index, item);
    LiveEmit("event=combat_weapon slot=%d entity=%d switched=%d active_weapon=%d", index, item, switched, LiveWeapon(index));
    return PLUGIN_HANDLED;
}

bool:CombatMonster(entity)
{
    if (entity <= MaxClients || pev_valid(entity) != 2) return false;
    new classname[32]; get_entvar(entity, var_classname, classname, charsmax(classname));
    return equal(classname, "monster_entity") != 0;
}

CombatEntitySample(entity, const stage[])
{
    if (!CombatMonster(entity)) { LiveEmit("event=combat_entity stage=%s entity=%d valid=0", stage, entity); return; }
    new Float:origin[3], Float:velocity[3]; get_entvar(entity, var_origin, origin); get_entvar(entity, var_velocity, velocity);
    LiveEmit("event=combat_entity stage=%s entity=%d valid=1 hp=%.4f dead=%d solid=%d effects=%d sequence=%d frame=%.4f rate=%.4f animtime=%.4f nextthink=%.4f model=%d origin=%.3f,%.3f,%.3f velocity=%.3f,%.3f,%.3f identity=slot_not_pool_lease",
        stage, entity, Float:get_entvar(entity, var_health), get_entvar(entity, var_deadflag), get_entvar(entity, var_solid),
        get_entvar(entity, var_effects), get_entvar(entity, var_sequence), Float:get_entvar(entity, var_frame),
        Float:get_entvar(entity, var_framerate), Float:get_entvar(entity, var_animtime), Float:get_entvar(entity, var_nextthink),
        get_entvar(entity, var_modelindex), origin[0], origin[1], origin[2], velocity[0], velocity[1], velocity[2]);
}

public CombatSnapshot()
{
    if (!LiveActive()) return PLUGIN_HANDLED;
    new entity, count;
    LiveEmit("event=combat_rules started=%d freeze=%d terminating=%d restarts=%d",
        get_member_game(m_bGameStarted), get_member_game(m_bFreezePeriod), get_member_game(m_bRoundTerminating), g_Restarts);
    while ((entity = rg_find_ent_by_class(entity, "monster_entity")))
    {
        if (get_entvar(entity, var_solid) == SOLID_NOT || get_entvar(entity, var_deadflag) != DEAD_NO ||
            (get_entvar(entity, var_effects) & EF_NODRAW)) continue;
        if (count++ < 64) CombatEntitySample(entity, "living");
    }
    LiveEmit("event=combat_population living=%d details_limit=64", count);
    return PLUGIN_HANDLED;
}

public CombatHit()
{
    if (!LiveActive() || read_argc() != 5 || task_exists(COMBAT_SAMPLE_TASK) ||
        !get_member_game(m_bGameStarted) || get_member_game(m_bFreezePeriod) ||
        get_member_game(m_bRoundTerminating))
    {
        server_print("[CSOL combat] rejected=hit_phase active=%d argc=%d sampling=%d started=%d freeze=%d terminating=%d",
            LiveActive(), read_argc(), task_exists(COMBAT_SAMPLE_TASK), get_member_game(m_bGameStarted),
            get_member_game(m_bFreezePeriod), get_member_game(m_bRoundTerminating));
        return PLUGIN_HANDLED;
    }
    new argument[32], method[12]; read_argv(1, argument, charsmax(argument)); new index = str_to_num(argument);
    read_argv(2, argument, charsmax(argument)); new entity = str_to_num(argument);
    read_argv(3, method, charsmax(method)); read_argv(4, argument, charsmax(argument)); new Float:damage = str_to_float(argument);
    if (!OwnedClient(index) || !is_user_alive(index))
    { CapacityIdentity("hit_rejected_identity_or_dead", index); return PLUGIN_HANDLED; }
    if (!(damage > 0.0 && damage <= 1000000.0) || (!equal(method, "trace") && !equal(method, "direct")) ||
        !IsHamValid(Ham_TraceAttack) || !IsHamValid(Ham_TakeDamage))
    { server_print("[CSOL combat] rejected=hit_arguments_or_api"); return PLUGIN_HANDLED; }
    if (!entity)
    {
        while ((entity = rg_find_ent_by_class(entity, "monster_entity")))
            if (get_entvar(entity, var_solid) != SOLID_NOT && get_entvar(entity, var_deadflag) == DEAD_NO &&
                !(get_entvar(entity, var_effects) & EF_NODRAW) && Float:get_entvar(entity, var_health) > 0.0) break;
    }
    if (!CombatMonster(entity) || get_entvar(entity, var_deadflag) != DEAD_NO ||
        get_entvar(entity, var_solid) == SOLID_NOT || Float:get_entvar(entity, var_health) <= 0.0)
    { server_print("[CSOL combat] rejected=no_live_target entity=%d", entity); return PLUGIN_HANDLED; }
    LiveAttach(entity, 0);
    LiveEmit("event=mutation operation=injected_monster_damage method=%s entity=%d attacker=%d userid=%d weapon=%d amount=%.4f bits=%d",
        method, entity, index, LiveUserid(index), LiveWeapon(index), damage, DMG_BULLET);
    CombatSnapshot(); CombatEntitySample(entity, "before");
    if (equal(method, "trace"))
    {
        new trace = create_tr2();
        new Float:position[3], Float:source[3], Float:direction[3], Float:length;
        get_entvar(entity, var_origin, position); get_entvar(index, var_origin, source);
        for (new axis; axis < 3; axis++) { direction[axis] = position[axis] - source[axis]; length += direction[axis] * direction[axis]; }
        length = floatsqroot(length);
        if (length > 0.001) for (new axis; axis < 3; axis++) direction[axis] /= length;
        else direction[0] = 1.0;
        set_tr2(trace, TR_pHit, entity); set_tr2(trace, TR_iHitgroup, HIT_GENERIC);
        set_tr2(trace, TR_flFraction, 0.5); set_tr2(trace, TR_vecEndPos, position);
        // This is a synthetic trace result, followed by the real accumulated
        // damage application. It does not claim a real bullet intersected it.
        rg_multidmg_clear();
        ExecuteHamB(Ham_TraceAttack, entity, index, damage, direction, trace, DMG_BULLET);
        rg_multidmg_apply(index, index);
        free_tr2(trace);
    }
    else ExecuteHamB(Ham_TakeDamage, entity, index, index, damage, DMG_BULLET);
    CombatEntitySample(entity, "immediate");
    g_CombatVictim = entity; g_CombatSample = 0;
    copy(g_CombatSession, charsmax(g_CombatSession), g_LiveSession);
    set_task(0.1, "CombatSample", COMBAT_SAMPLE_TASK, _, _, "a", 60);
    return PLUGIN_HANDLED;
}

// Aim an owned fake client, then invoke the actual CCannon::PrimaryAttack.
// The cannon performs its own sphere/cone/visibility traces and all damage.
// No target health, ammunition, cooldown or damage configuration is changed.
public CombatCannon()
{
    if (!LiveActive() || read_argc() != 2 || g_WalkSlot || task_exists(COMBAT_SAMPLE_TASK) ||
        !get_member_game(m_bGameStarted) || get_member_game(m_bFreezePeriod) ||
        get_member_game(m_bRoundTerminating) || !IsHamValid(Ham_Weapon_PrimaryAttack))
    { server_print("[CSOL combat] rejected=cannon_phase_or_api"); return PLUGIN_HANDLED; }
    new argument[16]; read_argv(1, argument, charsmax(argument)); new index = str_to_num(argument);
    if (!OwnedClient(index) || !is_user_alive(index))
    { CapacityIdentity("cannon_rejected_identity_or_dead", index); return PLUGIN_HANDLED; }
    new weapon = get_member(index, m_pActiveItem), name[32];
    if (weapon <= MaxClients || pev_valid(weapon) != 2) return PLUGIN_HANDLED;
    get_entvar(weapon, var_classname, name, charsmax(name));
    if (!equal(name, "weapon_cannon") || get_member(weapon, m_iId) != 144)
    { server_print("[CSOL combat] rejected=not_cannon"); return PLUGIN_HANDLED; }
    new clip = get_member(weapon, m_Weapon_iClip);
    new Float:next = get_member(weapon, m_Weapon_flNextPrimaryAttack), Float:playerNext = get_member(index, m_flNextAttack);
    if (clip <= 0 || next > 0.0 || playerNext > 0.0)
    { LiveEmit("event=cannon_wait reason=cooldown_or_clip clip=%d next=%.4f player_next=%.4f", clip, next, playerNext); return PLUGIN_HANDLED; }
    new Float:source[3], Float:view[3], Float:mins[3], Float:maxs[3], Float:body[3], Float:delta[3], Float:aim[3];
    get_entvar(index, var_origin, source); get_entvar(index, var_view_ofs, view);
    for (new axis; axis < 3; axis++) source[axis] += view[axis];
    new entities[64], serials[64], Float:health[64], count, entity, nearest, trace = create_tr2();
    new Float:distance, Float:best = 380.0;
    while ((entity = rg_find_ent_by_class(entity, "monster_entity")))
    {
        if (get_entvar(entity, var_deadflag) != DEAD_NO || get_entvar(entity, var_solid) == SOLID_NOT ||
            (get_entvar(entity, var_effects) & EF_NODRAW) || Float:get_entvar(entity, var_health) <= 0.0) continue;
        if (count < sizeof entities)
        { entities[count] = entity; serials[count] = pev_serial(entity); health[count++] = get_entvar(entity, var_health); }
        get_entvar(entity, var_absmin, mins); get_entvar(entity, var_absmax, maxs); distance = 0.0;
        for (new axis; axis < 3; axis++)
        { body[axis] = (mins[axis] + maxs[axis]) * 0.5; delta[axis] = body[axis] - source[axis]; distance += delta[axis] * delta[axis]; }
        distance = floatsqroot(distance);
        if (distance >= best) continue;
        engfunc(EngFunc_TraceLine, source, body, IGNORE_MONSTERS, index, trace);
        new Float:fraction; get_tr2(trace, TR_flFraction, fraction);
        if (fraction < 1.0 && get_tr2(trace, TR_pHit) != entity) continue;
        nearest = entity; best = distance;
        for (new axis; axis < 3; axis++) aim[axis] = delta[axis];
    }
    free_tr2(trace);
    if (!nearest) { LiveEmit("event=cannon_wait reason=no_visible_target range=380 living=%d", count); return PLUGIN_HANDLED; }
    new Float:angles[3]; engfunc(EngFunc_VecToAngles, aim, angles); angles[0] = -angles[0];
    for (new axis; axis < 2; axis++)
    {
        while (angles[axis] > 180.0) angles[axis] -= 360.0;
        while (angles[axis] < -180.0) angles[axis] += 360.0;
    }
    // A fake client never receives svc_setangle, so its Spawn fixangle is
    // never cleared by SV_WriteClientdataToMessage. Permit only this normal
    // aim command to update v_angle, then restore that transport-only field.
    new fixangle = get_entvar(index, var_fixangle);
    set_entvar(index, var_fixangle, 0);
    engfunc(EngFunc_RunPlayerMove, index, angles, 0.0, 0.0, 0.0, 0, 0, 20);
    set_entvar(index, var_fixangle, fixangle);
    new Float:actual[3]; get_entvar(index, var_v_angle, actual);
    LiveEmit("event=cannon_aim requested=%.4f,%.4f,%.4f actual=%.4f,%.4f,%.4f scoped_fixangle=%d", angles[0], angles[1], angles[2], actual[0], actual[1], actual[2], fixangle);
    LiveAttach(nearest, 0); CombatSnapshot();
    LiveEmit("event=mutation operation=actual_cannon_primary attacker=%d userid=%d weapon_entity=%d weapon=144 target=%d distance=%.4f clip=%d next=%.4f player_next=%.4f angles=%.3f,%.3f,%.3f",
        index, LiveUserid(index), weapon, nearest, best, clip, next, playerNext, angles[0], angles[1], angles[2]);
    new flags = get_entvar(index, var_flags), protocol[32];
    get_user_info(index, "csol_protocol", protocol, charsmax(protocol));
    set_user_info(index, "csol_protocol", "1"); set_entvar(index, var_flags, flags & ~FL_FAKECLIENT);
    LiveEmit("event=mutation operation=scoped_human_protocol stage=attack_begin slot=%d saved_flags=%d", index, flags);
    ExecuteHamB(Ham_Weapon_PrimaryAttack, weapon);
    set_entvar(index, var_flags, (get_entvar(index, var_flags) & ~FL_FAKECLIENT) | (flags & FL_FAKECLIENT));
    set_user_info(index, "csol_protocol", protocol);
    LiveEmit("event=mutation operation=scoped_human_protocol stage=attack_end slot=%d flags=%d", index, get_entvar(index, var_flags));
    new changed, killed, zeroHealthLiving;
    g_CombatVictim = nearest;
    for (new n; n < count; n++)
    {
        entity = entities[n];
        if (!CombatMonster(entity) || pev_serial(entity) != serials[n]) continue;
        if (Float:get_entvar(entity, var_health) == health[n] && get_entvar(entity, var_deadflag) == DEAD_NO) continue;
        changed++; CombatEntitySample(entity, "cannon_immediate");
        if (get_entvar(entity, var_deadflag) == DEAD_DEAD)
        { killed++; g_CombatVictim = entity; }
        else if (Float:get_entvar(entity, var_health) <= 0.0)
        { zeroHealthLiving++; g_CombatVictim = entity; }
    }
    LiveEmit("event=cannon_fired weapon=144 clip_before=%d clip_after=%d changed=%d killed=%d zero_health_living=%d sample_entity=%d", clip,
        get_member(weapon, m_Weapon_iClip), changed, killed, zeroHealthLiving, g_CombatVictim);
    g_CombatSample = 0; copy(g_CombatSession, charsmax(g_CombatSession), g_LiveSession);
    set_task(0.1, "CombatSample", COMBAT_SAMPLE_TASK, _, _, "a", 60);
    return PLUGIN_HANDLED;
}

public CombatWalk()
{
    if (!LiveActive() || read_argc() != 4 || g_WalkSlot || !get_member_game(m_bGameStarted) ||
        get_member_game(m_bFreezePeriod) || get_member_game(m_bRoundTerminating))
    { server_print("[CSOL combat] rejected=walk_phase_or_arguments"); return PLUGIN_HANDLED; }
    new argument[32]; read_argv(1, argument, charsmax(argument)); new index = str_to_num(argument);
    read_argv(2, argument, charsmax(argument)); new Float:yaw = str_to_float(argument);
    read_argv(3, argument, charsmax(argument)); new Float:duration = str_to_float(argument);
    if (!OwnedClient(index) || !is_user_alive(index))
    { CapacityIdentity("walk_rejected_identity_or_dead", index); return PLUGIN_HANDLED; }
    if (!(yaw >= -180.0 && yaw <= 180.0) || !(duration > 0.0 && duration <= 8.0)) return PLUGIN_HANDLED;
    g_WalkSlot = index; g_WalkUserid = get_user_userid(index); g_WalkSerial = pev_serial(index);
    g_WalkYaw = yaw; g_WalkLast = get_gametime(); g_WalkDeadline = g_WalkLast + duration; g_WalkCommands = 0;
    copy(g_WalkSession, charsmax(g_WalkSession), g_LiveSession);
    LiveEmit("event=mutation operation=engine_walk slot=%d userid=%d serial=%d yaw=%.3f duration=%.3f speed=240",
        index, g_WalkUserid, g_WalkSerial, yaw, duration);
    return PLUGIN_HANDLED;
}

public CombatWall()
{
    if (!LiveActive() || read_argc() != 4 || !get_member_game(m_bGameStarted) ||
        get_member_game(m_bFreezePeriod) || get_member_game(m_bRoundTerminating))
    { server_print("[CSOL combat] rejected=wall_phase_or_arguments"); return PLUGIN_HANDLED; }
    new argument[32]; read_argv(1, argument, charsmax(argument)); new index = str_to_num(argument);
    read_argv(2, argument, charsmax(argument)); new entity = str_to_num(argument);
    read_argv(3, argument, charsmax(argument)); new Float:damage = str_to_float(argument);
    if (!OwnedClient(index) || !is_user_alive(index))
    { CapacityIdentity("wall_rejected_identity_or_dead", index); return PLUGIN_HANDLED; }
    if (!(damage > 0.0 && damage <= 1000000.0) || !IsHamValid(Ham_TraceAttack) || !IsHamValid(Ham_TakeDamage)) return PLUGIN_HANDLED;
    if (entity <= MaxClients || pev_valid(entity) != 2) return PLUGIN_HANDLED;
    new classname[32], target[64]; get_entvar(entity, var_classname, classname, charsmax(classname));
    get_entvar(entity, var_target, target, charsmax(target));
    if (!equal(classname, "zbsbreak") || Float:get_entvar(entity, var_health) <= 0.0 ||
        get_entvar(entity, var_solid) == SOLID_NOT || Float:get_entvar(entity, var_takedamage) == 0.0) return PLUGIN_HANDLED;
    new serial = pev_serial(entity), trace = create_tr2();
    new Float:position[3], Float:source[3], Float:mins[3], Float:maxs[3], Float:direction[3], Float:length;
    get_entvar(entity, var_absmin, mins); get_entvar(entity, var_absmax, maxs); get_entvar(index, var_origin, source);
    for (new axis; axis < 3; axis++)
    {
        position[axis] = (mins[axis] + maxs[axis]) * 0.5;
        direction[axis] = position[axis] - source[axis]; length += direction[axis] * direction[axis];
    }
    length = floatsqroot(length);
    // Only the wall immediately in front of this owned test player is eligible.
    if (!(length > 0.001 && length <= 256.0)) { free_tr2(trace); return PLUGIN_HANDLED; }
    for (new axis; axis < 3; axis++) direction[axis] /= length;
    LiveAttach(entity, 1);
    LiveEmit("event=mutation operation=injected_wall_trace entity=%d serial=%d attacker=%d userid=%d weapon=%d amount=%.4f health=%.4f target=%s distance=%.3f",
        entity, serial, index, LiveUserid(index), LiveWeapon(index), damage, Float:get_entvar(entity, var_health), target, length);
    set_tr2(trace, TR_pHit, entity); set_tr2(trace, TR_iHitgroup, HIT_GENERIC);
    set_tr2(trace, TR_flFraction, 0.5); set_tr2(trace, TR_vecEndPos, position);
    rg_multidmg_clear();
    ExecuteHamB(Ham_TraceAttack, entity, index, damage, direction, trace, DMG_BULLET);
    rg_multidmg_apply(index, index); free_tr2(trace);
    if (pev_valid(entity) == 2 && pev_serial(entity) == serial)
        LiveEmit("event=wall_after entity=%d health=%.4f solid=%d effects=%d target=%s", entity,
            Float:get_entvar(entity, var_health), get_entvar(entity, var_solid), get_entvar(entity, var_effects), target);
    else LiveEmit("event=wall_after entity=%d removed=1 target=%s", entity, target);
    return PLUGIN_HANDLED;
}

public CombatWalkFrame()
{
    if (!g_WalkSlot) return FMRES_IGNORED;
    new index = g_WalkSlot;
    if (!OwnedClient(index) || get_user_userid(index) != g_WalkUserid || pev_serial(index) != g_WalkSerial)
    { g_WalkSlot = 0; return FMRES_IGNORED; }
    new Float:now = get_gametime(), Float:delta = now - g_WalkLast;
    if (delta < 0.02) return FMRES_IGNORED;
    new bool:stop = now >= g_WalkDeadline || !LiveActive() || !equal(g_WalkSession, g_LiveSession) ||
        !is_user_alive(index) || get_member_game(m_bFreezePeriod) || get_member_game(m_bRoundTerminating);
    new Float:angles[3]; angles[1] = g_WalkYaw;
    new msec = clamp(floatround(delta * 1000.0), 1, 50);
    g_WalkLast = now; g_WalkCommands++;
    // A normal engine user command performs movement, collision and Touch.
    // No origin, velocity, wave, monster or trigger entity is written here.
    engfunc(EngFunc_RunPlayerMove, index, angles, stop ? 0.0 : 240.0, 0.0, 0.0, 0, 0, msec);
    if (stop)
    {
        new Float:origin[3]; get_entvar(index, var_origin, origin);
        LiveEmit("event=engine_walk_end slot=%d commands=%d origin=%.3f,%.3f,%.3f", index, g_WalkCommands, origin[0], origin[1], origin[2]);
        g_WalkSlot = 0;
    }
    return FMRES_IGNORED;
}

public CombatSample()
{
    if (!LiveActive() || !equal(g_CombatSession, g_LiveSession)) { remove_task(COMBAT_SAMPLE_TASK); return; }
    g_CombatSample++;
    CombatEntitySample(g_CombatVictim, "after");
    if (g_CombatSample % 10 == 0) CombatSnapshot();
    if (g_CombatSample == 60) LiveEmit("event=combat_sample_end entity=%d samples=%d", g_CombatVictim, g_CombatSample);
}

// Passive combat observation is separate from the older capacity fixtures.
// It never calls Spawn/Use/TakeDamage, changes a wave, or edits game state.
bool:LiveActive() { return g_Live && get_pcvar_num(g_Enabled) != 0; }

LiveEmit(const format[], any:...)
{
    if (!LiveActive()) return;
    if (++g_LiveSequence > 20000)
    {
        log_to_file("csol_scenario_probe.log", "[CSOL live] session=%s seq=%d time=%.3f event=event_limit", g_LiveSession, g_LiveSequence, get_gametime());
        g_Live = false;
        return;
    }
    new text[640]; vformat(text, charsmax(text), format, 2);
    server_print("[CSOL live] session=%s seq=%d time=%.3f %s", g_LiveSession, g_LiveSequence, get_gametime(), text);
    log_to_file("csol_scenario_probe.log", "[CSOL live] session=%s seq=%d time=%.3f %s", g_LiveSession, g_LiveSequence, get_gametime(), text);
}

LiveField(entity, EntVars:member, text[], length)
{
    copy(text, length, "-");
    if (entity < 0 || !pev_valid(entity)) return;
    get_entvar(entity, member, text, length);
    if (!text[0]) copy(text, length, "-");
    for (new i; text[i]; i++) if (text[i] <= ' ' || text[i] == '=') text[i] = '_';
}

LiveUserid(index)
{ return index >= 1 && index <= MaxClients && is_user_connected(index) ? get_user_userid(index) : 0; }

LiveWeapon(index)
{
    if (!LiveUserid(index) || pev_valid(index) != 2) return 0;
    new item = get_member(index, m_pActiveItem);
    return item > MaxClients && pev_valid(item) == 2 ? get_member(item, m_iId) : 0;
}

Float:LivePlayerFrags(index)
{ return LiveUserid(index) ? (Float:get_entvar(index, var_frags)) : 0.0; }

public LiveBegin()
{
    if (!get_pcvar_num(g_Enabled) || read_argc() != 2 || g_Live) return PLUGIN_HANDLED;
    new map[64], session[49]; get_mapname(map, charsmax(map)); read_argv(1, session, charsmax(session));
    if (!equal(map, "zs_deadend") || get_cvar_num("csol_game_mode") != 2 || !session[0])
    { server_print("[CSOL live] rejected=requires_zs_deadend_scenario"); return PLUGIN_HANDLED; }
    for (new i; session[i]; i++)
        if (!((session[i] >= 'a' && session[i] <= 'z') || (session[i] >= 'A' && session[i] <= 'Z') ||
            (session[i] >= '0' && session[i] <= '9') || session[i] == '_' || session[i] == '-')) return PLUGIN_HANDLED;
    copy(g_LiveSession, charsmax(g_LiveSession), session); g_LiveSequence = 0; g_LiveDamageDepth = 0; g_LiveOverflow = 0; g_Live = true;
    LiveEmit("event=begin map=%s mode=%d maxclients=%d observer=passive", map, get_cvar_num("csol_game_mode"), MaxClients);
    new const messages[][] = {"MPToCL", "ZBSVer2", "DeathMsg"};
    new const callbacks[][] = {"LiveScenarioMessage", "LiveUpgradeMessage", "LiveDeathMessage"};
    for (new i; i < sizeof messages; i++)
    {
        if (g_LiveMessageHooks[i]) continue;
        new message = get_user_msgid(messages[i]);
        if (message) { register_message(message, callbacks[i]); g_LiveMessageHooks[i] = true; }
        else LiveEmit("event=message_unavailable name=%s", messages[i]);
    }
    return LiveStatus();
}

public LiveEnd()
{
    // Require the owner token so a stale probe cannot stop another recording.
    new session[49]; read_argv(1, session, charsmax(session));
    if (read_argc() == 2 && equal(session, g_LiveSession))
    { LiveEmit("event=end"); g_Live = false; }
    return PLUGIN_HANDLED;
}

LiveAttach(entity, kind)
{
    if (g_LiveHooks[kind] || pev_valid(entity) != 2) return;
    if ((kind < 3 && (!IsHamValid(Ham_TakeDamage) || !IsHamValid(Ham_Killed))) ||
        (kind == 3 && !IsHamValid(Ham_OnControls)) || !IsHamValid(Ham_Use))
    { LiveEmit("event=hook_unavailable kind=%d entity=%d", kind, entity); return; }
    // FromEntity observes an existing native object; RegisterHam(classname)
    // could create a diagnostic entity and alter this scenario's pools.
    if (kind < 3)
    {
        RegisterHamFromEntity(Ham_TakeDamage, entity, "LiveEntityDamageBefore", false);
        RegisterHamFromEntity(Ham_TakeDamage, entity, "LiveEntityDamageAfter", true);
        RegisterHamFromEntity(Ham_Killed, entity, "LiveEntityKilled", true);
    }
    else RegisterHamFromEntity(Ham_OnControls, entity, "LiveClearpoint", true);
    RegisterHamFromEntity(Ham_Use, entity, "LiveEntityUse", true);
    g_LiveHooks[kind] = true;
    LiveEmit("event=hook_ready kind=%d entity=%d", kind, entity);
}

public LiveStatus()
{
    if (!LiveActive()) return PLUGIN_HANDLED;
    LiveEmit("event=round started=%d freeze=%d terminating=%d start=%.3f round=%d ct_capacity=%d restarts=%d difficulty=%d freezetime=%.3f cheats=%d",
        get_member_game(m_bGameStarted), get_member_game(m_bFreezePeriod), get_member_game(m_bRoundTerminating),
        Float:get_member_game(m_fRoundStartTimeReal), get_member_game(m_iTotalRoundsPlayed),
        get_member_game(m_iSpawnPointCount_CT), g_Restarts, get_cvar_num("csol_scenario_difficulty"), get_cvar_float("mp_freezetime"), get_cvar_num("sv_cheats"));
    for (new index = 1; index <= MaxClients; index++)
    {
        if (!is_user_connected(index)) continue;
        new Float:origin[3]; get_entvar(index, var_origin, origin);
        LiveEmit("event=player slot=%d userid=%d team=%d alive=%d health=%.3f solid=%d observer=%d weapon=%d frags=%.3f origin=%.3f,%.3f,%.3f",
            index, get_user_userid(index), get_member(index, m_iTeam), is_user_alive(index), Float:get_entvar(index, var_health),
            get_entvar(index, var_solid), get_entvar(index, var_iuser1), LiveWeapon(index), LivePlayerFrags(index), origin[0], origin[1], origin[2]);
    }
    new entity, total, active, details; new Float:minimum, Float:maximum, Float:sum;
    while ((entity = rg_find_ent_by_class(entity, "monster_entity")))
    {
        total++; LiveAttach(entity, 0);
        if (get_entvar(entity, var_solid) == SOLID_NOT || (get_entvar(entity, var_effects) & EF_NODRAW)) continue;
        new Float:health = get_entvar(entity, var_health); sum += health;
        if (!active || health < minimum) minimum = health;
        if (!active || health > maximum) maximum = health;
        active++;
        if (details++ >= 24) continue;
        new Float:origin[3]; get_entvar(entity, var_origin, origin);
        LiveEmit("event=monster entity=%d health=%.3f dead=%d solid=%d sequence=%d frame=%.3f origin=%.3f,%.3f,%.3f",
            entity, health, get_entvar(entity, var_deadflag), get_entvar(entity, var_solid), get_entvar(entity, var_sequence),
            Float:get_entvar(entity, var_frame), origin[0], origin[1], origin[2]);
    }
    LiveEmit("event=monsters total=%d active=%d hp_min=%.3f hp_max=%.3f hp_sum=%.3f detail_truncated=%d", total, active, minimum, maximum, sum, active > 24);
    new const classes[][] = {"zbsbreak", "zbstarget", "trigger_clearpoint", "zbstrigger"};
    for (new kind; kind < sizeof classes; kind++)
    {
        entity = 0;
        while ((entity = rg_find_ent_by_class(entity, classes[kind])))
        {
            if (kind < 3) LiveAttach(entity, kind + 1);
            new target[64], name[64]; LiveField(entity, var_target, target, charsmax(target)); LiveField(entity, var_targetname, name, charsmax(name));
            LiveEmit("event=map_entity entity=%d class=%s health=%.3f solid=%d damage=%d target=%s targetname=%s",
                entity, classes[kind], Float:get_entvar(entity, var_health), get_entvar(entity, var_solid), get_entvar(entity, var_takedamage), target, name);
            if (kind != 3) continue;
            // This matches the marker's raw mins/maxs collection, but is only
            // an overlap observation. It does not expose manager progression.
            new Float:mins[3], Float:maxs[3]; get_entvar(entity, var_mins, mins); get_entvar(entity, var_maxs, maxs);
            for (new index = 1; index <= MaxClients; index++)
            {
                if (!is_user_alive(index) || get_member(index, m_iTeam) != TEAM_CT) continue;
                new Float:origin[3]; get_entvar(index, var_origin, origin);
                if (origin[0] >= mins[0] && origin[0] <= maxs[0] && origin[1] >= mins[1] && origin[1] <= maxs[1] && origin[2] >= mins[2] && origin[2] <= maxs[2])
                    LiveEmit("event=marker_overlap entity=%d slot=%d userid=%d progression=unverified", entity, index, get_user_userid(index));
            }
        }
    }
    return PLUGIN_HANDLED;
}

public LivePlayerSpawn(index)
{
    // The fixture constructs a CBasePlayer, not CBot. Match CBot::Spawn's
    // restoration after the actual base method clears all but FL_PROXY.
    // Userid plus edict serial and AMXX's Bot identity retain ownership;
    // ordinary players and recycled slots can never enter this mutation.
    if (get_pcvar_num(g_Enabled) && OwnedConnection(index) && pev_valid(index) == 2 &&
        IsReapiHookOriginalWasCalled(RG_CBasePlayer_Spawn))
    {
        CapacityIdentity("owned_spawn_before", index);
        if (!(pev(index, pev_flags) & FL_FAKECLIENT))
        {
            LiveEmit("event=mutation operation=restore_owned_bot_flag slot=%d userid=%d serial=%d", index, get_user_userid(index), pev_serial(index));
            set_pev(index, pev_flags, pev(index, pev_flags) | FL_FAKECLIENT | FL_CLIENT);
        }
        CapacityIdentity("owned_spawn_after", index);
    }
    if (LiveActive() && LiveUserid(index))
        LiveEmit("event=player_spawn slot=%d userid=%d team=%d alive=%d health=%.3f started=%d freeze=%d original=%d",
            index, get_user_userid(index), get_member(index, m_iTeam), is_user_alive(index), Float:get_entvar(index, var_health),
            get_member_game(m_bGameStarted), get_member_game(m_bFreezePeriod), IsReapiHookOriginalWasCalled(RG_CBasePlayer_Spawn));
    return HC_CONTINUE;
}

public LivePlayerKilled(victim, attacker, gib)
{
    new classname[64]; LiveField(attacker, var_classname, classname, charsmax(classname));
    if (LiveActive() && LiveUserid(victim)) LiveEmit("event=player_death slot=%d userid=%d attacker=%d attacker_class=%s alive=%d gib=%d original=%d",
        victim, get_user_userid(victim), attacker, classname, is_user_alive(victim), gib, IsReapiHookOriginalWasCalled(RG_CBasePlayer_Killed));
    return HC_CONTINUE;
}

LiveDamageBefore(entity, inflictor, attacker, Float:damage, bits, kind)
{
    if (!LiveActive()) return;
    if (g_LiveDamageDepth >= sizeof g_LiveDamage) { g_LiveOverflow++; LiveEmit("event=damage_stack_overflow"); return; }
    new depth = g_LiveDamageDepth++;
    g_LiveDamage[depth][0] = g_LiveSequence + 1; g_LiveDamage[depth][1] = entity; g_LiveDamage[depth][2] = attacker;
    g_LiveDamage[depth][3] = LiveUserid(attacker); g_LiveDamage[depth][4] = LiveWeapon(attacker);
    g_LiveDamage[depth][5] = get_entvar(entity, var_solid); g_LiveDamage[depth][6] = kind;
    g_LiveHealth[depth] = get_entvar(entity, var_health); g_LiveFrags[depth] = LivePlayerFrags(attacker);
    LiveField(entity, var_target, g_LiveTarget[depth], charsmax(g_LiveTarget[]));
    new classname[64], source[64], victim[64]; LiveField(attacker, var_classname, classname, charsmax(classname)); LiveField(inflictor, var_classname, source, charsmax(source));
    LiveField(entity, var_classname, victim, charsmax(victim));
    LiveEmit("event=damage_pre call=%d kind=%d entity=%d entity_class=%s inflictor=%d source=%s attacker=%d userid=%d attacker_class=%s weapon=%d amount=%.4f bits=%d hp=%.4f frags=%.3f",
        g_LiveDamage[depth][0], kind, entity, victim, inflictor, source, attacker, g_LiveDamage[depth][3], classname,
        g_LiveDamage[depth][4], damage, bits, g_LiveHealth[depth], g_LiveFrags[depth]);
}

LiveDamageAfter(entity, original, accepted)
{
    if (!LiveActive()) return;
    if (g_LiveOverflow) { g_LiveOverflow--; return; }
    if (!g_LiveDamageDepth) { LiveEmit("event=damage_stack_missing entity=%d", entity); return; }
    new depth = --g_LiveDamageDepth;
    if (g_LiveDamage[depth][1] != entity) { LiveEmit("event=damage_stack_mismatch entity=%d", entity); return; }
    new attacker = g_LiveDamage[depth][2], valid = pev_valid(entity);
    new Float:health = valid ? (Float:get_entvar(entity, var_health)) : 0.0;
    LiveEmit("event=damage_post call=%d kind=%d entity=%d valid=%d userid=%d userid_after=%d weapon=%d hp_before=%.4f hp_after=%.4f frags_before=%.3f frags_after=%.3f solid_before=%d solid_after=%d dead_after=%d original=%d accepted=%d target=%s",
        g_LiveDamage[depth][0], g_LiveDamage[depth][6], entity, valid, g_LiveDamage[depth][3], LiveUserid(attacker), g_LiveDamage[depth][4],
        g_LiveHealth[depth], health, g_LiveFrags[depth], LivePlayerFrags(attacker), g_LiveDamage[depth][5],
        valid ? get_entvar(entity, var_solid) : -1, valid ? get_entvar(entity, var_deadflag) : -1, original, accepted, g_LiveTarget[depth]);
}

public LivePlayerDamageBefore(entity, inflictor, attacker, Float:damage, bits)
{ LiveDamageBefore(entity, inflictor, attacker, damage, bits, 0); return HC_CONTINUE; }
public LivePlayerDamageAfter(entity, inflictor, attacker, Float:damage, bits)
{ LiveDamageAfter(entity, IsReapiHookOriginalWasCalled(RG_CBasePlayer_TakeDamage), GetHookChainReturn(ATYPE_INTEGER)); return HC_CONTINUE; }
public LiveEntityDamageBefore(entity, inflictor, attacker, Float:damage, bits)
{
    new classname[64]; LiveField(entity, var_classname, classname, charsmax(classname));
    LiveDamageBefore(entity, inflictor, attacker, damage, bits, equal(classname, "monster_entity") ? 1 : 2);
    return HAM_IGNORED;
}
public LiveEntityDamageAfter(entity, inflictor, attacker, Float:damage, bits)
{
    new result; GetOrigHamReturnInteger(result);
    LiveDamageAfter(entity, GetHamReturnStatus() < HAM_SUPERCEDE, result);
    return HAM_IGNORED;
}
public LiveEntityKilled(entity, attacker, gib)
{ LiveEmit("event=entity_killed_callback entity=%d attacker=%d userid=%d weapon_now=%d gib=%d original=%d", entity, attacker, LiveUserid(attacker), LiveWeapon(attacker), gib, GetHamReturnStatus() < HAM_SUPERCEDE); return HAM_IGNORED; }
public LiveEntityUse(entity, first, second, type, Float:value)
{
    new classname[64], name[64], target[64]; LiveField(entity, var_classname, classname, charsmax(classname));
    LiveField(entity, var_targetname, name, charsmax(name)); LiveField(entity, var_target, target, charsmax(target));
    // Preserve native argument order; the pinned Ham comments name first and
    // second differently from CBaseEntity::Use's activator/caller names.
    LiveEmit("event=use_callback entity=%d class=%s first=%d second=%d type=%d value=%.3f targetname=%s target=%s original=%d", entity, classname, first, second, type, value, name, target, GetHamReturnStatus() < HAM_SUPERCEDE);
    return HAM_IGNORED;
}
public LiveClearpoint(entity, activator)
{
    new result; GetOrigHamReturnInteger(result);
    LiveEmit("event=clearpoint_callback entity=%d activator=%d protection_request=%d original=%d progression=unverified", entity, activator, result, GetHamReturnStatus() < HAM_SUPERCEDE);
    return HAM_IGNORED;
}

// Layouts are taken from native/csol/server/scenario_messages.cpp. These
// observe server messages; they do not prove the client displayed their UI.
public LiveScenarioMessage(message, destination, recipient)
{
    if (!LiveActive() || get_msg_args() < 2 || get_msg_argtype(1) != ARG_BYTE || get_msg_argtype(2) != ARG_BYTE) return PLUGIN_CONTINUE;
    new opcode = get_msg_arg_int(1), subcode = get_msg_arg_int(2);
    if (get_msg_args() == 3 && get_msg_argtype(3) == ARG_SHORT && opcode == 0x1b && subcode == 7)
        LiveEmit("event=wave_message wave_event=%d destination=%d recipient=%d", get_msg_arg_int(3) & 0xffff, destination, recipient);
    else if (get_msg_args() == 2 && opcode == 0x19 && (subcode == 8 || subcode == 9))
        LiveEmit("event=round_outcome_message success=%d destination=%d recipient=%d", subcode == 8, destination, recipient);
    return PLUGIN_CONTINUE;
}
public LiveUpgradeMessage(message, destination, recipient)
{
    if (LiveActive() && get_msg_args() == 2 && get_msg_argtype(1) == ARG_BYTE && get_msg_argtype(2) == ARG_LONG && get_msg_arg_int(1) == 1)
        LiveEmit("event=kill_chain_message slot=%d userid=%d count=%d", recipient, LiveUserid(recipient), get_msg_arg_int(2));
    return PLUGIN_CONTINUE;
}
public LiveDeathMessage(message, destination, recipient)
{
    if (!LiveActive() || get_msg_args() != 4 || get_msg_argtype(1) != ARG_BYTE || get_msg_argtype(2) != ARG_BYTE ||
        get_msg_argtype(3) != ARG_BYTE || get_msg_argtype(4) != ARG_STRING) return PLUGIN_CONTINUE;
    new weapon[64]; get_msg_arg_string(4, weapon, charsmax(weapon));
    for (new i; weapon[i]; i++) if (weapon[i] <= ' ' || weapon[i] == '=') weapon[i] = '_';
    LiveEmit("event=death_message killer=%d victim=%d headshot=%d weapon=%s destination=%d recipient=%d",
        get_msg_arg_int(1), get_msg_arg_int(2), get_msg_arg_int(3), weapon, destination, recipient);
    return PLUGIN_CONTINUE;
}
