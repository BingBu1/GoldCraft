// Isolated real weapon/authority regression. Never enable on a public server.
#include <amxmodx>
#include <reapi>
#include <fakemeta>
#include <hamsandwich>

new gEnabled, gAttacker, gVictim, gKnife, gCancel, gTraces, bool:gMotion;
new Float:gTraceDamage, Float:gMotionTime;
public plugin_init()
{
    register_plugin("GoldCraft melee test", "1.0", "GoldCraft contributors");
    gEnabled = create_cvar("mc_melee_test", "0");
    register_srvcmd("gc_melee", "Command");
    RegisterHookChain(RG_CBasePlayer_TraceAttack, "TraceBefore");
    RegisterHookChain(RG_CBasePlayer_TakeDamage, "DamageBefore");
    register_forward(FM_StartFrame, "MotionFrame");
}
public MotionFrame()
{
    if (!gMotion || !get_pcvar_num(gEnabled) || !is_user_alive(gVictim)) return FMRES_IGNORED;
    new Float:now = get_gametime();
    new msec = floatround((now - gMotionTime) * 1000.0, floatround_floor);
    if (msec < 1) return FMRES_IGNORED;
    msec = min(msec, 100); gMotionTime = now;
    // Fake clients need real engine movement commands even when no key is held.
    // Matched AMXX 1.10 EngFunc_RunPlayerMove: angles, fwd/side/up, buttons, impulse, msec.
    engfunc(EngFunc_RunPlayerMove, gVictim, Float:{0.0, 0.0, 0.0}, 0.0, 0.0, 0.0, 0, 0, msec);
    return FMRES_IGNORED;
}
public TraceBefore(id, attacker, Float:damage, Float:direction[3], trace, bits)
{
    if (id == gVictim && attacker == gAttacker) { gTraceDamage = damage; gTraces++; }
    return HC_CONTINUE;
}
public DamageBefore(id, inflictor, attacker, Float:damage, bits)
{
    if (id == gVictim && attacker == gAttacker && gCancel) {
        SetHookChainReturn(ATYPE_INTEGER, 0);
        return HC_SUPERCEDE;
    }
    return HC_CONTINUE;
}
public Command()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    new action[16], first[16], second[16];
    read_argv(1, action, charsmax(action)); read_argv(2, first, charsmax(first)); read_argv(3, second, charsmax(second));
    if (equal(action, "create") && !gAttacker) {
        for (new n; n < 2; n++) {
            new name[32], reject[128]; formatex(name, charsmax(name), "GC_Melee_%d", n);
            new id = engfunc(EngFunc_CreateFakeClient, name);
            if (!id) return PLUGIN_HANDLED;
            dllfunc(DLLFunc_ClientConnect, id, name, "127.0.0.1", reject); dllfunc(DLLFunc_ClientPutInServer, id);
            set_entvar(id, var_flags, get_entvar(id, var_flags) | FL_FAKECLIENT);
            set_member(id, m_iJoiningState, JOINED); set_member(id, m_bJustConnected, false); set_member(id, m_iMenu, Menu_OFF);
            rg_set_user_team(id, n ? TEAM_TERRORIST : TEAM_CT); rg_round_respawn(id);
            set_entvar(id, var_flags, get_entvar(id, var_flags) | FL_FROZEN);
            if (n) gVictim = id; else gAttacker = id;
        }
    }
    if (!gAttacker || !gVictim) return PLUGIN_HANDLED;
    if (equal(action, "waterstep")) {
        gMotion = false;
        set_entvar(gVictim, var_flags, get_entvar(gVictim, var_flags) & ~(FL_FROZEN | FL_ONGROUND));
        set_entvar(gVictim, var_movetype, MOVETYPE_WALK);
        engfunc(EngFunc_SetOrigin, gVictim, Float:{524.0, 176.0, 100.0});
        set_entvar(gVictim, var_velocity, Float:{100.0, 0.0, 100.0});
        engfunc(EngFunc_RunPlayerMove, gVictim, Float:{0.0, 0.0, 0.0}, 250.0, 0.0, 0.0, 0, 0, 50);
    }
    if (equal(action, "motion")) {
        if (!is_user_alive(gVictim)) rg_round_respawn(gVictim);
        set_entvar(gVictim, var_health, 1000.0);
        set_entvar(gVictim, var_flags, get_entvar(gVictim, var_flags) & ~FL_FROZEN);
        set_entvar(gVictim, var_movetype, MOVETYPE_WALK);
        engfunc(EngFunc_SetOrigin, gVictim, Float:{524.0, 176.0, 36.03125});
        set_entvar(gVictim, var_velocity, Float:{0.0, 0.0, 0.0});
        gMotion = true; gMotionTime = get_gametime();
    }
    if (equal(action, "hurt")) {
        new kind = str_to_num(first), armor = str_to_num(second);
        if (!is_user_alive(gVictim)) rg_round_respawn(gVictim);
        set_entvar(gVictim, var_health, kind == 8 ? 10.0 : 1000.0);
        set_entvar(gVictim, var_takedamage, kind == 9 ? DAMAGE_NO : DAMAGE_AIM);
        rg_set_user_armor(gVictim, armor, armor ? ARMOR_VESTHELM : ARMOR_NONE);
        // Exact Ham_TakeDamage signature from the matched 1.10 ham_const.inc.
        new bits = DMG_GENERIC, attacker = 0;
        switch (kind) {
            case 1, 8, 9: { bits = DMG_CLUB; attacker = gAttacker; }
            case 2: { bits = DMG_BULLET; attacker = gAttacker; }
            case 3: { bits = DMG_BLAST; attacker = gAttacker; }
            case 4: bits = DMG_BURN;
            case 5: bits = DMG_FALL;
            case 6: bits = DMG_DROWN;
            case 7: bits = DMG_POISON;
        }
        if (kind == 2) {
            new gun = rg_find_weapon_bpack_by_name(gAttacker, "weapon_ak47");
            if (gun <= 0) gun = rg_give_item(gAttacker, "weapon_ak47");
            set_member(gAttacker, m_pActiveItem, gun);
        }
        ExecuteHamB(Ham_TakeDamage, gVictim, attacker, attacker, 25.0, bits);
    }
    if (equal(action, "attack")) {
        new kind = str_to_num(first), armor = str_to_num(second);
        gCancel = kind == 3; gTraceDamage = 0.0; gTraces = 0;
        set_entvar(gVictim, var_health, 1000.0); set_entvar(gVictim, var_takedamage, DAMAGE_AIM);
        rg_set_user_armor(gVictim, armor, armor ? ARMOR_VESTHELM : ARMOR_NONE);
        engfunc(EngFunc_SetOrigin, gAttacker, Float:{496.0, 176.0, 36.03125});
        engfunc(EngFunc_SetOrigin, gVictim, Float:{524.0, 176.0, 36.03125});
        set_entvar(gAttacker, var_velocity, Float:{0.0, 0.0, 0.0}); set_entvar(gVictim, var_velocity, Float:{0.0, 0.0, 0.0});
        set_entvar(gAttacker, var_v_angle, Float:{0.0, 0.0, 0.0}); set_entvar(gAttacker, var_view_ofs, Float:{0.0, 0.0, 0.0});
        new Float:angles[3]; angles[1] = kind == 2 ? 0.0 : 180.0;
        set_entvar(gVictim, var_angles, angles);
        // Spawn already grants a knife; duplicate GiveNamedItemEx can return -1.
        gKnife = rg_find_weapon_bpack_by_name(gAttacker, "weapon_knife");
        if (gKnife <= 0) gKnife = rg_give_item(gAttacker, "weapon_knife");
        if (gKnife <= 0) {
            server_print("[GC Melee] error=1");
            return PLUGIN_HANDLED;
        }
        set_member(gAttacker, m_pActiveItem, gKnife); set_member(gAttacker, m_flNextAttack, 0.0);
        set_member(gKnife, m_Weapon_flNextPrimaryAttack, 0.0); set_member(gKnife, m_Weapon_flNextSecondaryAttack, 0.0);
        if (kind == 1 || kind == 2) ExecuteHamB(Ham_Weapon_SecondaryAttack, gKnife);
        else ExecuteHamB(Ham_Weapon_PrimaryAttack, gKnife);
        gCancel = 0;
    }
    new Float:at[3], Float:velocity[3];get_entvar(gVictim, var_origin, at);get_entvar(gVictim, var_velocity, velocity);
    server_print("[GC Melee] attacker=%d victim=%d traces=%d trace=%.3f health=%.3f armor=%.3f x=%.5f y=%.5f z=%.5f vx=%.5f vy=%.5f vz=%.5f water=%d kind=%d",
        gAttacker, gVictim, gTraces, gTraceDamage, Float:get_entvar(gVictim, var_health), Float:get_entvar(gVictim, var_armorvalue),
        at[0], at[1], at[2], velocity[0], velocity[1], velocity[2], get_entvar(gVictim, var_waterlevel), get_entvar(gVictim, var_watertype));
    return PLUGIN_HANDLED;
}
