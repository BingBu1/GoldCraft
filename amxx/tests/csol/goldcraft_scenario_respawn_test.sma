// SPDX-License-Identifier: GPL-3.0-or-later
// Temporary, explicit server-console fixture. Never enable in production.
// Uses real jointeam/joinclass and zero-input RunPlayerMove for its own fake
// clients. No forced Spawn/RoundRespawn, deadtime, health, timer or rule writes.
// Fake clients validate server lifecycle only, not network/HUD or multiplayer.
#include <amxmodx>
#include <fakemeta>
#include <reapi>
#include <hamsandwich>

#define LOG_FILE "goldcraft_scenario_respawn_test.log"
enum Stage { Idle, InitialRound, LateJoin, NormalDeath, ForcedDeath, AwaitWipe, Wiped };
new Stage:g_Stage;
new g_Enabled, g_ForceCvar, g_Anchor, g_Subject, g_Checks, g_Failures;
new g_Userid[33], g_Serial[33], g_Spawns[33], g_Kills[33];
new bool:g_Protected[33], Float:g_SavedDamage[33];
new bool:g_Active, bool:g_Cleaning, bool:g_Failed, bool:g_CvarOwned;
new g_Failure[128], g_SavedForce[64], g_AppliedForce[64], g_Session;
new Float:g_Deadline, Float:g_NextMove, Float:g_NextSample, Float:g_RoundStart;
new Float:g_FirstNoticeAt, Float:g_WipeUntil, Float:g_StageStarted, Float:g_ClockStartedAt;
new g_Notices, g_FirstSeconds, g_LastSeconds, g_BeforeSpawns, g_StageKills;

public plugin_init()
{
    register_plugin("CSOL automatic respawn checks", "0.1.0", "GoldCraft contributors");
    g_Enabled = create_cvar("csol_respawn_test", "0");
    g_ForceCvar = get_cvar_pointer("mp_forcerespawn");
    register_srvcmd("csol_respawn_test_begin", "Begin");
    register_srvcmd("csol_respawn_test_status", "Status");
    register_srvcmd("csol_respawn_test_end", "End");
    RegisterHookChain(RG_CBasePlayer_Spawn, "PlayerSpawned", true);
    RegisterHookChain(RG_CBasePlayer_Killed, "PlayerKilled", true);
    register_forward(FM_StartFrame, "Frame", false);
    new message = get_user_msgid("TextMsg");
    if (message) register_message(message, "TextMessage");
    message = get_user_msgid("ZBSVer2");
    if (message) register_message(message, "UpgradeMessage");
}

Emit(const format[], any:...)
{
    new text[640]; vformat(text, charsmax(text), format, 2);
    server_print("[CSOL respawn] session=%d time=%.3f stage=%d %s", g_Session, get_gametime(), _:g_Stage, text);
    log_to_file(LOG_FILE, "session=%d time=%.3f stage=%d %s", g_Session, get_gametime(), _:g_Stage, text);
}

bool:OwnedIdentity(index)
{
    return index >= 1 && index <= MaxClients && g_Userid[index] > 0 && is_user_connected(index) &&
        get_user_userid(index) == g_Userid[index] && pev_valid(index) &&
        pev_serial(index) == g_Serial[index];
}

bool:Owned(index) { return OwnedIdentity(index) && is_user_bot(index); }

bool:Check(bool:condition, const reason[])
{
    g_Checks++;
    Emit("check=%s result=%s", reason, condition ? "PASS" : "FAIL");
    if (!condition)
    {
        g_Failures++;
        if (!g_Failed) copy(g_Failure, charsmax(g_Failure), reason);
        g_Failed = true;
    }
    return condition;
}

Fail(const reason[])
{
    if (!g_Failed) Check(false, reason);
}

bool:Context()
{
    new map[32]; get_mapname(map, charsmax(map));
    return equal(map, "zs_deadend") && get_cvar_num("csol_game_mode") == 2 && MaxClients == 32;
}

