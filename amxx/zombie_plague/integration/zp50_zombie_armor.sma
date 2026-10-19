#include <amxmodx>
#include <reapi>
#include <zp50_core>

new gArmor;
new bool:gGranted[MAX_PLAYERS + 1];

public plugin_init()
{
    register_plugin("Zombie Plague Armor", "1.0.0", "GoldCraft contributors");
    gArmor = create_cvar("mc_zombie_armor", "2000", FCVAR_NONE,
        "Vest and helmet armor granted on each successful zombie infection", true, 0.0, true, 32767.0);
}

public client_putinserver(id)
{
    gGranted[id] = false;
}

public client_disconnected(id)
{
    gGranted[id] = false;
}

public zp_fw_core_infect_post(id, attacker)
{
    if (!is_user_alive(id) || !zp_core_is_zombie(id)) return;

    // ZP strips old armor in infect, before it sets the zombie flag and emits
    // infect_post. Apply once here, including forced infections and respawns.
    new amount = get_pcvar_num(gArmor);
    rg_set_user_armor(id, amount, amount > 0 ? ARMOR_VESTHELM : ARMOR_NONE);
    gGranted[id] = amount > 0;
}

public zp_fw_core_cure(id, attacker)
{
    // Cure has already been accepted. Remove the zombie grant before the human
    // armor plugin applies its own cure_post defaults, regardless of load order.
    if (!gGranted[id]) return;
    gGranted[id] = false;
    if (is_user_alive(id)) rg_set_user_armor(id, 0, ARMOR_NONE);
}
