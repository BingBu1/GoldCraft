#include <amxmodx>
#include <goldcraft>
#include <zp50_core>

public plugin_init()
{
    register_plugin("GoldCraft ZP Form Rules", "1.0", "GoldCraft");
    if (!gc_gameplay_api_version()) set_fail_state("GoldCraft gameplay API required");
}

public goldcraft_form_request(id, requested)
{
    return requested == GC_FORM_MINECRAFT && is_user_connected(id) && zp_core_is_zombie(id)
        ? PLUGIN_HANDLED : PLUGIN_CONTINUE;
}

public zp_fw_core_infect_pre(id, attacker)
{
    return gc_mc_infection_protected(id) ? PLUGIN_HANDLED : PLUGIN_CONTINUE;
}

public zp_fw_core_infect_post(id, attacker)
{
    // Runs for forced/direct infection as well as ordinary zombie attacks.
    if (gc_get_form(id) == GC_FORM_MINECRAFT) gc_set_form(id, GC_FORM_CS);
}
