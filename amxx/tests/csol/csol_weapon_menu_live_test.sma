// SPDX-License-Identifier: GPL-3.0-or-later
// Temporary local-server fixture. Weapons retain their native GiveNamedItem path.
#include <amxmodx>
#include <reapi>

new const MENU_TITLE[] = "CSOL weapon menu live test";
new const WEAPONS[][] = {"weapon_cannon", "weapon_tknifeex2", "weapon_hegrenade"};
new g_Enabled, g_Slot, g_Userid, g_SavedMoney;
new bool:g_SavedGod, bool:g_MenuShown;
new Float:g_SavedDamage;

public plugin_init()
{
    register_plugin("CSOL weapon menu live checks", "0.1.0", "GoldCraft contributors");
    g_Enabled = create_cvar("csol_wmenu_test", "0");
    hook_cvar_change(g_Enabled, "EnabledChanged");
    register_srvcmd("csol_wmenu_bind", "Bind");
    register_srvcmd("csol_wmenu_give", "Give");
    register_srvcmd("csol_wmenu_menu", "Menu");
    register_srvcmd("csol_wmenu_status", "Status");
    register_srvcmd("csol_wmenu_restore", "RestoreCommand");
    register_menucmd(register_menuid(MENU_TITLE), MENU_KEY_1 | MENU_KEY_0, "MenuSelected");
    register_clcmd("menuselect", "ObserveMenuSelect");
    RegisterHookChain(RG_CBasePlayer_Spawn, "BeforeSpawn", false);
}

bool:Human(index, userid)
{
    return index >= 1 && index <= MaxClients && is_user_connected(index) &&
        !is_user_bot(index) && !is_user_hltv(index) && get_user_userid(index) == userid;
}

bool:Current() { return g_Userid > 0 && Human(g_Slot, g_Userid); }
bool:Ready() { return get_pcvar_num(g_Enabled) != 0 && Current(); }

Restore(const reason[])
{
    if (!g_Userid) return;
    new index = g_Slot, userid = g_Userid, savedMoney = g_SavedMoney;
    new bool:savedGod = g_SavedGod, bool:closeMenu = g_MenuShown;
    new Float:savedDamage = g_SavedDamage;
    g_Slot = 0; g_Userid = 0; g_MenuShown = false;
    if (!Human(index, userid))
    {
        server_print("[CSOL wmenu] restore_skipped reason=%s old_slot=%d old_userid=%d identity_changed=1", reason, index, userid);
        return;
    }
    if (closeMenu) show_menu(index, 0, "", 0, MENU_TITLE);
    if (!Human(index, userid))
    {
        server_print("[CSOL wmenu] restore_skipped reason=menu_callback old_slot=%d old_userid=%d identity_changed=1", index, userid);
        return;
    }
    new flags = get_entvar(index, var_flags);
    set_entvar(index, var_flags, savedGod ? flags | FL_GODMODE : flags & ~FL_GODMODE);
    set_entvar(index, var_takedamage, savedDamage);
    rg_add_account(index, savedMoney, AS_SET, false);
    server_print("[CSOL wmenu] restored reason=%s slot=%d userid=%d saved_money=%d current_money=%d current=%d inventory_retained=1",
        reason, index, userid, savedMoney, Human(index, userid) ? get_member(index, m_iAccount) : -1, Human(index, userid));
}

public Bind()
{
    if (!get_pcvar_num(g_Enabled) || read_argc() != 3) return PLUGIN_HANDLED;
    new slotText[12], userText[12]; read_argv(1, slotText, charsmax(slotText)); read_argv(2, userText, charsmax(userText));
    new index = str_to_num(slotText), userid = str_to_num(userText);
    if (userid <= 0 || !Human(index, userid) || !is_user_alive(index))
    {
        server_print("[CSOL wmenu] bind_refused expected_alive_human_slot_and_userid=1");
        return PLUGIN_HANDLED;
    }
    Restore("rebind");
    if (!Human(index, userid) || !is_user_alive(index)) return PLUGIN_HANDLED;
    g_Slot = index; g_Userid = userid;
    g_SavedMoney = get_member(index, m_iAccount);
    g_SavedGod = (get_entvar(index, var_flags) & FL_GODMODE) != 0;
    g_SavedDamage = Float:get_entvar(index, var_takedamage);
    set_entvar(index, var_flags, get_entvar(index, var_flags) | FL_GODMODE);
    set_entvar(index, var_takedamage, DAMAGE_NO);
    rg_add_account(index, 32000, AS_SET, false);
    server_print("[CSOL wmenu] bound slot=%d userid=%d saved_money=%d saved_god=%d saved_takedamage=%.2f current=%d",
        index, userid, g_SavedMoney, g_SavedGod, g_SavedDamage, Current());
    return Status();
}

