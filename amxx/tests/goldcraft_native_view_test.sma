// Owned localhost fixture only; actual engine eye/brush query, no native hook replacement.
#include <amxmodx>
#include <reapi>
#include <fakemeta>

new gEnabled, gPlayer, gWater, gPin, gMovement, gGrounded, gMoveType;
new Float:gOrigin[3], Float:gOffset[3];
new Float:gVelocity[3];
public plugin_init()
{
    register_plugin("GoldCraft native view test", "1.0", "GoldCraft contributors");
    gEnabled = create_cvar("mc_native_view_test", "0");
    register_srvcmd("gc_native_view", "Command");
    register_forward(FM_StartFrame, "Frame");
}
public Frame()
{
    if (!get_pcvar_num(gEnabled) || !gPin || !is_user_alive(gPlayer)) return FMRES_IGNORED;
    engfunc(EngFunc_SetOrigin, gPlayer, gOrigin);
    set_entvar(gPlayer, var_view_ofs, gOffset);
    if (gMovement) {
        set_entvar(gPlayer, var_velocity, gVelocity);
        set_entvar(gPlayer, var_movetype, gMoveType);
        new flags = get_entvar(gPlayer, var_flags);
        set_entvar(gPlayer, var_flags, gGrounded ? flags | FL_ONGROUND : flags & ~FL_ONGROUND);
    } else {
        set_entvar(gPlayer, var_velocity, Float:{0.0, 0.0, 0.0});
        set_entvar(gPlayer, var_movetype, MOVETYPE_NONE);
    }
    return FMRES_IGNORED;
}
public Command()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    new action[16], arg[32]; read_argv(1, action, charsmax(action));
    if (equal(action, "create") && !gPlayer) {
        gPlayer = engfunc(EngFunc_CreateFakeClient, "GC_NativeView");
        if (!gPlayer) return PLUGIN_HANDLED;
        new reject[128];
        dllfunc(DLLFunc_ClientConnect, gPlayer, "GC_NativeView", "127.0.0.1", reject);
        dllfunc(DLLFunc_ClientPutInServer, gPlayer);
        set_entvar(gPlayer, var_flags, get_entvar(gPlayer, var_flags) | FL_FAKECLIENT);
        set_member(gPlayer, m_iJoiningState, JOINED); set_member(gPlayer, m_bJustConnected, false);
        set_member(gPlayer, m_iMenu, Menu_OFF); rg_set_user_team(gPlayer, TEAM_CT); rg_round_respawn(gPlayer);
    }
    if (!gPlayer) return PLUGIN_HANDLED;
    if (equal(action, "movement") && read_argc() == 7) {
        for (new i; i < 3; i++) { read_argv(2 + i, arg, charsmax(arg)); gVelocity[i] = str_to_float(arg); }
        read_argv(5, arg, charsmax(arg)); gGrounded = str_to_num(arg);
        read_argv(6, arg, charsmax(arg)); gMoveType = str_to_num(arg);
        gMovement = 1; gPin = 1; Frame();
    }
    if (equal(action, "pose") && read_argc() == 8) {
        gMovement = 0;
        for (new i; i < 3; i++) {
            read_argv(2 + i, arg, charsmax(arg)); gOrigin[i] = str_to_float(arg);
            read_argv(5 + i, arg, charsmax(arg)); gOffset[i] = str_to_float(arg);
        }
        gPin = 1; Frame();
    }
    if (equal(action, "brush")) {
        read_argv(2, arg, charsmax(arg)); new contents = str_to_num(arg);
        if (pev_valid(gWater)) { engfunc(EngFunc_RemoveEntity, gWater); gWater = 0; }
        if (contents) {
            // cs_assault's pinned *11 door is a real convex brush. Relocate its
            // model centre to the chosen eye and use GoldSrc's liquid skin ABI.
            gWater = rg_create_entity("info_target");
            set_entvar(gWater, var_classname, "gc_view_liquid");
            set_entvar(gWater, var_movetype, MOVETYPE_NONE); set_entvar(gWater, var_solid, SOLID_NOT);
            set_entvar(gWater, var_skin, contents); engfunc(EngFunc_SetModel, gWater, "*11");
            new Float:lo[3], Float:hi[3], Float:at[3];get_entvar(gWater, var_mins, lo);get_entvar(gWater, var_maxs, hi);
            for (new i; i < 3; i++) at[i] = gOrigin[i] + gOffset[i] - (lo[i] + hi[i]) * 0.5;
            engfunc(EngFunc_SetOrigin, gWater, at);
        }
    }
    if (equal(action, "kill")) { gPin = 0; user_kill(gPlayer, 1); }
    if (equal(action, "respawn")) { rg_round_respawn(gPlayer); gPin = 1; Frame(); }
    new Float:at[3], Float:offset[3];get_entvar(gPlayer, var_origin, at);get_entvar(gPlayer, var_view_ofs, offset);
    for (new i; i < 3; i++) at[i] += offset[i];
    server_print("[GC View] slot=%d eye=%.5f,%.5f,%.5f contents=%d alive=%d",gPlayer,at[0],at[1],at[2],engfunc(EngFunc_PointContents,at),is_user_alive(gPlayer));
    return PLUGIN_HANDLED;
}
