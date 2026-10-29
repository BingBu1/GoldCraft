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
new bool:g_CapacityBusy;
new bool:g_ReportedInitialization;
new bool:g_Live, g_LiveSequence, g_LiveDamageDepth, g_LiveOverflow;
new g_LiveSession[49];
new bool:g_LiveHooks[4];
new bool:g_LiveMessageHooks[3];
new g_LiveDamage[64][7]; // event, entity, attacker, userid, weapon, solid, kind
new Float:g_LiveHealth[64], Float:g_LiveFrags[64];
new g_LiveTarget[64][64];

public plugin_init()
{
    register_plugin("CSOL selection live checks", "0.2.0", "GoldCraft contributors");
    g_Enabled = create_cvar("csol_ready_test", "0");
    register_srvcmd("csol_ready_status", "Status");
    register_srvcmd("csol_ready_force_spawn", "ForceSpawn");
    register_srvcmd("csol_ready_client", "ClientCommand");
    register_srvcmd("csol_ready_fill", "FillCapacity");
    register_srvcmd("csol_ready_clear", "ClearCapacity");
    register_srvcmd("csol_live_begin", "LiveBegin");
    register_srvcmd("csol_live_status", "LiveStatus");
    register_srvcmd("csol_live_end", "LiveEnd");
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

bool:OwnedClient(index)
{
    return index >= 1 && index <= MaxClients && g_OwnedUserid[index] > 0 &&
        is_user_connected(index) && is_user_bot(index) &&
        get_user_userid(index) == g_OwnedUserid[index] &&
        (pev(index, pev_flags) & FL_FAKECLIENT) != 0;
}

CapacityIdentity(const stage[], index)
{
    if (index < 1 || index > MaxClients)
    {
        server_print("[CSOL capacity] stage=%s invalid_slot=%d", stage, index);
        return;
    }
    new valid = pev_valid(index);
    server_print("[CSOL capacity] stage=%s slot=%d owned_userid=%d userid=%d connected=%d bot=%d flags=%d private=%d",
        stage, index, g_OwnedUserid[index], get_user_userid(index), is_user_connected(index),
        is_user_bot(index), valid ? pev(index, pev_flags) : 0, valid == 2);
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
CreateCapacityClient(const name[])
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
    engclient_cmd(index, "joinclass", "1");
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
