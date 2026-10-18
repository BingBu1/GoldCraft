// Isolated real weapon/authority regression. Never enable on a public server.
#include <amxmodx>
#include <reapi>
#include <fakemeta>
#include <hamsandwich>

new gEnabled, gAttacker, gVictim, gKnife, gCancel, gTraces;
new Float:gTraceDamage;
public plugin_init()
{
    register_plugin("GoldCraft melee test", "1.0", "GoldCraft contributors");
    gEnabled = create_cvar("mc_melee_test", "0");
    register_srvcmd("gc_melee", "Command");
    RegisterHookChain(RG_CBasePlayer_TraceAttack, "TraceBefore");
    RegisterHookChain(RG_CBasePlayer_TakeDamage, "DamageBefore");
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
    server_print("[GC Melee] attacker=%d victim=%d traces=%d trace=%.3f health=%.3f armor=%.3f",
        gAttacker, gVictim, gTraces, gTraceDamage, Float:get_entvar(gVictim, var_health), Float:get_entvar(gVictim, var_armorvalue));
    return PLUGIN_HANDLED;
}
