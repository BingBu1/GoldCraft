// Independent native lifecycle regression; never enable on a public server.
#include <amxmodx>
#include <fakemeta>
#include <reapi>
#include <zp50_class_zombie>

new const REPORT[] = "addons/amxmodx/logs/goldcraft-zp-disconnect.jsonl";
new gSlot, gLiveCallbacks, gRemovals, gOldUserid;
new bool:gObserve, bool:gForceRage, bool:gDisconnectClean;

public plugin_init()
{
    register_plugin("GoldCraft ZP disconnect regression", "0.1", "GoldCraft contributors");
    register_srvcmd("gc_zp_disconnect_abort", "AbortConnection");
    register_srvcmd("gc_zp_disconnect_live", "ConnectedLifecycle");
}

Check(const name[], bool:passed)
{
    new file = fopen(REPORT, "at");
    if (file) {
        fprintf(file, "{^"name^":^"%s^",^"passed^":%d}^n", name, passed);
        fclose(file);
    }
    server_print("[GC ZP disconnect] %s %s", passed ? "PASS" : "FAIL", name);
}

CreatePlayer(const name[])
{
    new reject[128], id = engfunc(EngFunc_CreateFakeClient, name);
    if (!id) return 0;
    dllfunc(DLLFunc_ClientConnect, id, name, "127.0.0.1", reject);
    dllfunc(DLLFunc_ClientPutInServer, id);
    return id;
}

public AbortConnection()
{
    new file = fopen(REPORT, "wt");
    if (file) fclose(file);
    Check("server_has_32_slots", get_maxplayers() == 32);
    for (new n = 1; n <= 25; n++) {
        new name[32];
        formatex(name, charsmax(name), "GC_Disconnect_Filler_%d", n);
        new id = CreatePlayer(name);
        if (id != n || !is_user_connected(id)) {
            Check("25_connected_occupants", false);
            return PLUGIN_HANDLED;
        }
    }
    Check("25_connected_occupants", true);
    gSlot = 26;
    Check("slot_26_not_in_game", !is_user_connected(gSlot));
    // Exercise the actual plugin callback with AMXX's not-in-game state.
    // This is a callback boundary adapter, not a real network connect/abort:
    // AMXX auto-enters CreateFakeClient before ClientPutInServer is called.
    new started = callfunc_begin("client_disconnected", "zp50_class_zombie_rage.amxx");
    Check("production_disconnect_callback_found", started == 1);
    if (started != 1) return PLUGIN_HANDLED;
    callfunc_push_int(gSlot);
    callfunc_end();
    Check("callback_returns_without_admitting_player", !is_user_connected(gSlot));
    return PLUGIN_HANDLED;
}

public ConnectedLifecycle()
{
    new id = CreatePlayer("GC_Rage_Replacement");
    Check("slot_26_enters_after_callback", id == gSlot && is_user_connected(id));
    if (id != gSlot || !is_user_connected(id)) return PLUGIN_HANDLED;
    set_entvar(id, var_flags, get_entvar(id, var_flags) | FL_FAKECLIENT);
    set_member(id, m_iJoiningState, JOINED);
    set_member(id, m_bJustConnected, false);
    set_member(id, m_iMenu, Menu_OFF);
    rg_set_user_team(id, TEAM_CT, MODEL_CT_URBAN);
    rg_round_respawn(id);
    Check("replacement_spawned_as_human", is_user_alive(id) && !zp_core_is_zombie(id));
    new rage = zp_class_zombie_get_id("Rage Zombie");
    Check("rage_class_registered", rage != ZP_INVALID_ZOMBIE_CLASS);
    if (rage == ZP_INVALID_ZOMBIE_CLASS) return PLUGIN_HANDLED;
    gForceRage = true;
    zp_class_zombie_set_next(id, rage);
    zp_core_force_infect(id);
    Check("rage_infection_applies_green_glow", Glow(id));
    zp_core_force_cure(id);
    Check("cure_restores_normal_rendering", get_entvar(id, var_renderfx) == kRenderFxNone);
    zp_core_force_infect(id);
    Check("reinfection_restores_glow", Glow(id));
    gOldUserid = get_user_userid(id);
    gRemovals = 0;
    gObserve = true;
    server_cmd("kick #%d", gOldUserid);
    server_exec();
    gObserve = false;
    Check("connected_disconnect_restores_rendering", gLiveCallbacks == 1 && gDisconnectClean);
    Check("connected_client_removed", gRemovals == 1 && !is_user_connected(id));
    id = CreatePlayer("GC_After_Rage");
    Check("second_reuse_has_new_userid", id == gSlot && get_user_userid(id) != gOldUserid);
    Check("new_occupant_has_no_rage_or_glow", is_user_connected(id)
        && zp_class_zombie_get_current(id) == ZP_INVALID_ZOMBIE_CLASS
        && get_entvar(id, var_renderfx) == kRenderFxNone);
    return PLUGIN_HANDLED;
}

bool:Glow(id)
{
    new Float:color[3];
    get_entvar(id, var_rendercolor, color);
    return zp_class_zombie_get_current(id) == zp_class_zombie_get_id("Rage Zombie")
        && get_entvar(id, var_renderfx) == kRenderFxGlowShell
        && color[0] == 0.0 && color[1] == 255.0 && color[2] == 0.0
        && Float:get_entvar(id, var_renderamt) == 15.0;
}

public zp_fw_class_zombie_select_pre(id, classid)
{
    // Retain the production Bot class selector, making this test deterministic.
    if (gForceRage && id == gSlot && classid != zp_class_zombie_get_id("Rage Zombie"))
        return ZP_CLASS_NOT_AVAILABLE;
    return ZP_CLASS_AVAILABLE;
}

public client_disconnected(id)
{
    if (!gObserve || id != gSlot) return;
    if (is_user_connected(id)) {
        gLiveCallbacks++;
        // Loaded after Rage: inspect its cleanup before the engine resets the slot.
        gDisconnectClean = get_entvar(id, var_renderfx) == kRenderFxNone;
    }
}

public client_remove(id)
{
    if (gObserve && id == gSlot && !is_user_connected(id)) gRemovals++;
}
