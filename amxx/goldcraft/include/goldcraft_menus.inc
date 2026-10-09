// AMXX 1.9 menu resources owned by each plugin and each connected player.
#if defined _goldcraft_menus_included
    #endinput
#endif
#define _goldcraft_menus_included
#include <goldcraft_amxx>

new gGcMenus[MAX_PLAYERS + 1] = {-1, ...};

stock gc_menu_release(const menu)
{
    for (new player = 1; player <= MaxClients; player++)
        if (gGcMenus[player] == menu) gGcMenus[player] = -1;
    menu_destroy(menu);
}

stock gc_menu_close(const player)
{
    new owned = gGcMenus[player];
    if (owned < 0) return;
    gGcMenus[player] = -1;
    new legacy, current;
    if (is_user_connected(player)) player_menu_info(player, legacy, current);
    else current = -1;
    if (current == owned)
        menu_cancel(player); // The handler releases its resource, including timeout.
    else
        gc_menu_release(owned);
}

stock gc_menu_create(const player, const title[], const handler[], const multilingual = 0)
{
    gc_menu_close(player);
    new menu = menu_create(title, handler, multilingual);
    gGcMenus[player] = menu;
    return menu;
}

stock gc_menu_fixed(const player, const title[], const handler[], const exitLabel[])
{
    new menu = gc_menu_create(player, title, handler);
    menu_setprop(menu, MPROP_PERPAGE, 0);
    menu_setprop(menu, MPROP_EXIT, MEXIT_FORCE);
    menu_setprop(menu, MPROP_EXITNAME, exitLabel);
    return menu;
}

stock gc_menu_item(const menu, const label[], const action, const bool:enabled = true)
{
    static disabled = -1;
    if (disabled < 0) disabled = menu_makecallback("gc_menu_disabled");
    new info[12];
    num_to_str(action, info, charsmax(info));
    menu_additem(menu, label, info, 0, enabled ? -1 : disabled);
}

stock gc_menu_item_ml(const menu, const player, const key[], const action, const bool:enabled = true)
{
    new label[128];
    formatex(label, charsmax(label), "%L", player, key);
    gc_menu_item(menu, label, action, enabled);
}

public gc_menu_disabled(const player, const menu, const item)
{
    return ITEM_DISABLED;
}

stock gc_menu_display_fixed(const player, const menu, const seconds = -1)
{
    // 1.9's blank2 consumes a real slot, so Exit stays on the user's 0 key.
    while (menu_items(menu) < 9) menu_addblank2(menu);
    set_member(player, m_iMenu, Menu_OFF);
    menu_display(player, menu, 0, seconds);
}

stock gc_menu_action(const menu, const item)
{
    new action = item;
    if (item >= 0) {
        new access, info[12];
        menu_item_getinfo(menu, item, access, info, charsmax(info));
        action = str_to_num(info);
    }
    gc_menu_release(menu);
    return action;
}

stock bool:gc_menu_active(const player)
{
    if (gGcMenus[player] < 0) return false;
    new legacy, current;
    player_menu_info(player, legacy, current);
    return current == gGcMenus[player];
}
