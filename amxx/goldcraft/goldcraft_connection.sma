#include <amxmodx>
#include <amxmisc>
#include <goldcraft>

new gUserid[33], Float:gDeadline[33];

public plugin_init()
{
    register_plugin("GoldCraft MC Connection", "1.0", "GoldCraft");
    if (!gc_gameplay_api_version()) set_fail_state("GoldCraft gameplay API required");
    set_task_ex(1.0, "CheckConnections", _, _, _, SetTask_Repeat);
}

public client_disconnected(id) { gUserid[id] = 0; gDeadline[id] = 0.0; }

public CheckConnections()
{
    new Float:now = get_gametime();
    for (new id = 1; id <= MaxClients; id++)
    {
        if (!is_user_connected(id) || is_user_bot(id) || is_user_hltv(id)) continue;
        new userid = get_user_userid(id), flags = gc_player_flags(id);
        if (gUserid[id] != userid) { gUserid[id] = userid; gDeadline[id] = 0.0; }
        // Legacy/unpaired players are outside this policy. Control readiness,
        // menu state and CS frame delivery never determine MC connectivity.
        if (flags < 0 || (flags & GC_PLAYER_CONNECTED))
        {
            if (gDeadline[id] != 0.0)
            {
                set_dhudmessage(40, 255, 80, -1.0, 0.22, 0, 0.0, 3.0, 0.0, 0.3);
                show_dhudmessage(id, "Minecraft 连接已恢复");
            }
            gDeadline[id] = 0.0;
            continue;
        }
        if (gDeadline[id] == 0.0) gDeadline[id] = now + 90.0;
        new remaining = max(0, floatround(gDeadline[id] - now, floatround_ceil));
        set_dhudmessage(255, 40, 30, -1.0, 0.22, 1, 0.0, 1.2, 0.0, 0.0);
        show_dhudmessage(id, "警告：Minecraft 连接已断开！^n请在 %d 秒内重新连接，否则将断开 CS", remaining);
        if (remaining == 0 && get_user_userid(id) == userid)
        {
            // Numeric userid prevents a delayed command from kicking a reused slot.
            server_cmd("kick #%d ^"Minecraft connection lost for 90 seconds^"", userid);
            gDeadline[id] = now + 90.0;
        }
    }
}