Protect(index)
{
    if (!Owned(index) || !is_user_alive(index) || g_Protected[index]) return;
    g_SavedDamage[index] = get_entvar(index, var_takedamage);
    g_Protected[index] = true;
    set_entvar(index, var_takedamage, DAMAGE_NO);
    Emit("event=protect slot=%d userid=%d old=%.1f", index, g_Userid[index], g_SavedDamage[index]);
}

Unprotect(index)
{
    if (!Owned(index) || !g_Protected[index]) return;
    if (Float:get_entvar(index, var_takedamage) == DAMAGE_NO)
        set_entvar(index, var_takedamage, g_SavedDamage[index]);
    g_Protected[index] = false;
}

SetForce(const value[])
{
    set_pcvar_string(g_ForceCvar, value);
    copy(g_AppliedForce, charsmax(g_AppliedForce), value);
    g_CvarOwned = true;
    Emit("event=mutation cvar=mp_forcerespawn value=%s", value);
}

Cleanup()
{
    g_Cleaning = true;
    g_Active = false;
    for (new index = 1; index <= MaxClients; index++)
    {
        if (!OwnedIdentity(index)) continue;
        Unprotect(index);
        new userid = g_Userid[index];
        rh_drop_client(index, "CSOL automatic respawn fixture cleanup");
        Emit("event=cleanup slot=%d userid=%d removed=%d", index, userid,
            !is_user_connected(index) || get_user_userid(index) != userid);
    }
    if (g_CvarOwned)
    {
        new current[64]; get_pcvar_string(g_ForceCvar, current, charsmax(current));
        if (equal(current, g_AppliedForce))
        {
            set_pcvar_string(g_ForceCvar, g_SavedForce);
            Emit("event=restore cvar=mp_forcerespawn value=%s", g_SavedForce);
        }
        else Emit("event=restore_conflict cvar=mp_forcerespawn current=%s expected=%s", current, g_AppliedForce);
    }
    g_CvarOwned = false;
    arrayset(g_Userid, 0, sizeof g_Userid);
    arrayset(g_Serial, 0, sizeof g_Serial);
    g_Anchor = g_Subject = 0;
    g_Stage = Idle;
    g_Cleaning = false;
}

public plugin_end() { Cleanup(); }
public End()
{
    if (g_Active) Emit("result=ABORT reason=operator_stop");
    Cleanup();
    return PLUGIN_HANDLED;
}

public client_disconnected(index)
{
    if (g_Userid[index] && g_Active && !g_Cleaning) Fail("owned_connection_disappeared");
    g_Userid[index] = g_Serial[index] = 0;
    g_Protected[index] = false;
}

// Mirrors the already-used selection fixture's safe allocation contract.
// Do not call Fakemeta ClientConnect: its pinned reject-buffer ABI is unsafe.
CreateSelectedClient(const name[], bool:subject)
{
    new index = engfunc(EngFunc_CreateFakeClient, name);
    if (!Check(index >= 1 && index <= MaxClients && is_user_connected(index) && is_user_bot(index), "fake_client_allocated")) return 0;
    g_Userid[index] = get_user_userid(index);
    if (pev_valid(index) == 2) engfunc(EngFunc_FreeEntPrivateData, index);
    g_Serial[index] = pev_serial(index);
    if (subject) g_Subject = index; else g_Anchor = index;
    // Only the edict exists until ClientPutInServer constructs CBasePlayer.
    // ReAPI entvar natives require its private object; match the ready fixture.
    set_pev(index, pev_flags, pev(index, pev_flags) | FL_FAKECLIENT | FL_CLIENT);
    dllfunc(DLLFunc_ClientPutInServer, index);
    if (!Check(OwnedIdentity(index) && pev_valid(index) == 2, "fake_client_private_object_initialized")) return 0;
    set_entvar(index, var_flags, get_entvar(index, var_flags) | FL_FAKECLIENT | FL_CLIENT);
    if (!Check(Owned(index), "fake_client_flag_restored_after_put")) return 0;
    // Native Bot menu entry only; no JOINED/team/deadflag/Spawn writes.
    set_member(index, m_iMenu, Menu_ChooseTeam);
    set_member(index, m_iJoiningState, PICKINGTEAM);
    engclient_cmd(index, "jointeam", "2");
    if (!Check(get_member(index, m_iTeam) == TEAM_CT && get_member(index, m_iMenu) == Menu_ChooseAppearance, "normal_CT_team_entry")) return 0;
    engclient_cmd(index, "specmode", "3");
    if (subject) g_BeforeSpawns = g_Spawns[index];
    engclient_cmd(index, "joinclass", "1");
    if (subject) g_ClockStartedAt = get_gametime();
    Emit("event=selection slot=%d userid=%d subject=%d joining=%d alive=%d observer=%d", index,
        g_Userid[index], subject, get_member(index, m_iJoiningState), is_user_alive(index), get_entvar(index, var_iuser1));
    return index;
}

