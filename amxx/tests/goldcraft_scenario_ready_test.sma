// SPDX-License-Identifier: GPL-3.0-or-later
// Temporary local-server observations through the pinned public ReAPI 5.29.
// This plugin never supplies spawn points or starts rounds. Capacity fixtures
// select a real character only for the fake clients created by this plugin.
#include <amxmodx>
#include <fakemeta>
#include <reapi>

new g_Enabled, g_Restarts;
new g_OwnedUserid[33];
new bool:g_CapacityBusy;
new bool:g_ReportedInitialization;

public plugin_init()
{
    register_plugin("CSOL selection live checks", "0.1.0", "GoldCraft contributors");
    g_Enabled = create_cvar("csol_ready_test", "0");
    register_srvcmd("csol_ready_status", "Status");
    register_srvcmd("csol_ready_force_spawn", "ForceSpawn");
    register_srvcmd("csol_ready_client", "ClientCommand");
    register_srvcmd("csol_ready_fill", "FillCapacity");
    register_srvcmd("csol_ready_clear", "ClearCapacity");
    RegisterHookChain(RG_CSGameRules_RestartRound, "RoundRestarted", true);
}

public RoundRestarted() { g_Restarts++; }

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
