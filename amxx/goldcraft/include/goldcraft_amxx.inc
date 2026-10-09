// Shared AMXX 1.9 / ReAPI operations whose side effects need more than a member write.
// Matched sources: AMXX b88b763a and ReAPI 1c448d06.
#if defined _goldcraft_amxx_included
    #endinput
#endif
#define _goldcraft_amxx_included

#include <amxmodx>
#include <amxmisc>
#include <fakemeta>
#include <reapi>

#if AMXX_VERSION_NUM < 190
    #error GoldCraft requires AMX Mod X 1.9 or newer.
#endif

stock gc_relink_origin(const entity, const Float:origin[3])
{
    // Preserve both engine relinks; writing var_origin alone leaves stale bounds.
    new Float:mins[3], Float:maxs[3];
    get_entvar(entity, var_mins, mins);
    get_entvar(entity, var_maxs, maxs);
    engfunc(EngFunc_SetSize, entity, mins, maxs);
    engfunc(EngFunc_SetOrigin, entity, origin);
}

stock gc_find_in_sphere(const previous, const Float:origin[3], const Float:radius)
{
    // Fakemeta uses -1 for the end of the search; callers use 0 as the sentinel.
    return max(0, engfunc(EngFunc_FindEntityInSphere, previous, origin, radius));
}

stock gc_set_health(const player, const health)
{
    if (health > 0)
        set_entvar(player, var_health, float(health));
    else
        // ClientKill applies native death/score rules; a zero entvar does not.
        dllfunc(DLLFunc_ClientKill, player);
}

stock gc_set_rendering(const entity, const effect = kRenderFxNone,
    const red = 0, const green = 0, const blue = 0,
    const mode = kRenderNormal, const amount = 0)
{
    // Disconnect can also arrive before the player entity has initialized.
    if (!is_entity(entity)) return;
    new Float:color[3];
    color[0] = float(red);
    color[1] = float(green);
    color[2] = float(blue);
    set_entvar(entity, var_renderfx, effect);
    set_entvar(entity, var_rendercolor, color);
    set_entvar(entity, var_rendermode, mode);
    set_entvar(entity, var_renderamt, float(amount));
}

stock bool:gc_in_buyzone(const player)
{
    new signals[UnifiedSignals];
    get_member(player, m_signals, signals);
    return (signals[US_State] & _:SIGNAL_BUY) != 0;
}

stock gc_get_bpammo(const player, const WeaponIdType:weapon)
{
    // The player can retain shared ammunition after dropping the last weapon.
    // rg_get_user_bpammo requires an owned weapon; use the named ammo member.
    new ammo = rg_get_weapon_info(weapon, WI_AMMO_TYPE);
    return ammo > 0 ? get_member(player, m_rgAmmo, ammo) : 0;
}

stock gc_set_bpammo(const player, const WeaponIdType:weapon, const amount)
{
    new ammo = rg_get_weapon_info(weapon, WI_AMMO_TYPE);
    if (ammo > 0) set_member(player, m_rgAmmo, amount, ammo);
}

stock gc_active_weapon(const player)
{
    return is_entity(player) ? get_member(player, m_pActiveItem) : -1;
}

stock gc_weapon_owner(const weapon)
{
    return is_entity(weapon) ? get_member(weapon, m_pPlayer) : -1;
}

stock gc_set_account(const player, const amount, const bool:flash = true)
{
    // ZP's compatibility API accepts the full account value. AddAccount would
    // clamp it to mp_maxmoney and change that contract.
    set_member(player, m_iAccount, amount);
    static message;
    if (!message) message = get_user_msgid("Money");
    emessage_begin(MSG_ONE, message, _, player);
    ewrite_long(amount);
    ewrite_byte(_:flash);
    emessage_end();
}

stock gc_set_nightvision(const player, const bool:enabled = true)
{
    if (enabled && get_member(player, m_bHasNightVision))
        client_print(player, print_center, "#Already_Have_One");
    else
        set_member(player, m_bHasNightVision, enabled);
}
