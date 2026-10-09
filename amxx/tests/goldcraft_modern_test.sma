// Isolated ReHLDS fixture. Menu rendering/lifecycle use AMXX's actual natives.
// Positive selections invoke the real plugin callback with copied item data;
// they do not claim network menuselect or graphical keyboard acceptance.
#include <amxmodx>
#include <goldcraft_menus>
#include <goldcraft>
#include <hamsandwich>
#include <amx_settings_api>
#include <cs_ham_bots_api>
#include <zp50_core>
#include <zp50_gamemodes>
#include <zp50_buy_menus>
#include <zp50_admin_menu>
#include <zp50_class_zombie>
#include <zp50_class_human>

new const REPORT[] = "addons/amxmodx/logs/goldcraft-modern.jsonl";
new const SETTINGS[] = "gc-modern-test.ini";
new gEnabled, gPlayer, gOther, gChecks, gFailed, gMenuCloses, gLastStatus;
new gRoundEnds, gDisconnects, gOldUserId, gStaleMenu = -1;
new gZpSpawns, gPrematureZpSpawns;
new bool:gRejectRound;

public plugin_init()
{
    register_plugin("GoldCraft AMXX modernization tests", "0.1", "GoldCraft contributors");
    gEnabled = create_cvar("goldcraft_modern_test", "0");
    register_srvcmd("gc_modern_start", "Start");
    register_srvcmd("gc_modern_menus", "Menus");
    register_srvcmd("gc_modern_timeout", "Timeout");
    register_srvcmd("gc_modern_round", "RoundEnd");
    register_srvcmd("gc_modern_disconnect", "Disconnect");
    register_srvcmd("gc_modern_reconnect", "Reconnect");
    register_srvcmd("gc_modern_admin", "AdminDatabase");
    RegisterHookChain(RG_RoundEnd, "BeforeRoundEnd", false);
}

public zp_fw_core_spawn_post(id)
{
    gZpSpawns++;
    if (get_member(id, m_bJustConnected) || get_member(id, has_disconnected)) gPrematureZpSpawns++;
}

Check(const name[], const bool:passed)
{
    gChecks++;
    if (!passed) gFailed++;
    new file = fopen(REPORT, "at");
    if (file) {
        fprintf(file, "{^"name^":^"%s^",^"passed^":%d}^n", name, passed);
        fclose(file);
    }
    server_print("[GC Modern] %s %s", passed ? "PASS" : "FAIL", name);
}

Summary()
{
    server_print("[GC Modern] checks=%d failed=%d", gChecks, gFailed);
    return PLUGIN_HANDLED;
}

CreatePlayer(const name[], const TeamName:team)
{
    new reject[128], id = engfunc(EngFunc_CreateFakeClient, name);
    if (!id) return 0;
    dllfunc(DLLFunc_ClientConnect, id, name, "127.0.0.1", reject);
    dllfunc(DLLFunc_ClientPutInServer, id);
    set_entvar(id, var_flags, get_entvar(id, var_flags) | FL_FAKECLIENT);
    set_member(id, m_iJoiningState, JOINED);
    set_member(id, m_bJustConnected, false);
    rg_set_user_team(id, team, team == TEAM_CT ? MODEL_CT_URBAN : MODEL_T_TERROR);
    rg_round_respawn(id);
    return id;
}

ActiveMenu(const id)
{
    new legacy, menu;
    player_menu_info(id, legacy, menu);
    return menu;
}

KeyMask(const id)
{
    new legacy, keys;
    get_user_menu(id, legacy, keys);
    return keys;
}

// AMXX amxclient_cmd forwards registered commands, but not its internal menu
// dispatcher. Copy the published item payload, close through menu_cancel, then
// invoke the production selection handler on a non-displayed menu resource.
CopySelection(const id, const item)
{
    new active = ActiveMenu(id), access, info[64], label[192];
    if (active < 0 || !menu_item_getinfo(active, item, access, info, charsmax(info), label, charsmax(label))) return -1;
    new menu = menu_create("controlled selection", "UnusedSelection");
    for (new index = 0; index < item; index++) menu_addblank2(menu);
    menu_additem(menu, label, info);
    menu_cancel(id);
    return menu;
}

DispatchSelection(const id, const menu, const plugin[], const handler[])
{
    if (menu < 0) { Check("selection_has_real_item", false); return; }
    if (callfunc_begin(handler, plugin) != 1) {
        menu_destroy(menu);
        Check("production_menu_callback_exists", false);
        return;
    }
    callfunc_push_int(id);
    callfunc_push_int(menu);
    callfunc_push_int(menu_items(menu) - 1);
    callfunc_end();
}