public Begin()
{
    if (!get_pcvar_num(g_Enabled) || g_Active || !g_ForceCvar) return PLUGIN_HANDLED;
    if (!Context() || get_member_game(m_bGameStarted) || get_cvar_num("sypb_quota") != 0 ||
        get_cvar_num("csol_scenario_difficulty") != 1)
    {
        server_print("[CSOL respawn] refused: fresh zs_deadend/mode2/difficulty1, 32 slots, SyPB quota 0 required");
        return PLUGIN_HANDLED;
    }
    for (new index = 1; index <= MaxClients; index++)
        if (is_user_connected(index))
        {
            server_print("[CSOL respawn] refused: this fixture requires an empty isolated server");
            return PLUGIN_HANDLED;
        }
    if (!get_user_msgid("TextMsg")) return PLUGIN_HANDLED;
    g_Session = get_systime(); g_Checks = g_Failures = 0; g_Failed = false; g_Failure[0] = 0;
    arrayset(g_Spawns, 0, sizeof g_Spawns); arrayset(g_Kills, 0, sizeof g_Kills);
    g_Active = true; g_Stage = InitialRound;
    g_Deadline = get_gametime() + 180.0; g_NextMove = g_NextSample = 0.0;
    get_pcvar_string(g_ForceCvar, g_SavedForce, charsmax(g_SavedForce));
    SetForce("0");
    Emit("event=begin fake_clients=2 network_multiplayer=unverified deadline=%.3f", g_Deadline);
    CreateSelectedClient("GC_Respawn_Anchor", false);
    return PLUGIN_HANDLED;
}

StartWait(Stage:stage)
{
    g_Stage = stage;
    g_StageStarted = get_gametime();
    g_Deadline = g_StageStarted + 180.0;
    g_Notices = 0; g_FirstSeconds = g_LastSeconds = -1; g_FirstNoticeAt = 0.0;
    g_ClockStartedAt = 0.0;
    g_BeforeSpawns = Owned(g_Subject) ? g_Spawns[g_Subject] : 0;
    g_StageKills = Owned(g_Subject) ? g_Kills[g_Subject] : 0;
    Emit("event=wait_begin expected_spawns=%d", g_BeforeSpawns + 1);
}

KillOwned(index)
{
    if (!Check(Owned(index) && is_user_alive(index), "kill_target_is_owned_living_player")) return;
    Unprotect(index);
    Emit("event=mutation operation=native_Killed slot=%d userid=%d", index, g_Userid[index]);
    // Real virtual Killed path including hooks, death animation and deaththink.
    // This is a controlled death trigger, not weapon damage/network evidence.
    ExecuteHamB(Ham_Killed, index, index, GIB_NEVER);
    Check(Owned(index) && !is_user_alive(index), "native_death_took_effect");
}

