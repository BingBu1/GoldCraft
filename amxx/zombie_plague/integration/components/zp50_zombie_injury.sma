// Implementation included by zp50_zombie_gore.sma, not a standalone plugin.
enum _:InjuryScope {
    InjuryVictim, InjuryLife, InjuryUser,
    Float:InjuryHealth, Float:InjuryArmor, Float:InjuryNested
};
new Array:gInjuryScopes;

InitZombieInjury()
{
    gInjuryScopes = ArrayCreate(InjuryScope);
    // Observe final damage after every ReAPI/Ham veto or modifier. Ham post
    // still runs when the inner ReAPI chain skips its own post callbacks.
    RegisterHam(Ham_TakeDamage, "player", "GoreDamagePre", false);
    RegisterHam(Ham_TakeDamage, "player", "GoreDamagePost", true);
    RegisterHookChain(RG_CBasePlayer_Killed, "GoreKilledPost", true);
}

public GoreDamagePre(victim, inflictor, attacker, Float:damage, bits)
{
    new scope[InjuryScope];
    if (is_user_alive(victim) && zp_core_is_zombie(victim)) {
        scope[InjuryVictim] = victim;
        scope[InjuryLife] = gLife[victim];
        scope[InjuryUser] = get_user_userid(victim);
        scope[InjuryHealth] = Float:get_entvar(victim, var_health);
        scope[InjuryArmor] = Float:get_entvar(victim, var_armorvalue);
    }
    ArrayPushArray(gInjuryScopes, scope);
    return HAM_IGNORED;
}

public GoreDamagePost(victim, inflictor, attacker, Float:damage, bits)
{
    new index = ArraySize(gInjuryScopes) - 1;
    if (index < 0) return HAM_IGNORED;
    new scope[InjuryScope]; ArrayGetArray(gInjuryScopes, index, scope);
    ArrayDeleteItem(gInjuryScopes, index);
    if (!scope[InjuryVictim] || !is_user_connected(victim)
        || get_user_userid(victim) != scope[InjuryUser] || gLife[victim] != scope[InjuryLife]
        || !zp_core_is_zombie(victim)) return HAM_IGNORED;
    new Float:lost = floatmax(0.0, scope[InjuryHealth] - floatmax(0.0, Float:get_entvar(victim, var_health)));
    new Float:ownLost = floatmax(0.0, lost - scope[InjuryNested]);
    // Account a nested hit once in the closest enclosing hit on this player.
    for (new n = index - 1; n >= 0; n--) {
        new parent[InjuryScope]; ArrayGetArray(gInjuryScopes, n, parent);
        if (parent[InjuryVictim] == victim && parent[InjuryLife] == scope[InjuryLife]) {
            parent[InjuryNested] += lost; ArraySetArray(gInjuryScopes, n, parent); break;
        }
    }
    new Float:armorLost = floatmax(0.0, scope[InjuryArmor] - Float:get_entvar(victim, var_armorvalue));
    if (ownLost > 0.0 || armorLost > 0.0) ZombieBlood(victim, ownLost + armorLost);
    new Float:step = get_pcvar_float(gGibDamage);
    if (step >= 1.0 && ownLost > 0.0) {
        gGibRemainder[victim] += ownLost;
        new count = floatround(gGibRemainder[victim] / step, floatround_floor);
        if (count > 0) {
            gGibRemainder[victim] -= float(count) * step;
            ZombieFlesh(victim, count);
        }
    } else if (step < 1.0) gGibRemainder[victim] = 0.0;
    if (!is_user_alive(victim)) gGibRemainder[victim] = 0.0;
    return HAM_IGNORED;
}

public GoreKilledPost(victim, attacker, gib)
{
    // The lethal TakeDamage post must first account the victim's remaining
    // health. Direct kills outside TakeDamage have no pending settlement.
    for (new n = ArraySize(gInjuryScopes) - 1; n >= 0; n--) {
        new scope[InjuryScope]; ArrayGetArray(gInjuryScopes, n, scope);
        if (scope[InjuryVictim] == victim && scope[InjuryLife] == gLife[victim]) return;
    }
    ResetZombieGore(victim);
}
