#include <amxmodx>
#include <amxmisc>
#include <reapi>
#include <goldcraft>

new gChanges, gSpawns, gFormMenu, gMenuOpens, gMenuSelections;
public plugin_init()
{
    register_plugin("GoldCraft forms", "0.2.0", "GoldCraft contributors");
    if (gc_api_version() != 1) set_fail_state("GoldCraft server API 1 is required");
    if (!is_rehlds() || !is_regamedll()) set_fail_state("ReAPI must resolve ReHLDS and ReGameDLL");
    RegisterHookChain(RG_CBasePlayer_Spawn, "OnPlayerSpawn", true);
    register_clcmd("say /mc", "ToggleForm");
    register_clcmd("say_team /mc", "ToggleForm");
    register_clcmd("say /cs", "NativeForm");
    register_clcmd("goldcraft_menu", "OpenFormMenu");
    register_clcmd("say /forms", "OpenFormMenu");
    register_clcmd("say_team /forms", "OpenFormMenu");
    gFormMenu = register_menuid("GoldCraft forms");
    register_menucmd(gFormMenu, MENU_KEY_1 | MENU_KEY_2 | MENU_KEY_0, "SelectFormMenu");
    register_concmd("amx_gc_form", "AdminForm", ADMIN_CFG, "<#userid> <0=CS|1=Minecraft>");
    register_srvcmd("gc_amxx_status", "ServerStatus");
}
// AMXX 1.9.0.5303 amxmodx.inc + util.cpp: the title identifies the server
// callback; the key mask controls selection. Recheck authority when selected.
public OpenFormMenu(id)
{
    if (!is_user_connected(id)) return PLUGIN_HANDLED;
    new current = gc_get_form(id), enabled = get_cvar_num("goldcraft_allow_switch");
    new paired = gc_is_paired(id), keys = MENU_KEY_0, body[384], used;
    used = formatex(body, charsmax(body), "\yGoldCraft forms^n\wCurrent: %s^n^n", current == GC_FORM_MINECRAFT ? "Minecraft" : "CS 1.6");
    if (enabled && current != GC_FORM_CS) keys |= MENU_KEY_1;
    if (enabled && paired && current != GC_FORM_MINECRAFT) keys |= MENU_KEY_2;
    used += formatex(body[used], charsmax(body)-used, "%s1. CS 1.6%s^n", (keys & MENU_KEY_1) ? "\w" : "\d", current == GC_FORM_CS ? " (active)" : "");
    used += formatex(body[used], charsmax(body)-used, "%s2. Minecraft%s^n^n", (keys & MENU_KEY_2) ? "\w" : "\d", !paired ? " (not connected)" : current == GC_FORM_MINECRAFT ? " (active)" : "");
    if (!enabled) used += formatex(body[used], charsmax(body)-used, "\dSwitching disabled by server^n^n");
    formatex(body[used], charsmax(body)-used, "\w0. Close   |   F6 / Esc");
    show_menu(id, keys, body, -1, "GoldCraft forms");
    gMenuOpens++;
    return PLUGIN_HANDLED;
}
public SelectFormMenu(id, key)
{
    show_menu(id, 0, "", 1);
    if (key != 0 && key != 1) return PLUGIN_HANDLED;
    if (!get_cvar_num("goldcraft_allow_switch")) return PLUGIN_HANDLED;
    new result = gc_set_form(id, key == 0 ? GC_FORM_CS : GC_FORM_MINECRAFT);
    if (result < 0) {
        client_print(id, print_chat, "[GoldCraft] Form change unavailable (status %d).", result);
        OpenFormMenu(id);
    } else gMenuSelections++;
    return PLUGIN_HANDLED;
}
// Signature and post-hook semantics: ReAPI 5.29.0.358 hook_callback.cpp / reapi_gamedll_const.inc.
public OnPlayerSpawn(const id)
{
    if (is_user_alive(id)) gSpawns++;
    return HC_CONTINUE;
}
public ToggleForm(id)
{
    if (!get_cvar_num("goldcraft_allow_switch")) return PLUGIN_HANDLED;
    new result = gc_set_form(id, gc_get_form(id) == GC_FORM_MINECRAFT ? GC_FORM_CS : GC_FORM_MINECRAFT);
    if (result < 0) client_print(id, print_chat, "[GoldCraft] Cannot change form (status %d). Connect the paired Minecraft client first.", result);
    return PLUGIN_HANDLED;
}
public NativeForm(id)
{
    if (get_cvar_num("goldcraft_allow_switch")) gc_set_form(id, GC_FORM_CS);
    return PLUGIN_HANDLED;
}
public AdminForm(id, level, cid)
{
    if (!cmd_access(id, level, cid, 3)) return PLUGIN_HANDLED;
    new who[16], value[8]; read_argv(1, who, charsmax(who)); read_argv(2, value, charsmax(value));
    if (who[0] != '#' || (!equal(value, "0") && !equal(value, "1"))) {
        console_print(id, "Usage: amx_gc_form #userid <0|1>"); return PLUGIN_HANDLED;
    }
    new target = find_player("k", str_to_num(who[1]));
    new result = gc_set_form(target, str_to_num(value));
    console_print(id, "[GoldCraft] userid=%d slot=%d form=%d result=%d", str_to_num(who[1]), target, str_to_num(value), result);
    return PLUGIN_HANDLED;
}
public goldcraft_form_changed(id, previous, current)
{
    gChanges++;
    client_print(id, print_chat, "[GoldCraft] Form: %s", current == GC_FORM_MINECRAFT ? "Minecraft (I: inventory; normal Use key: CS interaction)" : "Counter-Strike");
    log_amx("form slot=%d userid=%d previous=%d current=%d", id, get_user_userid(id), previous, current);
}
public ServerStatus()
{
    server_print("[GoldCraft AMXX] version=0.2.0 api=%d rehlds=%d regamedll=%d changes=%d spawns=%d menus=%d selections=%d", gc_api_version(), is_rehlds(), is_regamedll(), gChanges, gSpawns, gMenuOpens, gMenuSelections);
    for (new id=1; id<=get_maxplayers(); id++) if (is_user_connected(id)) {
        new weapon=get_member(id, m_pActiveItem);
        new clip=is_nullent(weapon) ? -1 : get_member(weapon, m_Weapon_iClip);
        new menu, keys; get_user_menu(id, menu, keys);
        server_print("[GoldCraft AMXX] slot=%d userid=%d paired=%d form=%d team=%d health=%.3f weapon=%d clip=%d formmenu=%d", id, get_user_userid(id), gc_is_paired(id), gc_get_form(id), get_member(id,m_iTeam), Float:get_entvar(id,var_health), weapon, clip, menu == gFormMenu);
    }
    return PLUGIN_HANDLED;
}
