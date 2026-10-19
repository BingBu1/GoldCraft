#include <amxmodx>
#include <fakemeta>
#include <hamsandwich>
#include <reapi>
#include <zp50_core>

new gBlood, gGibDamage, gGibChance;
new gLife[MAX_PLAYERS + 1];
new Float:gGibRemainder[MAX_PLAYERS + 1];

// Ham wraps the complete virtual call, including ReAPI HC_BREAK/HC_BYPASS.
// ReAPI's own post stage can be skipped, so it cannot own this paired scope.
enum _:TraceScope { TraceVictim, TraceLife, TraceHandle };
new Array:gTraceScopes;
new MessageHook:gHelmetMessage;
new Float:gHitPoint[MAX_PLAYERS + 1][3], Float:gHitTime[MAX_PLAYERS + 1];

enum _:InjuryScope {
    InjuryVictim, InjuryLife, InjuryUser,
    Float:InjuryHealth, Float:InjuryArmor, Float:InjuryNested
};
new Array:gInjuryScopes;

public plugin_init()
{
    register_plugin("Zombie Plague Gore", "1.0.2", "GoldCraft contributors");
    gBlood = create_cvar("zp_zombie_blood", "1", FCVAR_NONE,
        "Blood stream on confirmed zombie injury", true, 0.0, true, 1.0);
    gGibDamage = create_cvar("zp_zombie_gib_damage", "1000", FCVAR_NONE,
        "Actual health lost per gib chance roll; 0 disables", true, 0.0, true, 1000000.0);
    gGibChance = create_cvar("zp_zombie_gib_chance", "50", FCVAR_NONE,
        "Percent chance per damage threshold to spawn one gib", true, 0.0, true, 100.0);
    InitZombieHitEffects();
    InitZombieInjury();
    RegisterHookChain(RG_CBasePlayer_Spawn, "GoreSpawnPost", true);
}

ResetZombieGore(id)
{
    gLife[id]++;
    gGibRemainder[id] = 0.0;
    gHitTime[id] = -1.0;
}

public client_putinserver(id) { ResetZombieGore(id); }
public client_disconnected(id) { ResetZombieGore(id); }
public zp_fw_core_infect_post(id, attacker) { ResetZombieGore(id); }
public zp_fw_core_cure_post(id, attacker) { ResetZombieGore(id); }
public GoreSpawnPost(id) { ResetZombieGore(id); }

public plugin_end()
{
    ArrayDestroy(gTraceScopes);
    ArrayDestroy(gInjuryScopes);
}

InitZombieHitEffects()
{
    gTraceScopes = ArrayCreate(TraceScope);
    RegisterHam(Ham_TraceAttack, "player", "GoreTracePre", false);
    RegisterHam(Ham_TraceAttack, "player", "GoreTracePost", true);
    gHelmetMessage = RegisterMessage(SVC_TEMPENTITY, "GoreHelmetMessage");
    DisableHookMessage(gHelmetMessage);
}

public GoreTracePre(victim, attacker, Float:damage, Float:direction[3], trace, bits)
{
    new scope[TraceScope];
    scope[TraceVictim] = victim;
    scope[TraceLife] = gLife[victim];
    scope[TraceHandle] = trace;
    if (!ArraySize(gTraceScopes)) EnableHookMessage(gHelmetMessage);
    ArrayPushArray(gTraceScopes, scope);
    if (is_user_alive(victim) && zp_core_is_zombie(victim)) {
        get_tr2(trace, TR_vecEndPos, gHitPoint[victim]);
        gHitTime[victim] = get_gametime();
    }
    return HAM_IGNORED;
}

public GoreTracePost(victim, attacker, Float:damage, Float:direction[3], trace, bits)
{
    new count = ArraySize(gTraceScopes);
    if (count) ArrayDeleteItem(gTraceScopes, count - 1);
    if (count <= 1) DisableHookMessage(gHelmetMessage);
    return HAM_IGNORED;
}

public GoreHelmetMessage(message, destination, receiver)
{
    new count = ArraySize(gTraceScopes);
    if (!count || destination != MSG_PVS || GetMessageArgsNum() != 11) return HC_CONTINUE;
    // Match only ReGameDLL's helmet splash, leaving shields, other players
    // and unrelated streak effects alone. Keep the helmet/damage state intact.
    if (GetMessageData(MsgArg, 1) != TE_STREAK_SPLASH
        || GetMessageData(MsgArg, 8) != 5 || GetMessageData(MsgArg, 9) != 22
        || GetMessageData(MsgArg, 10) != 25 || GetMessageData(MsgArg, 11) != 65)
        return HC_CONTINUE;
    new scope[TraceScope]; ArrayGetArray(gTraceScopes, count - 1, scope);
    new victim = scope[TraceVictim], trace = scope[TraceHandle];
    if (!is_user_alive(victim) || gLife[victim] != scope[TraceLife]
        || !zp_core_is_zombie(victim) || get_tr2(trace, TR_iHitgroup) != HIT_HEAD
        || get_member(victim, m_iKevlar) != ARMOR_VESTHELM) return HC_CONTINUE;
    new Float:point[3]; get_tr2(trace, TR_vecEndPos, point);
    for (new axis = 0; axis < 3; axis++) {
        if (GetMessageArgType(axis + 2) != ArgCoord
            || floatabs(Float:GetMessageData(MsgArg, axis + 2) - point[axis]) > 0.125)
            return HC_CONTINUE;
    }
    SetMessageData(MsgArg, 9, 0); // No spark particles; other message hooks still run.
    return HC_CONTINUE;
}

ZombieBlood(id, Float:amount)
{
    if (!get_pcvar_num(gBlood)) return;
    new Float:point[3];
    if (gHitTime[id] == get_gametime()) {
        for (new axis = 0; axis < 3; axis++) point[axis] = gHitPoint[id][axis];
    } else {
        get_entvar(id, var_origin, point);
        point[2] += 12.0;
    }
    message_begin_f(MSG_PVS, SVC_TEMPENTITY, point);
    write_byte(TE_BLOODSTREAM);
    write_coord_f(point[0]); write_coord_f(point[1]); write_coord_f(point[2]);
    write_coord(random_num(-30, 30)); write_coord(random_num(-30, 30)); write_coord(random_num(30, 70));
    write_byte(247); write_byte(clamp(floatround(amount * 0.25) + 30, 30, 120));
    message_end();
}

ZombieFlesh(id, count)
{
    new chance = get_pcvar_num(gGibChance);
    if (!chance) return;
    // ReGameDLL precaches the language-specific gib models and owns their
    // bounce/fade lifecycle. Reserve entity slots for gameplay under load.
    new available = global_get(glb_maxEntities) - engfunc(EngFunc_NumberOfEntities) - 64;
    count = min(count, max(0, available));
    while (count-- > 0) {
        if (random_num(1, 100) > chance) continue;
        // Random human gibs exclude body 0 (the skull); select it separately.
        if (random_num(0, 5) == 0) rg_spawn_head_gib(id);
        else rg_spawn_random_gibs(id, 1, true);
    }
}


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