Select(const id, const item, const plugin[], const handler[])
{
    DispatchSelection(id, CopySelection(id, item), plugin, handler);
}

public UnusedSelection(id, menu, item)
{
    menu_destroy(menu);
    return PLUGIN_HANDLED;
}

public OwnSelection(id, menu, item)
{
    gMenuCloses++;
    gLastStatus = item;
    gc_menu_release(menu);
    return PLUGIN_HANDLED;
}

OwnMenu(const id, const seconds = -1)
{
    new menu = gc_menu_fixed(id, "modern menu lifetime", "OwnSelection", "退出");
    gc_menu_item(menu, "enabled", 7);
    gc_menu_item(menu, "disabled", 8, false);
    gc_menu_display_fixed(id, menu, seconds);
    return menu;
}

public Start()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    gPlayer = CreatePlayer("GC_Modern_Menu", TEAM_CT);
    gOther = CreatePlayer("GC_Modern_Target", TEAM_TERRORIST);
    Check("actual_native_players_alive", bool:(is_user_alive(gPlayer) && is_user_alive(gOther)));
    Check("zp_spawn_skips_connect_initialization", gZpSpawns == 2 && gPrematureZpSpawns == 0);
    TestSettings();
    TestMembers();
    new hook = RegisterHamBots(Ham_Spawn, "CompatSpawn");
    Check("typed_cz_compat_record_registered", hook >= 0);
    Check("pending_cz_hook_disable", bool:DisableHamForwardBots(hook));
    Check("pending_cz_hook_enable", bool:EnableHamForwardBots(hook));
    Check("pending_cz_hook_enable_idempotent", bool:EnableHamForwardBots(hook));
    return Summary();
}

public CompatSpawn(id) {}

// These are synthetic database records, never a simulated Steam login.
public AdminDatabase()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    new argument[8];
    read_argv(1, argument, charsmax(argument));
    new stage = str_to_num(argument), count = admins_num();
    Check("admin_base_loaded", is_plugin_loaded("Admin Base") >= 0);
    if (stage == 0) Check("admin_database_initial_record", count == 1);
    else if (stage == 1) Check("admin_append_and_duplicate_rejection", count == 2);
    else Check("admin_reload_without_accumulation", count == 2);

    if (count == (stage == 0 ? 1 : 2)) {
        new auth[44], password[32];
        admins_lookup(0, AdminProp_Auth, auth, charsmax(auth));
        admins_lookup(0, AdminProp_Password, password, charsmax(password));
        Check("admin_steam_record_preserved", equal(auth, "STEAM_0:0:0") && !password[0]
            && admins_lookup(0, AdminProp_Flags) == (FLAG_AUTHID | FLAG_NOPASS)
            && admins_lookup(0, AdminProp_Access) == read_flags("abcdefghijklmnopqrstuv"));
        if (stage > 0) {
            admins_lookup(1, AdminProp_Auth, auth, charsmax(auth));
            admins_lookup(1, AdminProp_Password, password, charsmax(password));
            Check("admin_command_writes_auth_flags", equal(auth, "STEAM_0:1:0") && !password[0]
                && admins_lookup(1, AdminProp_Flags) == (FLAG_AUTHID | FLAG_NOPASS)
                && admins_lookup(1, AdminProp_Access) == ADMIN_RCON);
        }
    }
    Check("unmatched_bot_has_no_admin_access", !(get_user_flags(gPlayer) & ADMIN_RCON)
        && !(get_user_flags(gOther) & ADMIN_RCON));
    return Summary();
}

