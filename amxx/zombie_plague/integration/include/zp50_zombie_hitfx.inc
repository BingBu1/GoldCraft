// Ham wraps the complete virtual call, including ReAPI HC_BREAK/HC_BYPASS.
// ReAPI's own post stage can be skipped, so it cannot own this paired scope.
enum _:TraceScope { TraceVictim, TraceLife, TraceHandle };
new Array:gTraceScopes;
new MessageHook:gHelmetMessage;
new Float:gHitPoint[MAX_PLAYERS + 1][3], Float:gHitTime[MAX_PLAYERS + 1];

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
    // Native client-side gib physics: random body parts, bounce and a short
    // lifetime, with no persistent server edicts for 24 zombies under fire.
    new Float:point[3]; get_entvar(id, var_origin, point);
    point[2] += 12.0;
    while (count > 0) {
        new batch = min(count, 255);
        message_begin_f(MSG_PVS, SVC_TEMPENTITY, point);
        write_byte(TE_BREAKMODEL);
        write_coord_f(point[0]); write_coord_f(point[1]); write_coord_f(point[2]);
        write_coord(12); write_coord(12); write_coord(24);
        write_coord(random_num(-100, 100)); write_coord(random_num(-100, 100)); write_coord(random_num(150, 250));
        write_byte(100); write_short(gGibModel); write_byte(batch);
        write_byte(40); write_byte(0x04); // Four seconds; BREAK_FLESH.
        message_end();
        count -= batch;
    }
}
