// SPDX-License-Identifier: GPL-3.0-or-later
// Explicit isolated-server diagnostic. Owns one fake client and calls actual
// RunPlayerMove / UpdateClientData; does not set stamina or prediction flags.
#include <amxmodx>
#include <fakemeta>
#include <reapi>

new g_Player, g_Userid, g_Serial, g_Jumps;
new bool:g_Active, bool:g_Cleaning;
new Float:g_Start, Float:g_Sample, Float:g_Move;

public plugin_init()
{
    register_plugin("GoldCraft jump snapshot probe", "0.1.0", "GoldCraft contributors");
    register_srvcmd("gc_jump_probe_begin", "Begin");
    register_srvcmd("gc_jump_probe_end", "End");
    register_forward(FM_StartFrame, "Frame");
    RegisterHookChain(RG_CBasePlayer_Spawn, "Spawned", true);
}

bool:Owned()
{
    return g_Player > 0 && g_Userid > 0 && is_user_connected(g_Player) &&
        get_user_userid(g_Player) == g_Userid && pev_valid(g_Player) == 2 &&
        pev_serial(g_Player) == g_Serial;
}

Cleanup()
{
    g_Active = false; g_Cleaning = true;
    if (Owned()) rh_drop_client(g_Player, "Jump snapshot probe complete");
    g_Player = g_Userid = g_Serial = 0; g_Cleaning = false;
}

Stop(const reason[])
{
    server_print("[Jump snapshot] event=end reason=%s jumps=%d", reason, g_Jumps);
    Cleanup();
}
public End() { if (g_Active) Stop("operator_stop"); return PLUGIN_HANDLED; }
public plugin_end() { Cleanup(); }
public client_disconnected(id)
{ if (id == g_Player && g_Active && !g_Cleaning) Stop("owned_disconnect"); }

public Begin()
{
    if (g_Active) return PLUGIN_HANDLED;
    new map[32]; get_mapname(map, charsmax(map));
    if (!equal(map, "zs_deadend") || MaxClients != 32 || get_cvar_num("csol_game_mode") != 2 ||
        get_cvar_num("sypb_quota") != 0) return PLUGIN_HANDLED;
    for (new id = 1; id <= MaxClients; id++) if (is_user_connected(id)) return PLUGIN_HANDLED;
    g_Active = true; g_Start = get_gametime(); g_Sample = g_Move = 0.0; g_Jumps = 0;
    g_Player = engfunc(EngFunc_CreateFakeClient, "GC_Jump_Snapshot");
    if (g_Player < 1 || !is_user_connected(g_Player) || !is_user_bot(g_Player))
    { Stop("fake_allocation_failed"); return PLUGIN_HANDLED; }
    g_Userid = get_user_userid(g_Player);
    if (pev_valid(g_Player) == 2) engfunc(EngFunc_FreeEntPrivateData, g_Player);
    g_Serial = pev_serial(g_Player);
    set_pev(g_Player, pev_flags, pev(g_Player, pev_flags) | FL_FAKECLIENT | FL_CLIENT);
    dllfunc(DLLFunc_ClientPutInServer, g_Player);
    if (!Owned()) { Stop("fake_private_failed"); return PLUGIN_HANDLED; }
    set_member(g_Player, m_iMenu, Menu_ChooseTeam);
    set_member(g_Player, m_iJoiningState, PICKINGTEAM);
    engclient_cmd(g_Player, "jointeam", "2");
    engclient_cmd(g_Player, "specmode", "3");
    engclient_cmd(g_Player, "joinclass", "1");
    server_print("[Jump snapshot] event=begin slot=%d native=1 fake=1 alive=%d health=%.3f dead=%d joining=%d team=%d",
        g_Player, is_user_alive(g_Player), Float:get_entvar(g_Player, var_health),
        get_entvar(g_Player, var_deadflag), get_member(g_Player, m_iJoiningState), get_member(g_Player, m_iTeam));
    return PLUGIN_HANDLED;
}

public Spawned(id)
{
    if (!g_Active || id != g_Player || !Owned() ||
        !IsReapiHookOriginalWasCalled(RG_CBasePlayer_Spawn)) return HC_CONTINUE;
    // Native Spawn clears the non-CBot fixture flag; restore our owned actor.
    set_entvar(id, var_flags, get_entvar(id, var_flags) | FL_FAKECLIENT | FL_CLIENT);
    set_entvar(id, var_takedamage, DAMAGE_NO);
    server_print("[Jump snapshot] event=spawn slot=%d spawns=%d", id, get_member(id, m_iNumSpawns));
    return HC_CONTINUE;
}

public Frame()
{
    if (!g_Active) return FMRES_IGNORED;
    if (!Owned()) { Stop("ownership_changed"); return FMRES_IGNORED; }
    for (new id = 1; id <= MaxClients; id++)
        if (is_user_connected(id) && id != g_Player) { Stop("other_player"); return FMRES_IGNORED; }
    new Float:now = get_gametime();
    if (now < g_Start || now - g_Start >= 45.0) { Stop("complete"); return FMRES_IGNORED; }
    // RunPlayerMove also drives native GETINTOGAME / PreThink. Waiting for
    // alive here would prevent our fake actor from ever finishing admission.
    if (now >= g_Move)
    {
        g_Move = now + 0.01;
        new Float:angles[3], Float:before[3], Float:after[3];
        get_entvar(g_Player, var_v_angle, angles); get_entvar(g_Player, var_velocity, before);
        new buttons;
        if (get_entvar(g_Player, var_deadflag) == DEAD_NO && Float:get_entvar(g_Player, var_health) > 0.0 &&
            now - g_Start >= 22.0 && !get_member_game(m_bFreezePeriod) &&
            get_entvar(g_Player, var_flags) & FL_ONGROUND && !(get_entvar(g_Player, var_oldbuttons) & IN_JUMP))
            buttons = IN_JUMP;
        engfunc(EngFunc_RunPlayerMove, g_Player, angles, 0.0, 0.0, 0.0, buttons, 0, 10);
        if (!Owned()) return FMRES_IGNORED;
        get_entvar(g_Player, var_velocity, after);
        if (buttons && before[2] <= 0.0 && after[2] > 0.0)
        {
            g_Jumps++;
            server_print("[Jump snapshot] event=jump n=%d t=%.3f vz=%.3f", g_Jumps, now, after[2]);
        }
    }
    if (now >= g_Sample)
    {
        g_Sample = now + 0.2;
        new base[256], outgoing[256];
        engfunc(EngFunc_GetPhysicsInfoString, g_Player, base, charsmax(base));
        // Fakemeta's documented global clientdata handle is zero. Read it
        // immediately on this server-thread boundary, before another callback.
        dllfunc(DLLFunc_UpdateClientData, g_Player, 1, 0);
        get_cd(0, CD_PhysInfo, outgoing, charsmax(outgoing));
        server_print("[Jump snapshot] event=sample t=%.3f freeze=%d alive=%d health=%.3f dead=%d joining=%d base=%s outgoing=%s", now,
            get_member_game(m_bFreezePeriod), is_user_alive(g_Player), Float:get_entvar(g_Player, var_health),
            get_entvar(g_Player, var_deadflag), get_member(g_Player, m_iJoiningState), base, outgoing);
    }
    return FMRES_IGNORED;
}
