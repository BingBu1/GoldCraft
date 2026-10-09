// Opt-in, bounded integer HUD fixture. Never enabled during ordinary play.
#include <amxmodx>
#include <goldcraft_amxx>
#include <fakemeta>
#include <hamsandwich>
#include <reapi>

new gEnabled, gType[33][4], gValue[33][4], gCount[33][4], gTarget[33];
new gSelected, gUserid, gOriginalArmorType;
new Float:gHealth, Float:gArmor, Float:gMaxHealth, Float:gDamage;
new gCapability[8];

public plugin_init()
{
    register_plugin("GoldCraft integer vitals fixture", "1.0", "GoldCraft contributors");
    gEnabled = create_cvar("goldcraft_vitals_test", "0");
    register_srvcmd("gc_vitals_create", "Create");
    register_srvcmd("gc_vitals_select", "Select");
    register_srvcmd("gc_vitals_set", "Set");
    register_srvcmd("gc_vitals_damage", "Damage");
    register_srvcmd("gc_vitals_heal", "Heal");
    register_srvcmd("gc_vitals_respawn", "Respawn");
    register_srvcmd("gc_vitals_observe", "Observe");
    register_srvcmd("gc_vitals_status", "Status");
    register_srvcmd("gc_vitals_clear", "Clear");
}
public plugin_cfg()
{
    // Observe the final payload after ordinary mod message hooks.
    register_message(get_user_msgid("Health"), "HealthMessage");
    register_message(get_user_msgid("Battery"), "ArmorMessage");
    register_message(get_user_msgid("SpecHealth"), "SpecMessage");
    register_message(get_user_msgid("SpecHealth2"), "Spec2Message");
}
stock Record(id, kind)
{
    if (!get_pcvar_num(gEnabled) || id < 1 || id > 32) return;
    gType[id][kind] = get_msg_argtype(1);
    gValue[id][kind] = get_msg_arg_int(1);
    gCount[id][kind]++;
    if (kind == 3) gTarget[id] = get_msg_arg_int(2);
}
public HealthMessage(msg, dest, id) { Record(id, 0); }
public ArmorMessage(msg, dest, id) { Record(id, 1); }
public SpecMessage(msg, dest, id) { Record(id, 2); }
public Spec2Message(msg, dest, id) { Record(id, 3); }
stock Player()
{
    return get_pcvar_num(gEnabled) && is_user_connected(gSelected) && get_user_userid(gSelected) == gUserid ? gSelected : 0;
}
stock Update(id)
{
    set_member(id, m_iClientHealth, -1); set_member(id, m_iClientBattery, -1);
    ExecuteHamB(Ham_Player_UpdateClientData, id);
}
public Create()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    for (new i; i < 2; i++) {
        new name[24], reject[128]; formatex(name, charsmax(name), "GC_Vitals_%d", i);
        new id = engfunc(EngFunc_CreateFakeClient, name);
        if (!id) return PLUGIN_HANDLED;
        dllfunc(DLLFunc_ClientConnect, id, name, "127.0.0.1", reject);
        dllfunc(DLLFunc_ClientPutInServer, id);
        set_entvar(id, var_flags, get_entvar(id, var_flags) | FL_FAKECLIENT);
        set_member(id, m_iJoiningState, JOINED); set_member(id, m_bJustConnected, false);
        set_member(id, m_iMenu, Menu_OFF);
        rg_set_user_team(id, i ? TEAM_TERRORIST : TEAM_CT, i ? MODEL_T_TERROR : MODEL_CT_URBAN);
        rg_round_respawn(id);
        server_print("GC_VITALS_CREATED slot=%d userid=%d", id, get_user_userid(id));
    }
    return PLUGIN_HANDLED;
}
public Select()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    new arg[24]; read_argv(1, arg, charsmax(arg));
    new id = arg[0] == '#' ? find_player_ex(FindPlayer_MatchUserId, str_to_num(arg[1])) : 0;
    if (!is_user_alive(id)) return PLUGIN_HANDLED;
    Clear(); gSelected = id; gUserid = get_user_userid(id);
    gHealth = get_entvar(id, var_health); gArmor = get_entvar(id, var_armorvalue);
    gMaxHealth = get_entvar(id, var_max_health); gDamage = get_entvar(id, var_takedamage);
    gOriginalArmorType = get_member(id, m_iKevlar);
    get_user_info(id, "_gcvitals", gCapability, charsmax(gCapability));
    set_entvar(id, var_takedamage, DAMAGE_NO);
    set_entvar(id, var_max_health, 2147483648.0);
    set_task_ex(120.0, "Clear", 19071);
    return Status();
}
public Set()
{
    new id = Player(), arg[32]; if (!id || read_argc() != 4) return PLUGIN_HANDLED;
    read_argv(1, arg, charsmax(arg)); new Float:health = str_to_float(arg);
    read_argv(2, arg, charsmax(arg)); new armor = str_to_num(arg);
    read_argv(3, arg, charsmax(arg)); new wide = str_to_num(arg);
    if (health < 0.0 || health > 2147483648.0 || armor < 0 || wide < 0 || wide > 1) return PLUGIN_HANDLED;
    set_user_info(id, "_gcvitals", wide ? "1" : "0");
    set_entvar(id, var_health, health); rg_set_user_armor(id, armor, armor ? ARMOR_VESTHELM : ARMOR_NONE);
    Update(id); return Status();
}
public Damage()
{
    new id = Player(), arg[32]; if (!id || !is_user_alive(id)) return PLUGIN_HANDLED;
    read_argv(1, arg, charsmax(arg)); new Float:amount = str_to_float(arg);
    if (amount <= 0.0 || amount > 1000000.0) return PLUGIN_HANDLED;
    set_entvar(id, var_takedamage, DAMAGE_AIM);
    ExecuteHamB(Ham_TakeDamage, id, 0, 0, amount, DMG_DROWN);
    set_entvar(id, var_takedamage, DAMAGE_NO);
    Update(id); return Status();
}
public Heal()
{
    new id = Player(), arg[32]; if (!id || !is_user_alive(id)) return PLUGIN_HANDLED;
    read_argv(1, arg, charsmax(arg)); new Float:amount = str_to_float(arg);
    if (amount <= 0.0 || amount > 1000000.0) return PLUGIN_HANDLED;
    set_entvar(id, var_takedamage, DAMAGE_AIM);
    ExecuteHamB(Ham_TakeHealth, id, amount, DMG_GENERIC);
    set_entvar(id, var_takedamage, DAMAGE_NO);
    Update(id); return Status();
}
public Respawn()
{
    new id = Player(); if (!id) return PLUGIN_HANDLED;
    rg_round_respawn(id); set_entvar(id, var_takedamage, DAMAGE_NO);
    Update(id); return Status();
}
public Observe()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    new arg[32]; read_argv(1, arg, charsmax(arg)); new id = str_to_num(arg);
    read_argv(2, arg, charsmax(arg)); new target = str_to_num(arg);
    read_argv(3, arg, charsmax(arg)); new wide = str_to_num(arg);
    if (!is_user_bot(id) || !is_user_alive(target) || id == target) return PLUGIN_HANDLED;
    set_user_info(id, "_gcvitals", wide ? "1" : "0");
    rg_set_user_team(id, TEAM_SPECTATOR);
    set_entvar(id, var_iuser1, OBS_IN_EYE); set_entvar(id, var_deadflag, DEAD_DEAD);
    set_member(id, m_flNextFollowTime, 0.0);
    get_user_name(target, arg, charsmax(arg)); rg_internal_cmd(id, "follow", arg);
    Print(id); return PLUGIN_HANDLED;
}
stock Print(id)
{
    server_print("GC_VITALS {^"slot^":%d,^"userid^":%d,^"alive^":%d,^"health^":%.1f,^"armor^":%.1f,^"types^":[%d,%d,%d,%d],^"values^":[%d,%d,%d,%d],^"counts^":[%d,%d,%d,%d],^"target^":%d}",
        id, get_user_userid(id), is_user_alive(id), Float:get_entvar(id, var_health), Float:get_entvar(id, var_armorvalue),
        gType[id][0], gType[id][1], gType[id][2], gType[id][3], gValue[id][0], gValue[id][1], gValue[id][2], gValue[id][3],
        gCount[id][0], gCount[id][1], gCount[id][2], gCount[id][3], gTarget[id]);
}
public Status()
{
    if (!get_pcvar_num(gEnabled)) return PLUGIN_HANDLED;
    for (new id = 1; id <= MaxClients; id++) if (is_user_connected(id)) Print(id);
    return PLUGIN_HANDLED;
}
public Clear()
{
    remove_task(19071);
    new id = gSelected;
    if (is_user_connected(id) && get_user_userid(id) == gUserid) {
        set_user_info(id, "_gcvitals", gCapability);
        set_entvar(id, var_max_health, gMaxHealth); set_entvar(id, var_takedamage, gDamage);
        if (is_user_alive(id)) {
            set_entvar(id, var_health, gHealth); set_entvar(id, var_armorvalue, gArmor);
            set_member(id, m_iKevlar, gOriginalArmorType); Update(id);
        }
    }
    gSelected = gUserid = 0;
    return PLUGIN_HANDLED;
}
public client_disconnected(id) { if (id == gSelected) { gSelected = gUserid = 0; remove_task(19071); } }