public PlayerSpawned(index)
{
    // AMXX IsBot reads FL_FAKECLIENT (or native BOT authid). A real Spawn can
    // clear that flag, so establish our allocation identity before restoring.
    if (!g_Active || !OwnedIdentity(index) || pev_valid(index) != 2) return HC_CONTINUE;
    new original = IsReapiHookOriginalWasCalled(RG_CBasePlayer_Spawn);
    if (!original) { Fail("original_spawn_was_suppressed"); return HC_CONTINUE; }
    // CBasePlayer::Spawn clears the flag; match CBot::Spawn for our clients.
    set_entvar(index, var_flags, get_entvar(index, var_flags) | FL_FAKECLIENT | FL_CLIENT);
    if (!Owned(index)) { Fail("fake_client_flag_not_restored_after_spawn"); return HC_CONTINUE; }
    if (is_user_alive(index) && get_member(index, m_iTeam) == TEAM_CT)
    {
        g_Spawns[index]++;
        g_Protected[index] = false;
        Protect(index);
        Emit("event=spawn slot=%d userid=%d count=%d native_spawns=%d notices=%d original=%d", index,
            g_Userid[index], g_Spawns[index], get_member(index, m_iNumSpawns), g_Notices, original);
        if (g_Stage == Wiped || (g_Stage != InitialRound && index == g_Anchor)) Fail("unexpected_spawn_after_initial_round");
    }
    return HC_CONTINUE;
}

public PlayerKilled(victim, attacker, gib)
{
    if (!g_Active || !Owned(victim)) return HC_CONTINUE;
    if (!IsReapiHookOriginalWasCalled(RG_CBasePlayer_Killed)) Fail("original_Killed_was_suppressed");
    g_Kills[victim]++;
    g_Protected[victim] = false;
    Emit("event=killed slot=%d userid=%d count=%d deadflag=%d", victim, g_Userid[victim], g_Kills[victim], get_entvar(victim, var_deadflag));
    return HC_CONTINUE;
}

public TextMessage(message, destination, recipient)
{
    if (!g_Active || recipient != g_Subject || !Owned(recipient) || get_msg_args() < 2) return PLUGIN_CONTINUE;
    new token[96]; get_msg_arg_string(2, token, charsmax(token));
    if (!equal(token, "#CSO_ZBS_AutoRespawnWait")) return PLUGIN_CONTINUE;
    if (g_Stage == Wiped) { Fail("countdown_after_team_wipe"); return PLUGIN_CONTINUE; }
    if (get_msg_args() != 3 || get_msg_arg_int(1) != 4) { Fail("malformed_original_countdown_message"); return PLUGIN_CONTINUE; }
    new argument[16]; get_msg_arg_string(3, argument, charsmax(argument));
    new seconds = str_to_num(argument);
    if (seconds < 0 || is_user_alive(recipient) || get_entvar(recipient, var_iuser1) == 0)
        Fail("countdown_requires_dead_observer");
    if (g_Notices && seconds > g_LastSeconds) Fail("countdown_restarted_or_increased");
    if (!g_Notices)
    {
        g_FirstSeconds = seconds; g_FirstNoticeAt = get_gametime();
        if (g_Stage != LateJoin) g_ClockStartedAt = get_member(recipient, m_fDeadTime);
        Emit("event=clock_bound started=%.3f minimum_delay=1.500 basis=%s",
            g_ClockStartedAt, g_Stage == LateJoin ? "successful_selection" : "native_deadtime");
    }
    g_LastSeconds = seconds; g_Notices++;
    Emit("event=countdown seconds=%d messages=%d deadtime=%.3f observer=%d", seconds, g_Notices,
        Float:get_member(recipient, m_fDeadTime), get_entvar(recipient, var_iuser1));
    return PLUGIN_CONTINUE;
}

