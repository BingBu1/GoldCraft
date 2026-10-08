// Opt-in independent fixture only. Must precede Nade Modes in plugins.ini.
// No fake assets: reserve holes, then precache the actual grenade media.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <amxmodx>

public plugin_precache()
{
    if (get_cvar_num("gc_precache_protocol") != 3) {
        set_fail_state("GoldCraft negotiated media v3 engine required");
        return;
    }
    server_cmd("gc_precache_fixture_pad");
    server_exec();
}

public plugin_init()
{
    register_plugin("GoldCraft Nade Modes media fixture", "0.1", "GoldCraft contributors");
}
