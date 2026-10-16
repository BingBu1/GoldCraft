#include <amxmodx>
#include <goldcraft>
#include <reapi>
#include <xs>
#include <zp50_class_zombie>
#define LIBRARY_NEMESIS "zp50_class_nemesis"
#include <zp50_class_nemesis>

new gMultiplier, gKnockback, gStagger;
new gDucking, gNemesis, gObeyClass;

public plugin_init()
{
    register_plugin("GoldCraft ZP Player Combat", "1.0", "GoldCraft");
    if (!gc_gameplay_api_version()) set_fail_state("GoldCraft gameplay API required");
    gMultiplier = create_cvar("mc_zombie_damage_multiplier", "5.0", FCVAR_SERVER,
        "Active MC player attacks against zombies only; 1 keeps base damage", true, 0.0, true, 100.0);
    gKnockback = create_cvar("mc_zombie_knockback", "160.0", FCVAR_SERVER,
        "Horizontal velocity per confirmed MC player hit; 0 disables", true, 0.0, true, 1000.0);
    gStagger = create_cvar("mc_zombie_stagger", "0.5", FCVAR_SERVER,
        "Native pain-shock velocity modifier; 1 disables extra stagger", true, 0.0, true, 1.0);
    RegisterHookChain(RG_CBasePlayer_TakeDamage, "PlayerDamage", false);
}

public plugin_cfg()
{
    gDucking = get_cvar_pointer("zp_knockback_ducking");
    gNemesis = get_cvar_pointer("zp_knockback_nemesis");
    gObeyClass = get_cvar_pointer("zp_knockback_obey_class");
}

public plugin_natives()
{
    set_module_filter("module_filter");
    set_native_filter("native_filter");
}
public module_filter(const module[])
{
    return equal(module, LIBRARY_NEMESIS) ? PLUGIN_HANDLED : PLUGIN_CONTINUE;
}
public native_filter(const name[], index, trap)
{
    return !trap && equal(name, "zp_class_nemesis_get") ? PLUGIN_HANDLED : PLUGIN_CONTINUE;
}

stock bool:enemy_hit(victim, attacker)
{
    return victim != attacker && is_user_alive(victim) && is_user_alive(attacker)
        && zp_core_is_zombie(victim) && !zp_core_is_zombie(attacker);
}

public PlayerDamage(victim, inflictor, attacker, Float:damage, damage_type)
{
    // This scope exists only around the authenticated active-hit message.
    // Ordinary CS weapons, mobs, potions, DOT, TNT and the world never enter it.
    if (damage > 0.0 && gc_player_attack(victim, attacker) && enemy_hit(victim, attacker))
        SetHookChainArg(4, ATYPE_FLOAT, damage * get_pcvar_float(gMultiplier));
    return HC_CONTINUE;
}

public goldcraft_player_attack_post(victim, attacker, kind, Float:health_loss, Float:armor_loss)
{
    // Fired after the complete native damage chain, only for a matching life
    // with actual health/armor loss. Blocked or lethal hits get no extra impulse.
    if (!enemy_hit(victim, attacker) || (health_loss <= 0.0 && armor_loss <= 0.0)) return;
    new Float:stagger = get_pcvar_float(gStagger);
    new Float:old = Float:get_member(victim, m_flVelocityModifier);
    if (stagger < old) set_member(victim, m_flVelocityModifier, stagger);

    new Float:power = get_pcvar_float(gKnockback);
    if (get_entvar(victim, var_flags) & FL_DUCKING && gDucking) power *= get_pcvar_float(gDucking);
    if (LibraryExists(LIBRARY_NEMESIS, LibType_Library) && zp_class_nemesis_get(victim))
        power *= gNemesis ? get_pcvar_float(gNemesis) : 0.25;
    else if (gObeyClass && get_pcvar_num(gObeyClass))
        power *= zp_class_zombie_get_kb(zp_class_zombie_get_current(victim));
    if (power <= 0.0) return;

    new Float:from[3], Float:to[3], Float:direction[3], Float:velocity[3];
    get_entvar(attacker, var_origin, from);
    get_entvar(victim, var_origin, to);
    xs_vec_sub(to, from, direction);
    direction[2] = 0.0;
    if (xs_vec_len(direction) < 0.001) return;
    xs_vec_normalize(direction, direction);
    xs_vec_mul_scalar(direction, power, direction);
    get_entvar(victim, var_velocity, velocity);
    xs_vec_add(velocity, direction, velocity);
    set_entvar(victim, var_velocity, velocity);
}