public UpgradeMessage(message, destination, recipient)
{
    if (!g_Active || !Owned(recipient) || get_msg_args() != 2) return PLUGIN_CONTINUE;
    new opcode = get_msg_arg_int(1);
    if (opcode == 4 || opcode == 5)
        Emit("event=published_total_level slot=%d level=%d increased=%d", recipient,
            get_msg_arg_int(2), opcode == 5);
    return PLUGIN_CONTINUE;
}

bool:ValidateRespawn()
{
    if (!Check(g_Spawns[g_Subject] == g_BeforeSpawns + 1, "exactly_one_natural_spawn")) return false;
    // The default row has column6=0.1: base level1 waits 1.5 seconds,
    // which the original integer TextMsg displays as 1 then 0.
    if (!Check(g_Notices >= 1 && g_FirstSeconds >= 0, "countdown_observed_before_spawn")) return false;
    if (!Check(get_gametime() + 0.25 >= g_FirstNoticeAt + float(g_FirstSeconds), "no_spawn_before_announced_deadline")) return false;
    // The reviewed runtime row1 column6 is 0.1 and the smallest base level is
    // one: every legal base level therefore waits at least 1.5 seconds. This
    // independent bound does not use rounded TextMsg seconds. Allow one 10ms
    // fixture movement tick for timestamp quantization, never the 0.1 force.
    Emit("event=clock_elapsed seconds=%.3f minimum=1.500 tick_tolerance=0.010", get_gametime() - g_ClockStartedAt);
    if (!Check(g_ClockStartedAt > 0.0 && get_gametime() + 0.01 >= g_ClockStartedAt + 1.5,
        "actual_clock_respects_difficulty_minimum")) return false;
    if (!Check(get_entvar(g_Subject, var_iuser1) == 0 && get_entvar(g_Subject, var_solid) != SOLID_NOT &&
        get_member(g_Subject, m_iJoiningState) == JOINED, "native_observer_exit_and_solid_player")) return false;
    if (g_Stage == LateJoin && !Check(g_Kills[g_Subject] == g_StageKills, "late_join_does_not_require_a_death")) return false;
    if (g_Stage != LateJoin && !Check(g_Kills[g_Subject] == g_StageKills + 1, "one_native_death_before_respawn")) return false;
    return true;
}

