#include <amxmodx>
#include <goldcraft_menus>
#include <amxmisc>
#include <reapi>
#include <goldcraft>

new gChanges, gSpawns, gAllowSwitch, gMenuOpens, gMenuSelections;
public plugin_init()
{
    register_plugin("GoldCraft forms", "0.3.0", "GoldCraft contributors");
    if (gc_api_version() != 1) set_fail_state("GoldCraft server API 1 is required");
    if (!is_rehlds() || !is_regamedll()) set_fail_state("ReAPI must resolve ReHLDS and ReGameDLL");
    RegisterHookChain(RG_CBasePlayer_Spawn, "OnPlayerSpawn", true);
    register_clcmd("say /mc", "ToggleForm");
    register_clcmd("say_team /mc", "ToggleForm");
    register_clcmd("say /cs", "NativeForm");
    register_clcmd("goldcraft_menu", "OpenFormMenu");
    register_clcmd("say /forms", "OpenFormMenu");
    register_clcmd("say_team /forms", "OpenFormMenu");
    gAllowSwitch = get_cvar_pointer("goldcraft_allow_switch");
    if (!gAllowSwitch) set_fail_state("GoldCraft form policy cvar is missing");
    register_concmd("amx_gc_form", "AdminForm", ADMIN_CFG, "<#userid> <0=CS|1=Minecraft>");
    register_srvcmd("gc_amxx_status", "ServerStatus");
}
// Keep the title marker recognized by the native ShowMenu adapter.
// Availability is visual guidance; server authority is rechecked on selection.
public OpenFormMenu(id)
{
    if (!is_user_connected(id)) return PLUGIN_HANDLED;
    new current = gc_get_form(id), bool:enabled = bool:get_pcvar_num(gAllowSwitch);
    new bool:paired = bool:gc_is_paired(id), title[192], label[128];
    formatex(title, charsmax(title), "GoldCraft forms^n\w当前形态：%s%s", current == GC_FORM_MINECRAFT ? "Minecraft" : "CS 1.6", enabled ? "" : "^n\d服务器已关闭切换");
    new menu = gc_menu_fixed(id, title, "SelectFormMenu", "关闭   |   F6 / Esc");
    formatex(label, charsmax(label), "CS 1.6%s", current == GC_FORM_CS ? "（当前）" : "");
    gc_menu_item(menu, label, 0, enabled && current != GC_FORM_CS);
    formatex(label, charsmax(label), "Minecraft%s", !paired ? "（尚未连接）" : current == GC_FORM_MINECRAFT ? "（当前）" : "");
    gc_menu_item(menu, label, 1, enabled && paired && current != GC_FORM_MINECRAFT);
    gc_menu_display_fixed(id, menu);
    gMenuOpens++;
    return PLUGIN_HANDLED;
}
public SelectFormMenu(id, menu, item)
{
    new key = gc_menu_action(menu, item);
    if (!is_user_connected(id)) return PLUGIN_HANDLED;
    if (key != 0 && key != 1) return PLUGIN_HANDLED;
    if (!get_pcvar_num(gAllowSwitch)) return PLUGIN_HANDLED;
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
    if (!get_pcvar_num(gAllowSwitch)) return PLUGIN_HANDLED;
    new result = gc_set_form(id, gc_get_form(id) == GC_FORM_MINECRAFT ? GC_FORM_CS : GC_FORM_MINECRAFT);
    if (result < 0) client_print(id, print_chat, "[GoldCraft] Cannot change form (status %d). Connect the paired Minecraft client first.", result);
    return PLUGIN_HANDLED;
}
public NativeForm(id)
{
    if (get_pcvar_num(gAllowSwitch)) gc_set_form(id, GC_FORM_CS);
    return PLUGIN_HANDLED;
}
public AdminForm(id, level, cid)
{
    if (!cmd_access(id, level, cid, 3)) return PLUGIN_HANDLED;
    new who[16], value[8]; read_argv(1, who, charsmax(who)); read_argv(2, value, charsmax(value));
    if (who[0] != '#' || (!equal(value, "0") && !equal(value, "1"))) {
        console_print(id, "Usage: amx_gc_form #userid <0|1>"); return PLUGIN_HANDLED;
    }
    new target = find_player_ex(FindPlayer_MatchUserId, str_to_num(who[1]));
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
    server_print("[GoldCraft AMXX] version=0.3.0 api=%d rehlds=%d regamedll=%d changes=%d spawns=%d menus=%d selections=%d", gc_api_version(), is_rehlds(), is_regamedll(), gChanges, gSpawns, gMenuOpens, gMenuSelections);
    for (new id=1; id<=MaxClients; id++) if (is_user_connected(id)) {
        new weapon=get_member(id, m_pActiveItem);
        new clip=is_nullent(weapon) ? -1 : get_member(weapon, m_Weapon_iClip);
        server_print("[GoldCraft AMXX] slot=%d userid=%d paired=%d form=%d team=%d health=%.3f weapon=%d clip=%d formmenu=%d", id, get_user_userid(id), gc_is_paired(id), gc_get_form(id), get_member(id,m_iTeam), Float:get_entvar(id,var_health), weapon, clip, gc_menu_active(id));
    }
    return PLUGIN_HANDLED;
}

public client_disconnected(id)
{
    gc_menu_close(id);
}