TestSettings()
{
    new path[PLATFORM_MAX_PATH];
    get_configsdir(path, charsmax(path));
    add(path, charsmax(path), "/"); add(path, charsmax(path), SETTINGS);
    delete_file(path);
    new scalar, Float:real, value[128];
    Check("settings_new_file_integer", bool:amx_save_setting_int(SETTINGS, "one", "NUMBER", 37));
    Check("settings_integer_roundtrip", bool:(amx_load_setting_int(SETTINGS, "one", "NUMBER", scalar) && scalar == 37));
    amx_save_setting_string(SETTINGS, "two", "TEXT", "中文 value");
    amx_save_setting_float(SETTINGS, "one", "REAL", 1.25);
    amx_save_setting_int(SETTINGS, "one", "NUMBER", -19);
    Check("settings_replace_existing_key", bool:(amx_load_setting_int(SETTINGS, "one", "NUMBER", scalar) && scalar == -19));
    Check("settings_insert_keeps_next_section", bool:(amx_load_setting_string(SETTINGS, "two", "TEXT", value, charsmax(value)) && equal(value, "中文 value")));
    Check("settings_float_roundtrip", bool:(amx_load_setting_float(SETTINGS, "one", "REAL", real) && floatabs(real - 1.25) < 0.001));
    new Array:values = ArrayCreate(1), Array:loaded = ArrayCreate(1);
    ArrayPushCell(values, 17); ArrayPushCell(values, -2); ArrayPushCell(values, 65536);
    amx_save_setting_int_arr(SETTINGS, "arrays", "INTS", values);
    amx_load_setting_int_arr(SETTINGS, "arrays", "INTS", loaded);
    Check("settings_int_array_includes_last_token", bool:(ArraySize(loaded) == 3 && ArrayGetCell(loaded, 2) == 65536));
    ArrayClear(values); ArrayClear(loaded);
    ArrayPushCell(values, 1.25); ArrayPushCell(values, -2.75);
    amx_save_setting_float_arr(SETTINGS, "arrays", "FLOATS", values);
    amx_load_setting_float_arr(SETTINGS, "arrays", "FLOATS", loaded);
    Check("settings_float_array", bool:(ArraySize(loaded) == 2 && floatabs(Float:ArrayGetCell(loaded, 1) + 2.75) < 0.001));
    ArrayClear(values); ArrayClear(loaded);
    Check("settings_save_empty_array", bool:amx_save_setting_int_arr(SETTINGS, "arrays", "EMPTY", values));
    Check("settings_load_empty_array", bool:(amx_load_setting_int_arr(SETTINGS, "arrays", "EMPTY", loaded) && ArraySize(loaded) == 0));
    ArrayDestroy(values); ArrayDestroy(loaded);
    values = ArrayCreate(128); loaded = ArrayCreate(128);
    ArrayPushString(values, "alpha"); ArrayPushString(values, "中文路径"); ArrayPushString(values, "omega");
    amx_save_setting_string_arr(SETTINGS, "arrays", "STRINGS", values);
    amx_load_setting_string_arr(SETTINGS, "arrays", "STRINGS", loaded);
    if (ArraySize(loaded) > 2) ArrayGetString(loaded, 2, value, charsmax(value));
    Check("settings_string_array_last_token", bool:(ArraySize(loaded) == 3 && equal(value, "omega")));
    if (ArraySize(loaded) > 1) ArrayGetString(loaded, 1, value, charsmax(value));
    Check("settings_string_array_utf8", bool:equal(value, "中文路径"));
    ArrayDestroy(values); ArrayDestroy(loaded);
}

TestMembers()
{
    new id = gPlayer;
    rg_give_item(id, "weapon_ak47");
    gc_set_bpammo(id, WEAPON_AK47, 117);
    Check("ammo_owned_roundtrip", gc_get_bpammo(id, WEAPON_AK47) == 117);
    rg_remove_item(id, "weapon_ak47", false);
    Check("ammo_read_after_last_weapon_removed", gc_get_bpammo(id, WEAPON_AK47) == 117);
    gc_set_bpammo(id, WEAPON_AK47, 0);
    Check("ammo_clear_after_last_weapon_removed", get_member(id, m_rgAmmo, rg_get_weapon_info(WEAPON_AK47, WI_AMMO_TYPE)) == 0);
    rg_give_item(id, "weapon_flashbang"); rg_give_item(id, "weapon_flashbang");
    rg_remove_item(id, "weapon_flashbang", true);
    Check("strip_removes_all_grenades_and_ammo", !rg_find_weapon_bpack_by_name(id, "weapon_flashbang") && gc_get_bpammo(id, WEAPON_FLASHBANG) == 0);
    gc_set_health(id, 375);
    Check("health_named_float_member", floatabs(Float:get_entvar(id, var_health) - 375.0) < 0.001);
    gc_set_rendering(id, kRenderFxGlowShell, 12, 34, 56, kRenderTransAlpha, 123);
    new Float:color[3]; get_entvar(id, var_rendercolor, color);
    Check("render_fields_keep_float_values", floatabs(color[1] - 34.0) < 0.001 && floatabs(Float:get_entvar(id, var_renderamt) - 123.0) < 0.001);
    gc_set_rendering(id);
    Check("render_default_clears_glow", get_entvar(id, var_renderfx) == kRenderFxNone && get_entvar(id, var_rendermode) == kRenderNormal);
    new entity = rg_create_entity("info_target");
    new Float:origin[3], Float:actual[3], Float:mins[3] = {-4.0, -4.0, -4.0}, Float:maxs[3] = {4.0, 4.0, 4.0};
    get_entvar(id, var_origin, origin); origin[0] += 48.0;
    set_entvar(entity, var_solid, SOLID_BBOX);
    engfunc(EngFunc_SetSize, entity, mins, maxs);
    gc_relink_origin(entity, origin);
    get_entvar(entity, var_absmin, actual);
    Check("origin_change_relinks_collision_bounds", floatabs(actual[0] - (origin[0] - 5.0)) < 0.01);
    Check("sphere_end_uses_zero_sentinel", gc_find_in_sphere(0, Float:{99999.0, 99999.0, 99999.0}, 1.0) == 0);
    engfunc(EngFunc_RemoveEntity, entity);
}