public Frame()
{
    if (!g_Active || g_Cleaning) return FMRES_IGNORED;
    if (!get_pcvar_num(g_Enabled) || !Context()) Fail("test_disabled_or_map_mode_changed");
    for (new index = 1; index <= MaxClients; index++)
        if (is_user_connected(index) && !Owned(index)) Fail("unowned_client_joined_abort_without_touching_it");
    new Float:now = get_gametime();
    if (now > g_Deadline) Fail("phase_timeout");
    if (!g_Failed && now >= g_NextMove)
    {
        g_NextMove = now + 0.01;
        for (new index = 1; index <= MaxClients; index++)
        {
            if (!Owned(index) || pev_valid(index) != 2) continue;
            new Float:angles[3]; get_entvar(index, var_v_angle, angles);
            engfunc(EngFunc_RunPlayerMove, index, angles, 0.0, 0.0, 0.0, 0, 0, 10);
        }
    }
    if (g_Failed)
    {
        Emit("result=FAIL checks=%d failures=%d reason=%s", g_Checks, g_Failures, g_Failure);
        Cleanup(); return FMRES_IGNORED;
    }
    if (now >= g_NextSample) { Status(); g_NextSample = now + 1.0; }
    if (g_Stage == InitialRound)
    {
        if (!Owned(g_Anchor) || !is_user_alive(g_Anchor) || !get_member_game(m_bGameStarted) ||
            get_member_game(m_bFreezePeriod) || get_member_game(m_bRoundTerminating)) return FMRES_IGNORED;
        g_RoundStart = get_member_game(m_fRoundStartTimeReal);
        if (!Check(g_Spawns[g_Anchor] == 1, "anchor_natural_initial_round_spawn")) return FMRES_IGNORED;
        Protect(g_Anchor);
        StartWait(LateJoin);
        CreateSelectedClient("GC_Respawn_Late", true);
        return FMRES_IGNORED;
    }
    if (!Owned(g_Anchor) || !Owned(g_Subject)) { Fail("owned_identity_missing"); return FMRES_IGNORED; }
    if (Float:get_member_game(m_fRoundStartTimeReal) != g_RoundStart || get_member_game(m_bFreezePeriod))
        { Fail("automatic_respawn_restarted_round"); return FMRES_IGNORED; }
    if (g_Stage == Wiped)
    {
        if (is_user_alive(g_Anchor) || is_user_alive(g_Subject)) Fail("team_wipe_cannot_respawn_even_with_forcerespawn");
        if (now >= g_WipeUntil && !g_Failed)
        {
            Check(bool:get_member_game(m_bRoundTerminating), "team_wipe_enters_failed_round");
            Check(g_Spawns[g_Subject] == g_BeforeSpawns, "no_spawn_after_full_remaining_deadline");
            Emit("result=%s checks=%d failures=%d network_multiplayer=unverified", g_Failed ? "FAIL" : "PASS", g_Checks, g_Failures);
            Cleanup();
        }
        return FMRES_IGNORED;
    }
    if (!is_user_alive(g_Anchor)) { Fail("anchor_died_before_controlled_wipe"); return FMRES_IGNORED; }
    if (g_Stage == AwaitWipe)
    {
        if (is_user_alive(g_Subject)) { Fail("subject_revived_before_wipe_setup"); return FMRES_IGNORED; }
        if (g_Notices && g_FirstSeconds >= 1)
        {
            // Integer TextMsg truncates the remaining fractional second.
            // Observe five full seconds beyond both independent deadlines.
            g_WipeUntil = floatmax(g_FirstNoticeAt + float(g_FirstSeconds),
                g_ClockStartedAt + 1.5) + 5.0;
            g_Stage = Wiped; g_Deadline = g_WipeUntil + 10.0;
            KillOwned(g_Anchor);
        }
        return FMRES_IGNORED;
    }
    if (!is_user_alive(g_Subject)) return FMRES_IGNORED;
    if (!ValidateRespawn()) return FMRES_IGNORED;
    if (g_Stage == LateJoin) StartWait(NormalDeath);
    else if (g_Stage == NormalDeath) { SetForce("0.1"); StartWait(ForcedDeath); }
    else if (g_Stage == ForcedDeath) StartWait(AwaitWipe);
    KillOwned(g_Subject);
    return FMRES_IGNORED;
}

public Status()
{
    if (!g_Active) return PLUGIN_HANDLED;
    Emit("event=rules started=%d freeze=%d terminating=%d restart_at=%.3f force=%.3f",
        get_member_game(m_bGameStarted), get_member_game(m_bFreezePeriod), get_member_game(m_bRoundTerminating),
        Float:get_member_game(m_flRestartRoundTime), get_pcvar_float(g_ForceCvar));
    for (new index = 1; index <= MaxClients; index++)
        if (Owned(index) && pev_valid(index) == 2)
            Emit("event=player slot=%d userid=%d serial=%d alive=%d team=%d joining=%d observer=%d deadflag=%d deadtime=%.3f spawns=%d pending=%.3f game_forcing=%d protected=%d",
                index, g_Userid[index], g_Serial[index], is_user_alive(index), get_member(index, m_iTeam),
                get_member(index, m_iJoiningState), get_entvar(index, var_iuser1), get_entvar(index, var_deadflag),
                Float:get_member(index, m_fDeadTime), get_member(index, m_iNumSpawns), Float:get_member(index, m_flRespawnPending),
                get_member(index, m_bGameForcingRespawn), g_Protected[index]);
    return PLUGIN_HANDLED;
}
