// Development fixtures only. Enabled exclusively by Initialize-AMXX -TestFixtures.
#include <amxmodx>
#include <goldcraft_amxx>
#include <reapi>
#include <goldcraft>

new gPortal, gDestination;
new gDamageCalls[33],gDeathCalls[33],gLastAttacker[33],gBlockDamage[33];
public plugin_init()
{
    register_plugin("GoldCraft test fixtures", "0.1.0", "GoldCraft contributors");
    register_srvcmd("gc_test_portal", "CreatePortal");
    register_srvcmd("gc_test_cleanup", "Cleanup");
    register_srvcmd("gc_test_position", "Position");
    register_srvcmd("gc_test_equip", "Equip");
    register_srvcmd("gc_test_vitals", "Vitals");
    register_srvcmd("gc_test_damage", "Damage");
    register_srvcmd("gc_test_fall", "FallDamage");
    register_srvcmd("gc_test_respawn", "Respawn");
    register_srvcmd("gc_test_damage_filter", "DamageFilter");
    register_srvcmd("gc_test_combat_status", "CombatStatus");
    RegisterHookChain(RG_CBasePlayer_TakeDamage, "BeforeDamage", false);
    RegisterHookChain(RG_CBasePlayer_Killed, "AfterKilled", true);
}
stock FixturePlayer()
{
    new who[20];read_argv(1,who,charsmax(who));
    if(who[0]!='#')return 0;
    return find_player_ex(FindPlayer_MatchUserId, str_to_num(who[1]));
}
public Vitals()
{
    new id=FixturePlayer();if(read_argc()!=4||!is_user_alive(id))return PLUGIN_HANDLED;
    new arg[32];read_argv(2,arg,charsmax(arg));new Float:health=str_to_float(arg);
    read_argv(3,arg,charsmax(arg));new armor=str_to_num(arg);
    if(health<1.0||health>100.0||armor<0||armor>100)return PLUGIN_HANDLED;
    // Fixture baseline only. Assertions exercise real damage/healing afterwards.
    set_entvar(id,var_health,health);rg_set_user_armor(id,armor,armor?ARMOR_VESTHELM:ARMOR_NONE);
    server_print("[GoldCraft fixture] vitals slot=%d health=%.3f armor=%d",id,health,armor);
    return PLUGIN_HANDLED;
}
public Damage()
{
    // Unarmored host damage independent of which runtime owns falling.
    return FixtureDamage(DMG_DROWN);
}
public FallDamage()
{
    return FixtureDamage(DMG_FALL);
}
stock FixtureDamage(bits)
{
    new id=FixturePlayer();if(read_argc()!=3||!is_user_alive(id))return PLUGIN_HANDLED;
    new arg[32];read_argv(2,arg,charsmax(arg));new Float:amount=str_to_float(arg);
    if(amount<=0.0||amount>500.0)return PLUGIN_HANDLED;
    // Exact ReAPI 5.29 natives_misc.cpp: accumulator -> CBasePlayer::TakeDamage.
    set_member(id,m_LastHitGroup,HIT_GENERIC);
    rg_multidmg_clear();rg_multidmg_add(0,id,amount,bits);rg_multidmg_apply(0,0);
    server_print("[GoldCraft fixture] damage slot=%d health=%.3f",id,Float:get_entvar(id,var_health));
    return PLUGIN_HANDLED;
}
public Respawn()
{
    new id=FixturePlayer();if(read_argc()!=2||!is_user_connected(id))return PLUGIN_HANDLED;
    rg_round_respawn(id);server_print("[GoldCraft fixture] respawn slot=%d",id);return PLUGIN_HANDLED;
}
public DamageFilter()
{
    new id=FixturePlayer();if(read_argc()!=3||!is_user_connected(id))return PLUGIN_HANDLED;
    new arg[8];read_argv(2,arg,charsmax(arg));gBlockDamage[id]=str_to_num(arg)==1;
    server_print("[GoldCraft fixture] filter slot=%d blocked=%d",id,gBlockDamage[id]);
    return PLUGIN_HANDLED;
}
public BeforeDamage(id,inflictor,attacker,Float:amount,bits)
{
    gDamageCalls[id]++;gLastAttacker[id]=attacker;
    // ReAPI hook_callback.cpp uses callForward<int>; GoldSrc BOOL is an int.
    if(gBlockDamage[id]){SetHookChainReturn(ATYPE_INTEGER,0);return HC_SUPERCEDE;}
    return HC_CONTINUE;
}
public AfterKilled(id,attacker,gib){gDeathCalls[id]++;return HC_CONTINUE;}
public CombatStatus()
{
    new id=FixturePlayer();if(!is_user_connected(id))return PLUGIN_HANDLED;
    server_print("[GoldCraft fixture] slot=%d calls=%d deaths=%d attacker=%d blocked=%d",id,gDamageCalls[id],gDeathCalls[id],gLastAttacker[id],gBlockDamage[id]);
    return PLUGIN_HANDLED;
}
public client_disconnected(id){gDamageCalls[id]=gDeathCalls[id]=gLastAttacker[id]=gBlockDamage[id]=0;}
public Position()
{
    if (read_argc()!=5) { server_print("Usage: gc_test_position #userid x y z (CS form only)"); return PLUGIN_HANDLED; }
    new arg[32];read_argv(1,arg,charsmax(arg));new id=find_player_ex(FindPlayer_MatchUserId, str_to_num(arg[1]));
    if (!is_user_alive(id)||gc_get_form(id)!=GC_FORM_CS) return PLUGIN_HANDLED;
    new Float:pos[3],Float:zero[3];
    for (new i=0;i<3;i++){read_argv(i+2,arg,charsmax(arg));pos[i]=str_to_float(arg);if(floatabs(pos[i])>8192.0)return PLUGIN_HANDLED;}
    gc_relink_origin(id, pos);set_entvar(id,var_velocity,zero);
    server_print("[GoldCraft fixture] positioned slot=%d",id);return PLUGIN_HANDLED;
}
public Equip()
{
    if (read_argc()!=3) { server_print("Usage: gc_test_equip #userid <weapon_usp|weapon_ak47|weapon_knife|weapon_hegrenade>"); return PLUGIN_HANDLED; }
    new who[20],name[32];read_argv(1,who,charsmax(who));read_argv(2,name,charsmax(name));
    new id=find_player_ex(FindPlayer_MatchUserId, str_to_num(who[1]));if(!is_user_alive(id))return PLUGIN_HANDLED;
    if (!equal(name,"weapon_usp")&&!equal(name,"weapon_ak47")&&!equal(name,"weapon_knife")&&!equal(name,"weapon_hegrenade"))return PLUGIN_HANDLED;
    // Exact ReAPI 5.29 declarations: rg_give_item returns the weapon entity;
    // rg_switch_weapon takes that entity index, not a CSW enum.
    new weapon=rg_give_item(id,name,GT_REPLACE);
    if(!is_nullent(weapon)){
        new WeaponIdType:kind=get_member(weapon,m_iId);gc_set_bpammo(id,kind,120);
        server_print("[GoldCraft fixture] slot=%d weapon=%d switched=%d",id,weapon,rg_switch_weapon(id,weapon));
    }
    return PLUGIN_HANDLED;
}
public Cleanup()
{
    if (!is_nullent(gPortal)) rg_remove_entity(gPortal);
    if (!is_nullent(gDestination)) rg_remove_entity(gDestination);
    gPortal = gDestination = 0;
    server_print("[GoldCraft fixture] cleaned");
    return PLUGIN_HANDLED;
}
public plugin_end() { Cleanup(); }
public CreatePortal()
{
    if (read_argc() != 7) { server_print("Usage: gc_test_portal sx sy sz dx dy dz (GoldSrc units)"); return PLUGIN_HANDLED; }
    new Float:start[3], Float:finish[3], arg[32];
    for (new i=0; i<6; i++) {
        read_argv(i+1, arg, charsmax(arg)); new Float:value = str_to_float(arg);
        if (floatabs(value) > 8192.0) return PLUGIN_HANDLED;
        if (i<3) start[i] = value; else finish[i-3] = value;
    }
    Cleanup();
    // Both are built-in game classes; keep their names unchanged for the game hash table.
    gDestination = rg_create_entity("info_target", true);
    if (is_nullent(gDestination)) return PLUGIN_HANDLED;
    set_entvar(gDestination, var_targetname, "goldcraft_test_destination");
    gc_relink_origin(gDestination, finish);
    dllfunc(DLLFunc_Spawn, gDestination);
    gPortal = rg_create_entity("trigger_teleport", true);
    if (is_nullent(gPortal)) { Cleanup(); return PLUGIN_HANDLED; }
    set_entvar(gPortal, var_target, "goldcraft_test_destination");
    gc_relink_origin(gPortal, start);
    dllfunc(DLLFunc_Spawn, gPortal); // Installs the real CBaseTrigger::TeleportTouch callback.
    new Float:mins[3] = {-20.0,-20.0,0.0}, Float:maxs[3] = {20.0,20.0,72.0};
    engfunc(EngFunc_SetSize, gPortal, mins, maxs);
    gc_relink_origin(gPortal, start);
    server_print("[GoldCraft fixture] portal=%d destination=%d", gPortal, gDestination);
    return PLUGIN_HANDLED;
}
