#include <amxmodx>
#include <reapi>
#include <zp50_core>

new gExtraHeadshotImpulse;

public plugin_init()
{
    register_plugin("Zombie Plague Headshot Knockback", "1.0.0", "GoldCraft contributors");
    gExtraHeadshotImpulse = create_cvar("mc_zombie_headshot_extra_knockback", "0", FCVAR_NONE,
        "Allow native large-flinch impulse on zombie headshots in addition to ZP knockback", true, 0.0, true, 1.0);
    RegisterHookChain(RG_CBasePlayer_TakeDamageImpulse, "DamageImpulse", false);
}

public DamageImpulse(victim, attacker, Float:force, Float:velocityModifier)
{
    // ZP's TraceAttack knockback has already run. Do not stack the native
    // large-flinch shove on top of it for a surviving zombie's head hit.
    if (!get_pcvar_num(gExtraHeadshotImpulse) && force > 0.0
        && is_user_alive(victim) && is_user_connected(attacker)
        && zp_core_is_zombie(victim) && !zp_core_is_zombie(attacker)
        && get_member(victim, m_LastHitGroup) == HIT_HEAD)
        SetHookChainArg(3, ATYPE_FLOAT, 0.0);
    return HC_CONTINUE;
}
