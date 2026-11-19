// SPDX-License-Identifier: GPL-3.0-or-later
// Local fixture: inspect real inventory and exercise the actual purchase command.
#include <amxmodx>
#include <fakemeta>
#include <hamsandwich>
#include <reapi>

public plugin_init()
{
    register_plugin("CoF local purchase checks", "0.1.0", "GoldCraft contributors");
    register_srvcmd("cof_live_status", "Status");
    register_srvcmd("cof_live_purchase", "Purchase");
    register_srvcmd("cof_live_clear_batch", "ClearBatch");
    register_srvcmd("cof_live_test_funds", "TestFunds");
    register_srvcmd("cof_live_burn_setup", "BurnSetup");
    register_srvcmd("cof_live_burn_state", "BurnState");
    register_srvcmd("cof_live_join", "Join");
    register_srvcmd("cof_live_burn_wave", "BurnWave");
}

// Isolated zs_deadend fixture. All four entities already came from normal play.
// The level's own BSP separates the second target from the original thrown flare.
new burnFlare, burnSerial, burnTargets[3], burnTargetSerial[3], burnHits[3];
new HamHook:burnHook;
new const Float:burnOrigin[3] = {-2000.0, 1288.0, 0.03125};
new const Float:burnCenters[3][3] = {
    {-2000.0, 1248.0, 36.0}, {-2000.0, 1368.0, 36.0}, {-1860.0, 1288.0, 36.0}
};
public BurnSetup()
{
    new slot[12], user[12], address[32], map[32];
    read_argv(1,slot,charsmax(slot)); read_argv(2,user,charsmax(user));
    new id=str_to_num(slot), userid=str_to_num(user);
    if (!Human(id,userid) || !is_user_alive(id) || get_cvar_num("csol_game_mode")!=2)
        return PLUGIN_HANDLED;
    get_user_ip(id,address,charsmax(address),1); get_mapname(map,charsmax(map));
    if (!equal(address,"127.0.0.1") || !equal(map,"zs_deadend")) return PLUGIN_HANDLED;
    new entity, flare, candidates[3], count;
    while ((entity=rg_find_ent_by_class(entity,"weapon_cof_flare"))>0)
        if (get_entvar(entity,var_body)==1 && get_entvar(entity,var_solid)==SOLID_TRIGGER &&
            (get_entvar(entity,var_flags)&FL_ONGROUND) && is_nullent(get_entvar(entity,var_owner)))
            flare=entity;
    entity=0;
    while (count<sizeof candidates && (entity=rg_find_ent_by_class(entity,"monster_entity"))>0)
        if (get_entvar(entity,var_deadflag)==DEAD_NO && Float:get_entvar(entity,var_health)>0.0 &&
            Float:get_entvar(entity,var_takedamage)!=DAMAGE_NO && !(get_entvar(entity,var_effects)&EF_NODRAW))
            candidates[count++]=entity;
    server_print("[cof-burn] setup flare=%d candidates=%d",flare,count);
    if (!flare || count!=sizeof candidates) return PLUGIN_HANDLED;
    engfunc(EngFunc_SetOrigin,flare,burnOrigin);
    burnFlare=flare; burnSerial=pev_serial(flare);
    for (new i=0;i<sizeof candidates;i++)
    {
        entity=candidates[i];
        new Float:mins[3], Float:maxs[3], Float:point[3];
        get_entvar(entity,var_mins,mins); get_entvar(entity,var_maxs,maxs);
        for (new axis=0;axis<3;axis++)
            point[axis]=burnCenters[i][axis]-(mins[axis]+maxs[axis])*0.5;
        // Hold the real targets still and leave enough health to observe natural expiry.
        set_entvar(entity,var_nextthink,0.0); set_entvar(entity,var_movetype,MOVETYPE_NONE);
        set_entvar(entity,var_velocity,Float:{0.0,0.0,0.0});
        set_entvar(entity,var_health,1000.0); set_entvar(entity,var_armorvalue,50.0);
        engfunc(EngFunc_SetOrigin,entity,point);
        burnTargets[i]=entity; burnTargetSerial[i]=pev_serial(entity); burnHits[i]=0;
    }
    if (!burnHook) burnHook=RegisterHamFromEntity(Ham_TakeDamage,candidates[0],"BurnHit",1);
    return BurnState();
}
public BurnHit(entity,inflictor,attacker,Float:amount,bits)
{
    if (inflictor!=burnFlare || !pev_valid(inflictor) || pev_serial(inflictor)!=burnSerial) return HAM_IGNORED;
    for (new i=0;i<sizeof burnTargets;i++)
        if (entity==burnTargets[i] && pev_serial(entity)==burnTargetSerial[i])
        {
            ++burnHits[i];
            server_print("[cof-burn] hit t=%.3f target=%d index=%d source=%d actor=%d amount=%.1f bits=%d hp=%.1f",
                get_gametime(),i,entity,inflictor,attacker,amount,bits,Float:get_entvar(entity,var_health));
        }
    return HAM_IGNORED;
}
public BurnState()
{
    if (!burnFlare || !pev_valid(burnFlare) || pev_serial(burnFlare)!=burnSerial)
    {
        server_print("[cof-burn] no tracked world flare"); return PLUGIN_HANDLED;
    }
    new Float:origin[3], Float:start[3], tr=create_tr2();
    get_entvar(burnFlare,var_origin,origin);
    for(new axis=0;axis<3;axis++) start[axis]=origin[axis];
    start[2]+=1.0;
    server_print("[cof-burn] t=%.3f flare=%d body=%d next=%.3f",get_gametime(),burnFlare,
        get_entvar(burnFlare,var_body),Float:get_entvar(burnFlare,var_nextthink));
    for (new i=0;i<sizeof burnTargets;i++)
    {
        new entity=burnTargets[i];
        if (!pev_valid(entity) || pev_serial(entity)!=burnTargetSerial[i]) continue;
        new Float:lo[3], Float:hi[3], Float:center[3], Float:fraction;
        get_entvar(entity,var_absmin,lo); get_entvar(entity,var_absmax,hi);
        for(new axis=0;axis<3;axis++) center[axis]=(lo[axis]+hi[axis])*0.5;
        engfunc(EngFunc_TraceLine,start,center,IGNORE_MONSTERS,burnFlare,tr);
        get_tr2(tr,TR_flFraction,fraction);
        server_print("[cof-burn] target=%d index=%d hp=%.2f armor=%.2f range=%.2f trace=%.3f hits=%d",i,
            entity,Float:get_entvar(entity,var_health),Float:get_entvar(entity,var_armorvalue),
            get_distance_f(origin,center),fraction,burnHits[i]);
    }
    free_tr2(tr);
    return PLUGIN_HANDLED;
}
bool:Human(id, userid)
{
    return id >= 1 && id <= MaxClients && is_user_connected(id) &&
        !is_user_bot(id) && !is_user_hltv(id) && get_user_userid(id) == userid;
}
// Confirm the sandbox client's chosen appearance without focusing its window.
public Join()
{
    new slot[12], user[12], chosen[12], address[32], map[32];
    if (read_argc()!=4) return PLUGIN_HANDLED;
    read_argv(1,slot,charsmax(slot)); read_argv(2,user,charsmax(user));
    read_argv(3,chosen,charsmax(chosen));
    new id=str_to_num(slot);
    if (!Human(id,str_to_num(user)) || is_user_alive(id) || get_member(id,m_iTeam)!=TEAM_CT ||
        get_cvar_num("csol_game_mode")!=2 || str_to_num(chosen)<=0) return PLUGIN_HANDLED;
    get_user_ip(id,address,charsmax(address),1); get_mapname(map,charsmax(map));
    if (equal(map,"zs_deadend") && equal(address,"127.0.0.1"))
        engclient_cmd(id,"csol_character_select",chosen);
    return PLUGIN_HANDLED;
}
public BurnWave()
{
    new slot[12], user[12], address[32], map[32];
    read_argv(1,slot,charsmax(slot)); read_argv(2,user,charsmax(user));
    new id=str_to_num(slot);
    if (!Human(id,str_to_num(user)) || !is_user_alive(id) || get_cvar_num("csol_game_mode")!=2)
        return PLUGIN_HANDLED;
    get_user_ip(id,address,charsmax(address),1); get_mapname(map,charsmax(map));
    if (equal(map,"zs_deadend") && equal(address,"127.0.0.1"))
    {
        // Center of BSP trigger 1 at standing height. The normal trigger manager
        // selects and spawns the wave. Keep the lone test CT alive until expiry.
        set_entvar(id,var_takedamage,DAMAGE_NO);
        engfunc(EngFunc_SetOrigin,id,Float:{1088.0,-1728.0,36.03125});
        server_print("[cof-burn] CT placed inside native wave trigger 1");
    }
    return PLUGIN_HANDLED;
}
Inspect(id)
{
    server_print("[CoF live] player=%d userid=%d alive=%d health=%.1f money=%d team=%d",
        id, get_user_userid(id), is_user_alive(id), Float:get_entvar(id,var_health),
        get_member(id,m_iAccount), get_member(id,m_iTeam));
    new active=get_member(id,m_pActiveItem);
    for(new slot;slot<6;slot++)
    {
        new entity=get_member(id,m_rgpPlayerItems,slot), count;
        while(!is_nullent(entity) && count++<64)
        {
            new classname[64];get_entvar(entity,var_classname,classname,charsmax(classname));
            new ammo=get_member(entity,m_Weapon_iPrimaryAmmoType);
            new reserve=ammo>=0&&ammo<32?get_member(id,m_rgAmmo,ammo):-1;
            server_print("[CoF live] slot=%d entity=%d id=%d clip=%d reserve=%d active=%d classname=%s",
                slot,entity,get_member(entity,m_iId),get_member(entity,m_Weapon_iClip),reserve,entity==active,classname);
            entity=get_member(entity,m_pNext);
        }
    }
}
public Status()
{
    new map[32];
    get_mapname(map, charsmax(map));
    server_print("[CoF live] map=%s time=%.2f ct_wins=%d started=%d freeze=%d terminating=%d",
        map, get_gametime(), get_member_game(m_iNumCTWins), get_member_game(m_bGameStarted),
        get_member_game(m_bFreezePeriod), get_member_game(m_bRoundTerminating));
    for(new id=1;id<=MaxClients;id++)
        if(is_user_connected(id)&&!is_user_bot(id)&&!is_user_hltv(id))Inspect(id);
    return PLUGIN_HANDLED;
}

