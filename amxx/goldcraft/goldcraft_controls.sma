#include <amxmodx>

new gAutoBind;

public plugin_init()
{
    register_plugin("GoldCraft controls", "1.0.0", "GoldCraft contributors");
    gAutoBind = create_cvar("mc_bind_f6", "1", FCVAR_NONE,
        "Bind F6 to the GoldCraft form menu when a human joins", true, 0.0, true, 1.0);
}

public client_putinserver(id)
{
    remove_task(id);
    if (is_user_bot(id) || is_user_hltv(id)) return;

    // AMXX 1.9 set_task passes the array first, followed by the task id.
    // Keep the userid so a delayed join cannot bind a replacement connection.
    new connection[1];
    connection[0] = get_user_userid(id);
    set_task(1.0, "BindFormMenu", id, connection, sizeof connection);
}

public BindFormMenu(const connection[], id)
{
    if (!get_pcvar_num(gAutoBind) || !is_user_connected(id)
        || is_user_bot(id) || is_user_hltv(id)
        || !connection[0] || get_user_userid(id) != connection[0]) return;

    // F6 is the server's designated form-menu shortcut. No other bind changes.
    // A client may filter this command; never disable its command filter.
    client_cmd(id, "bind ^"F6^" ^"goldcraft_menu^"");
    client_print(id, print_chat, "[GoldCraft] F6: form menu. If blocked, use /forms or bind F6 goldcraft_menu in console.");
}

public client_disconnected(id)
{
    remove_task(id);
}
