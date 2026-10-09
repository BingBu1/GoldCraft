// Independent native fixture only. ReAPI 5.29 / AMXX 1.9 named APIs.
#include <amxmodx>
#include <reapi>
#include <fakemeta>
#include <hamsandwich>

new gEnabled, gTarget, gBlocked, gCalls, gAttacker, gEarlyCvars, gRoundBlocked;
new Float:gTraceStart[3], Float:gTraceEnd[3];

public plugin_init()
{
    register_plugin("GoldCraft map mining fixture", "0.1.0", "GoldCraft contributors");
    gEnabled = create_cvar("mc_map_mining_test", "0");
    gEarlyCvars = get_cvar_pointer("mc_map_mining") != 0
        && get_cvar_pointer("mc_map_mining_persist") != 0
        && get_cvar_pointer("mc_default_form") != 0 && get_cvar_pointer("mc_allow_switch") != 0;
    register_srvcmd("gc_mining_probe", "Probe");
    register_srvcmd("gc_mining_state", "State");
    register_srvcmd("gc_mining_edit", "Edit");
    register_srvcmd("gc_mining_filter", "Filter");
    register_srvcmd("gc_mining_bullet", "FireNativeBullet");
    register_srvcmd("gc_mining_round", "RoundCommand");
    RegisterHookChain(RG_RoundEnd, "RoundBefore", false);
    // ReAPI has no CBreakable::TakeDamage hookchain in the pinned source.
    // Ham observes/cancels that actual virtual callback, without private offsets.
    RegisterHam(Ham_TakeDamage, "func_breakable", "DamageBefore", false);
}

public DamageBefore(entity, inflictor, attacker, Float:amount, bits)
{
    if (entity != gTarget) return HAM_IGNORED;
    gCalls++; gAttacker = attacker;
    if (!gBlocked) return HAM_IGNORED;
    SetHamReturnInteger(0);
    return HAM_SUPERCEDE;
}

public Probe()
{
    if (!get_pcvar_num(gEnabled) || (read_argc() != 2 && read_argc() != 3)) return PLUGIN_HANDLED;
    new arg[16], wanted[16], model[32];
    read_argv(1, arg, charsmax(arg)); formatex(wanted, charsmax(wanted), "*%d", str_to_num(arg));
    new ignore;
    if (read_argc() == 3) { read_argv(2, arg, charsmax(arg)); ignore = str_to_num(arg); }
    if (ignore < 1 || ignore > MaxClients || !is_user_connected(ignore)) ignore = 0;
    for (new entity = MaxClients + 1; entity < global_get(glb_maxEntities); entity++) {
        if (is_nullent(entity) || get_entvar(entity, var_solid) == SOLID_NOT) continue;
        get_entvar(entity, var_model, model, charsmax(model));
        if (!equal(model, wanted)) continue;
        gTarget = entity; gBlocked = gCalls = gAttacker = 0;
        new Float:low[3], Float:high[3], Float:center[3], Float:eye[3], Float:point[3];
        get_entvar(entity, var_absmin, low); get_entvar(entity, var_absmax, high);
        for (new i = 0; i < 3; i++) center[i] = (low[i] + high[i]) * 0.5;
        new trace = create_tr2();
        for (new axis = 0; axis < 3; axis++) {
            for (new side = 0; side < 2; side++) {
                eye = center; eye[axis] = side ? high[axis] + 64.0 : low[axis] - 64.0;
                engfunc(EngFunc_TraceLine, eye, center, DONT_IGNORE_MONSTERS, ignore, trace);
                if (get_tr2(trace, TR_StartSolid) || get_tr2(trace, TR_AllSolid) || get_tr2(trace, TR_pHit) != entity) continue;
                get_tr2(trace, TR_vecEndPos, point);
                gTraceStart = eye; gTraceEnd = center;
                server_print("[GC mining] target=%d eye=%.6f,%.6f,%.6f point=%.6f,%.6f,%.6f early=%d", entity,
                    eye[0], eye[1], eye[2], point[0], point[1], point[2], gEarlyCvars);
                free_tr2(trace); return State();
            }
        }
        free_tr2(trace);
        server_print("[GC mining] no clear approach target=%d", entity);
        return PLUGIN_HANDLED;
    }
    server_print("[GC mining] target not found");
    return PLUGIN_HANDLED;
}