public ClearBatch()
{
    if (read_argc() != 3 || !IsHamValid(Ham_TakeDamage)) return PLUGIN_HANDLED;
    new slot[12], user[12], map[32], address[32];
    read_argv(1, slot, charsmax(slot));
    read_argv(2, user, charsmax(user));
    new id = str_to_num(slot), userid = str_to_num(user);
    if (!Human(id, userid) || !is_user_alive(id) || !get_member_game(m_bGameStarted) ||
        get_member_game(m_bFreezePeriod) || get_member_game(m_bRoundTerminating) ||
        get_member_game(m_iNumCTWins) != 0 || get_cvar_num("csol_game_mode") != 2)
        return PLUGIN_HANDLED;
    get_mapname(map, charsmax(map));
    get_user_ip(id, address, charsmax(address), 1);
    if (!equal(map, "zs_deadend") || !equal(address, "127.0.0.1")) return PLUGIN_HANDLED;

    new entity, examined, damaged;
    while (examined++ < 256 && (entity = rg_find_ent_by_class(entity, "monster_entity")))
    {
        if (get_entvar(entity, var_solid) == SOLID_NOT || get_entvar(entity, var_deadflag) != DEAD_NO ||
            (get_entvar(entity, var_effects) & EF_NODRAW) || Float:get_entvar(entity, var_health) <= 0.0)
            continue;
        ExecuteHamB(Ham_TakeDamage, entity, id, id, 2000.0, DMG_BULLET);
        ++damaged;
        if (!Human(id, userid) || !is_user_alive(id) || get_member_game(m_iNumCTWins) != 0 ||
            get_member_game(m_bRoundTerminating)) break;
    }
    server_print("[CoF live] synthetic_monster_damage=1 targets=%d", damaged);
    return PLUGIN_HANDLED;
}
// Refill only the named local test actor. Purchase still pays the real price.
public TestFunds()
{
    if(read_argc()!=3)return PLUGIN_HANDLED;
    new slot[12], user[12], address[32], map[32];
    read_argv(1,slot,charsmax(slot));read_argv(2,user,charsmax(user));
    new id=str_to_num(slot), userid=str_to_num(user);
    if(!Human(id,userid)||!is_user_alive(id)||get_cvar_num("csol_game_mode")!=2)return PLUGIN_HANDLED;
    get_user_ip(id,address,charsmax(address),1);
    get_mapname(map,charsmax(map));
    if(!equal(address,"127.0.0.1")||!equal(map,"zs_deadend"))return PLUGIN_HANDLED;
    new before=get_member(id,m_iAccount);
    rg_add_account(id,32000,AS_SET,false);
    server_print("[CoF live] fixture_balance before=%d after=%d",before,get_member(id,m_iAccount));
    return PLUGIN_HANDLED;
}
public Purchase()
{
    if(read_argc()!=4 && read_argc()!=5)return PLUGIN_HANDLED;
    new slot[12],user[12],request[12];read_argv(1,slot,charsmax(slot));read_argv(2,user,charsmax(user));read_argv(3,request,charsmax(request));
    new id=str_to_num(slot),userid=str_to_num(user),token=str_to_num(request);
    if(!Human(id,userid)||!is_user_alive(id)||token<1||token>65535)return PLUGIN_HANDLED;
    new item[12]="896", address[32];
    if(read_argc()==5)read_argv(4,item,charsmax(item));
    new itemid=str_to_num(item);
    if((itemid<896 || itemid>904) && itemid!=976 && itemid!=977)return PLUGIN_HANDLED;
    get_user_ip(id,address,charsmax(address),1);
    if(!equal(address,"127.0.0.1"))return PLUGIN_HANDLED;
    new before=get_member(id,m_iAccount);
    engclient_cmd(id,"csol_buy_menu",request);
    if(!Human(id,userid)||!is_user_alive(id))return PLUGIN_HANDLED;
    engclient_cmd(id,"csol_buy",request,item);
    if(!Human(id,userid))return PLUGIN_HANDLED;
    server_print("[CoF live] actual_server_purchase item=%d before=%d after=%d debit=%d synthetic_command=1 physical_ui=0",
        itemid,before,get_member(id,m_iAccount),before-get_member(id,m_iAccount));
    Inspect(id);
    return PLUGIN_HANDLED;
}