public Menus()
{
    if (!get_pcvar_num(gEnabled) || !is_user_alive(gPlayer)) return PLUGIN_HANDLED;
    new id = gPlayer, before = gMenuCloses;
    OwnMenu(id);
    Check("newmenu_disabled_and_exit_keys", KeyMask(id) == (MENU_KEY_1 | MENU_KEY_0));
    new first = ActiveMenu(id), peak = first;
    for (new index = 0; index < 100; index++) peak = max(peak, OwnMenu(id));
    Check("replacement_closes_each_previous_menu_once", gMenuCloses - before == 100);
    Check("replacement_reuses_menu_resources", peak <= first + 1);
    amxclient_cmd(id, "goldcraft_menu");
    Check("cross_plugin_replacement_closes_previous", gMenuCloses - before == 101 && ActiveMenu(id) >= 0);
    Check("forms_unpaired_and_active_disabled", KeyMask(id) == MENU_KEY_0);
    Check("forms_original_nine_slots", menu_items(ActiveMenu(id)) == 9);
    menu_cancel(id);
    Check("forms_cancel_releases_menu", ActiveMenu(id) < 0);
    Check("fakeclient_identity_preserved", bool:is_user_bot(id));
    remove_user_flags(id, -1); set_user_flags(id, ADMIN_USER);
    amxclient_cmd(id, "say", "/zpmenu");
    Check("zp_main_disabled_admin", !(KeyMask(id) & MENU_KEY_9));
    Check("zp_main_original_keys", bool:(KeyMask(id) & MENU_KEY_1) && !(KeyMask(id) & MENU_KEY_8) && bool:(KeyMask(id) & MENU_KEY_0));
    Select(id, 0, "zp50_main_menu.amxx", "menu_main");
    Check("buy_menu_opened_from_main", ActiveMenu(id) >= 0 && menu_items(ActiveMenu(id)) == 9);
    if (ActiveMenu(id) < 0) return Summary();
    Select(id, 8, "zp50_buy_menus.amxx", "menu_buy_primary");
    if (ActiveMenu(id) < 0) { Check("primary_next_page_open", false); return Summary(); }
    new access, info[12], label[128];
    menu_item_getinfo(ActiveMenu(id), 0, access, info, charsmax(info), label, charsmax(label));
    Check("primary_next_page_keeps_local_action", bool:equal(info, "0"));
    Select(id, 0, "zp50_buy_menus.amxx", "menu_buy_primary");
    Check("primary_second_page_buys_m3", rg_find_weapon_bpack_by_name(id, "weapon_m3") > 0);
    Check("secondary_blanks_disable_seventh_and_ninth", !(KeyMask(id) & MENU_KEY_7) && !(KeyMask(id) & MENU_KEY_9) && bool:(KeyMask(id) & MENU_KEY_8));
    Select(id, 3, "zp50_buy_menus.amxx", "menu_buy_secondary");
    Check("secondary_selection_buys_deagle", rg_find_weapon_bpack_by_name(id, "weapon_deagle") > 0);
    set_user_flags(id, ADMIN_BAN | ADMIN_MENU | ADMIN_CFG | ADMIN_RCON);
    zp_admin_menu_show(id);
    Select(id, 0, "zp50_admin_menu.amxx", "menu_admin");
    new menu = ActiveMenu(id), found = -1;
    for (new index = 0; index < menu_items(menu); index++) {
        menu_item_getinfo(menu, index, access, info, charsmax(info));
        if (str_to_num(info) == get_user_userid(gOther)) found = index;
    }
    Check("admin_targets_identified_by_userid", found >= 0);
    if (found >= 0) gStaleMenu = CopySelection(id, found);
    amxclient_cmd(id, "amx_nmm");
    Check("nade_standalone_menu_without_menufront", ActiveMenu(id) >= 0 && is_plugin_loaded("Menus Front-End") < 0);
    Select(id, 2, "nademodes.amxx", "menu_handler");
    Check("nade_settings_submenu_opens", ActiveMenu(id) >= 0);
    menu_cancel(id);
    Check("nade_cancel_does_not_reopen", ActiveMenu(id) < 0);
    amxclient_cmd(id, "amx_nmm");
    new option = get_cvar_pointer("nademodes_enable");
    new optionBefore = get_pcvar_num(option), selection = CopySelection(id, 0);
    remove_user_flags(id, ADMIN_RCON);
    DispatchSelection(id, selection, "nademodes.amxx", "menu_handler");
    Check("nade_main_rechecks_revoked_permission", get_pcvar_num(option) == optionBefore && ActiveMenu(id) < 0);
    set_user_flags(id, ADMIN_RCON);
    amxclient_cmd(id, "amx_nmm");
    Select(id, 2, "nademodes.amxx", "menu_handler");
    option = get_cvar_pointer("nademodes_play_grenade_sounds");
    optionBefore = get_pcvar_num(option);
    selection = CopySelection(id, 2);
    remove_user_flags(id, ADMIN_RCON);
    DispatchSelection(id, selection, "nademodes.amxx", "cvar_menu_handler");
    Check("nade_setting_rechecks_revoked_permission", get_pcvar_num(option) == optionBefore && ActiveMenu(id) < 0);
    set_user_flags(id, ADMIN_RCON);
    zp_admin_menu_show(id);
    menu_cancel(id);
    Check("admin_cancel_does_not_reopen", ActiveMenu(id) < 0);
    OwnMenu(id, 1);
    return Summary();
}