public State()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    server_print("[GC mining] terminating=%d", get_member_game(m_bRoundTerminating));
    if (is_nullent(gTarget)) { server_print("[GC mining] removed=1 calls=%d attacker=%d", gCalls, gAttacker); return PLUGIN_HANDLED; }
    new ignore = gAttacker, arg[16];
    if (read_argc() == 2) { read_argv(1, arg, charsmax(arg)); ignore = str_to_num(arg); }
    if (ignore < 1 || ignore > MaxClients || !is_user_connected(ignore)) ignore = 0;
    new trace = create_tr2();
    engfunc(EngFunc_TraceLine, gTraceStart, gTraceEnd, DONT_IGNORE_MONSTERS, ignore, trace);
    new rayHits = get_tr2(trace, TR_pHit) == gTarget;
    engfunc(EngFunc_TraceHull, gTraceStart, gTraceEnd, DONT_IGNORE_MONSTERS, HULL_HUMAN, ignore, trace);
    new standingHits = get_tr2(trace, TR_pHit) == gTarget;
    engfunc(EngFunc_TraceHull, gTraceStart, gTraceEnd, DONT_IGNORE_MONSTERS, HULL_HEAD, ignore, trace);
    new crouchingHits = get_tr2(trace, TR_pHit) == gTarget;
    free_tr2(trace);
    server_print("[GC mining] ray=%d standing=%d crouching=%d", rayHits, standingHits, crouchingHits);
    server_print("[GC mining] target=%d health=%.3f solid=%d damage=%.1f calls=%d attacker=%d blocked=%d", gTarget,
        Float:get_entvar(gTarget, var_health), get_entvar(gTarget, var_solid), Float:get_entvar(gTarget, var_takedamage), gCalls, gAttacker, gBlocked);
    return PLUGIN_HANDLED;
}

public RoundBefore(WinStatus:status, ScenarioEventEndRound:event, Float:delay)
{
    if (!gRoundBlocked) return HC_CONTINUE;
    SetHookChainReturn(ATYPE_BOOL, false);
    return HC_SUPERCEDE;
}

public RoundCommand()
{
    if (!get_pcvar_num(gEnabled) || read_argc() != 2) return PLUGIN_HANDLED;
    new arg[16]; read_argv(1, arg, charsmax(arg));
    if (equal(arg, "restart_end")) {
        rg_restart_round();
        set_pcvar_num(get_cvar_pointer("mc_map_mining_persist"), 0);
        rg_round_end(30.0, WINSTATUS_DRAW, ROUND_END_DRAW, "", "", true);
    }
    else if (equal(arg, "restart")) rg_restart_round();
    else if (equal(arg, "end") || equal(arg, "cancel")) {
        gRoundBlocked = equal(arg, "cancel");
        // A long real termination delay separates end-of-round restoration from
        // the following cleanup. trigger=true exercises actual ReAPI cancellation.
        rg_round_end(30.0, WINSTATUS_DRAW, ROUND_END_DRAW, "", "", true);
        gRoundBlocked = false;
    }
    return State();
}

public Edit()
{
    if (!get_pcvar_num(gEnabled) || is_nullent(gTarget) || read_argc() != 3) return PLUGIN_HANDLED;
    new arg[32]; read_argv(1, arg, charsmax(arg)); new Float:health = str_to_float(arg);
    read_argv(2, arg, charsmax(arg)); new Float:damage = str_to_float(arg);
    if (health < 1.0 || health > 5000.0 || (damage != DAMAGE_NO && damage != DAMAGE_YES)) return PLUGIN_HANDLED;
    set_entvar(gTarget, var_health, health); set_entvar(gTarget, var_takedamage, damage);
    return State();
}

public Filter()
{
    if (!get_pcvar_num(gEnabled) || read_argc() != 2) return PLUGIN_HANDLED;
    new arg[8]; read_argv(1, arg, charsmax(arg)); gBlocked = str_to_num(arg) == 1;
    return State();
}

public FireNativeBullet()
{
    if (!get_pcvar_num(gEnabled) || is_nullent(gTarget) || read_argc() != 3) return PLUGIN_HANDLED;
    new arg[16]; read_argv(1, arg, charsmax(arg)); new player = str_to_num(arg);
    read_argv(2, arg, charsmax(arg)); new damage = str_to_num(arg);
    if (!is_user_alive(player) || damage < 1 || damage > 100) return PLUGIN_HANDLED;
    new Float:eye[3], Float:offset[3], Float:low[3], Float:high[3], Float:direction[3], Float:length;
    get_entvar(player, var_origin, eye); get_entvar(player, var_view_ofs, offset);
    get_entvar(gTarget, var_absmin, low); get_entvar(gTarget, var_absmax, high);
    for (new i = 0; i < 3; i++) {
        eye[i] += offset[i]; direction[i] = (low[i] + high[i]) * 0.5 - eye[i]; length += direction[i] * direction[i];
    }
    length = floatsqroot(length); if (length < 1.0) return PLUGIN_HANDLED;
    for (new i = 0; i < 3; i++) direction[i] /= length;
    rg_fire_bullets3(player, player, eye, direction, 0.0, 256.0, 1, BULLET_PLAYER_45ACP, damage, 1.0, true, 0);
    return State();
}