public Give()
{
    if (!Ready() || !is_user_alive(g_Slot)) return PLUGIN_HANDLED;
    for (new i; i < sizeof WEAPONS; i++)
    {
        if (!Ready() || !is_user_alive(g_Slot)) break;
        new entity = rg_give_item(g_Slot, WEAPONS[i], GT_APPEND);
        server_print("[CSOL wmenu] give classname=%s entity=%d current=%d native_path=1 inventory_retained=1", WEAPONS[i], entity, Current());
    }
    return Status();
}

public Menu()
{
    if (!Ready() || read_argc() != 2) return PLUGIN_HANDLED;
    new action[16]; read_argv(1, action, charsmax(action));
    if (equal(action, "exit"))
    {
        g_MenuShown = true;
        show_menu(g_Slot, MENU_KEY_1 | MENU_KEY_0, "\yCSOL 切枪测试^n^n\w1. 关闭菜单^n0. 关闭菜单^n^n请使用实体 Escape 或数字键。", -1, MENU_TITLE);
    }
    else if (equal(action, "view"))
    {
        g_MenuShown = true;
        show_menu(g_Slot, 0, "\yCSOL 切枪测试^n^n\w仅展示，数字键没有菜单选项。^n请使用实体 Escape 关闭。", -1, MENU_TITLE);
    }
    else if (equal(action, "close"))
    {
        show_menu(g_Slot, 0, "", 0, MENU_TITLE); g_MenuShown = false;
    }
    else return PLUGIN_HANDLED;
    server_print("[CSOL wmenu] menu action=%s slot=%d userid=%d time=-1 physical_input_required=1", action, g_Slot, g_Userid);
    return PLUGIN_HANDLED;
}

public MenuSelected(index, key)
{
    if (!Ready() || index != g_Slot) return PLUGIN_HANDLED;
    server_print("[CSOL wmenu] menu_callback slot=%d userid=%d key=%d", index, g_Userid, key == 9 ? 0 : key + 1);
    show_menu(index, 0, "", 0, MENU_TITLE); g_MenuShown = false;
    return PLUGIN_HANDLED;
}

public ObserveMenuSelect(index)
{
    if (!Ready() || index != g_Slot) return PLUGIN_CONTINUE;
    new text[16]; read_argv(1, text, charsmax(text));
    server_print("[CSOL wmenu] observed_menuselect slot=%d userid=%d value=%s", index, g_Userid, text);
    return PLUGIN_CONTINUE;
}

public Status()
{
    if (!Current())
    {
        server_print("[CSOL wmenu] unbound_or_disconnected slot=%d userid=%d enabled=%d", g_Slot, g_Userid, get_pcvar_num(g_Enabled));
        return PLUGIN_HANDLED;
    }
    new index = g_Slot, active = get_member(index, m_pActiveItem), classname[64];
    if (!is_nullent(active)) get_entvar(active, var_classname, classname, charsmax(classname));
    server_print("[CSOL wmenu] status slot=%d userid=%d alive=%d health=%.1f deadflag=%d god=%d takedamage=%.2f money=%d menu=%d active=%d classname=%s",
        index, g_Userid, is_user_alive(index), Float:get_entvar(index, var_health), get_entvar(index, var_deadflag),
        (get_entvar(index, var_flags) & FL_GODMODE) != 0, Float:get_entvar(index, var_takedamage),
        get_member(index, m_iAccount), get_member(index, m_iMenu), active, classname);
    for (new slot; slot < 6; slot++)
    {
        new entity = get_member(index, m_rgpPlayerItems, slot), count;
        while (!is_nullent(entity) && count++ < 64)
        {
            get_entvar(entity, var_classname, classname, charsmax(classname));
            server_print("[CSOL wmenu] inventory slot=%d entity=%d id=%d owner=%d active=%d classname=%s",
                slot, entity, get_member(entity, m_iId), get_member(entity, m_pPlayer), entity == active, classname);
            entity = get_member(entity, m_pNext);
        }
        if (!is_nullent(entity)) server_print("[CSOL wmenu] inventory_traversal_limit slot=%d", slot);
    }
    return PLUGIN_HANDLED;
}

public RestoreCommand() { Restore("command"); return PLUGIN_HANDLED; }
public EnabledChanged(pcvar, const oldValue[], const newValue[]) { if (!str_to_num(newValue)) Restore("disabled"); }
public BeforeSpawn(index) { if (index == g_Slot && Current()) Restore("before_spawn"); }
public client_disconnected(index) { if (index == g_Slot) Restore("disconnect"); }
public plugin_end() { Restore("plugin_end"); }