public Timeout()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    menu_cancel(gPlayer);
    Check("menu_timeout_is_status_not_selection", gLastStatus == MENU_TIMEOUT && ActiveMenu(gPlayer) < 0);
    return Summary();
}

public BeforeRoundEnd(WinStatus:status, ScenarioEventEndRound:event, Float:delay)
{
    if (gRejectRound) {
        SetHookChainReturn(ATYPE_BOOL, false);
        return HC_SUPERCEDE;
    }
    return HC_CONTINUE;
}

public zp_fw_gamemodes_end(mode) { gRoundEnds++; }

public RoundEnd()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    new mode = zp_gamemodes_get_id("Infection Mode");
    zp_gamemodes_start(mode, gOther);
    Check("round_mode_started", zp_gamemodes_get_current() == mode);
    new before = gRoundEnds;
    gRejectRound = true;
    rg_round_end(5.0, WINSTATUS_TERRORISTS, ROUND_TERRORISTS_WIN, _, _, true);
    Check("rejected_round_end_preserves_mode", gRoundEnds == before && zp_gamemodes_get_current() == mode);
    gRejectRound = false;
    rg_round_end(5.0, WINSTATUS_TERRORISTS, ROUND_TERRORISTS_WIN, _, _, true);
    Check("accepted_round_end_runs_once", gRoundEnds == before + 1 && zp_gamemodes_get_current() == ZP_NO_GAME_MODE);
    return Summary();
}

public Disconnect()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    OwnMenu(gOther);
    gOldUserId = get_user_userid(gOther);
    server_cmd("kick #%d", gOldUserId); server_exec();
    return Summary();
}

public client_disconnected(id)
{
    gc_menu_close(id);
    if (id == gOther) gDisconnects++;
}

public Reconnect()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    Check("disconnect_callback_and_menu_release", gDisconnects == 1 && gGcMenus[gOther] == -1);
    new previous = gOther;
    new before = gZpSpawns;
    gOther = CreatePlayer("GC_Modern_Replacement", TEAM_TERRORIST);
    Check("actual_slot_reuse_has_new_userid", gOther == previous && get_user_userid(gOther) != gOldUserId);
    Check("reused_slot_has_only_completed_zp_spawn", gZpSpawns == before + 1 && gPrematureZpSpawns == 0);
    if (gStaleMenu >= 0) {
        DispatchSelection(gPlayer, gStaleMenu, "zp50_admin_menu.amxx", "menu_player_list");
        gStaleMenu = -1;
        Check("stale_admin_menu_does_not_infect_new_occupant", !zp_core_is_zombie(gOther));
    }
    return Summary();
}
