// SPDX-License-Identifier: GPL-3.0-or-later
// Owned headless actor only. Real DLL inventory, RunPlayerMove, Think and
// CBreakable damage; scoped protocol qualification is a declared test seam.
#include <amxmodx>
#include <reapi>
#include <fakemeta>
#include <hamsandwich>
#include <csol_weapons>
#include <csol_scenario_admin>

#define LOG_FILE "csol_melee_live_test.log"
new gEnabled, gPlayer, gUserid, gSerial, gStage, gChecks, gFailures;
new gKnife, gStock, gTargets[4], gTargetSerial[4], gCompletions, gAnimations;
new gLastAnimation, gLastContact, gLastWeapon, gLastSerial;
new bool:gActive, bool:gCleaning, bool:gShort, bool:gOrdinaryOnly;
new Float:gStart, Float:gDue, Float:gNextMove, Float:gHealth[4], Float:gOrigin[3], Float:gNextMonsterReport;
new Float:gBeforePrimary, Float:gBeforeSecondary, Float:gBeforeNextAttack;
new gBeamMode;

public plugin_precache()
{
    precache_model("models/w_usp.mdl"); precache_model("models/glassgibs.mdl");
    precache_sound("debris/glass1.wav"); precache_sound("debris/glass2.wav");
    precache_sound("debris/glass3.wav"); precache_sound("debris/bustglass1.wav");
    precache_sound("debris/bustglass2.wav");
}
public plugin_init()
{
    register_plugin("CSOL melee native test", "0.1.0", "GoldCraft contributors");
    gEnabled=create_cvar("csol_melee_test", "0");
    register_srvcmd("csol_melee_test_begin", "Begin");
    register_srvcmd("csol_melee_test_end", "End");
    register_forward(FM_StartFrame,"Frame");
    RegisterHookChain(RG_CBasePlayer_Spawn,"PlayerSpawned",true);
    new msg=get_user_msgid("CSOMelee"); if(msg)register_message(msg,"MeleeMessage");
}
Emit(const format[],any:...)
{
    new text[600];vformat(text,charsmax(text),format,2);
    server_print("[Melee test] t=%.3f %s",get_gametime(),text);
    log_to_file(LOG_FILE,"t=%.3f %s",get_gametime(),text);
}
bool:Owned()
{
    return gPlayer>0 && gUserid>0 && is_user_connected(gPlayer) &&
        get_user_userid(gPlayer)==gUserid && pev_valid(gPlayer)==2 && pev_serial(gPlayer)==gSerial;
}
bool:Target(index)
{ return gTargets[index]>MaxClients && pev_valid(gTargets[index])==2 && pev_serial(gTargets[index])==gTargetSerial[index]; }
ClearTargets()
{
    for(new i=0;i<4;i++) {if(Target(i))engfunc(EngFunc_RemoveEntity,gTargets[i]);gTargets[i]=gTargetSerial[i]=0;}
}
Cleanup()
{
    gActive=false;gCleaning=true;ClearTargets();
    if(Owned())rh_drop_client(gPlayer,"Melee test complete");
    gPlayer=gUserid=gSerial=0;
    csol_weapon_zombie_mode(false);set_cvar_num("csol_beam_damage_mode",gBeamMode);
    gCleaning=false;
}
Stop(const reason[])
{Emit("event=end reason=%s stage=%d checks=%d failures=%d",reason,gStage,gChecks,gFailures);Cleanup();}
bool:Check(bool:value,const label[])
{
    gChecks++;if(value){Emit("event=check ok=1 label=%s",label);return true;}
    gFailures++;Emit("event=check ok=0 label=%s",label);Stop(label);return false;
}
public End(){if(gActive)Stop("operator_stop");return PLUGIN_HANDLED;}
public plugin_end(){if(gActive)Cleanup();}
public client_disconnected(id)
{if(id==gPlayer && gActive && !gCleaning)Stop("owned_disconnect");}
public PlayerSpawned(id)
{
    if(!gActive || id!=gPlayer || !Owned())return HC_CONTINUE;
    if(!IsReapiHookOriginalWasCalled(RG_CBasePlayer_Spawn)){Stop("native_spawn_suppressed");return HC_CONTINUE;}
    // Native Spawn clears this flag for non-CBot fake actors. Restore only
    // after proving the fixture's userid/serial identity, as CBot::Spawn does.
    set_entvar(id,var_flags,get_entvar(id,var_flags)|FL_FAKECLIENT|FL_CLIENT);
    Emit("event=spawn alive=%d original=1",is_user_alive(id));
    return HC_CONTINUE;
}

public Begin()
{
    if(!get_pcvar_num(gEnabled) || gActive)return PLUGIN_HANDLED;
    new map[32],arg[8];get_mapname(map,charsmax(map));read_argv(1,arg,charsmax(arg));
    if(!equal(map,"zs_deadend") || MaxClients!=32 || get_cvar_num("csol_game_mode")!=2 || get_cvar_num("sypb_quota")!=0)return PLUGIN_HANDLED;
    for(new id=1;id<=MaxClients;id++)if(is_user_connected(id))return PLUGIN_HANDLED;
    gActive=true;gStage=gChecks=gFailures=gCompletions=gAnimations=0;
    gShort=str_to_num(arg)==1;gOrdinaryOnly=str_to_num(arg)==2;
    gStart=get_gametime();gDue=gStart;gNextMove=0.0;gBeamMode=get_cvar_num("csol_beam_damage_mode");
    Emit("event=begin map=%s short=%d qualification=scoped_fake_protocol actual_engine=1",map,gShort);
    gPlayer=engfunc(EngFunc_CreateFakeClient,"GC_Melee_Probe");
    if(gPlayer<1 || !is_user_connected(gPlayer)){Stop("fake_allocation_failed");return PLUGIN_HANDLED;}
    gUserid=get_user_userid(gPlayer);
    if(pev_valid(gPlayer)==2)engfunc(EngFunc_FreeEntPrivateData,gPlayer);
    gSerial=pev_serial(gPlayer);
    set_pev(gPlayer,pev_flags,pev(gPlayer,pev_flags)|FL_FAKECLIENT|FL_CLIENT);
    dllfunc(DLLFunc_ClientPutInServer,gPlayer);
    if(!Owned()){Stop("fake_private_failed");return PLUGIN_HANDLED;}
    set_entvar(gPlayer,var_flags,get_entvar(gPlayer,var_flags)|FL_FAKECLIENT|FL_CLIENT);
    set_member(gPlayer,m_iMenu,Menu_ChooseTeam);set_member(gPlayer,m_iJoiningState,PICKINGTEAM);
    engclient_cmd(gPlayer,"jointeam","2");engclient_cmd(gPlayer,"specmode","3");engclient_cmd(gPlayer,"joinclass","1");
    Emit("event=selection joining=%d alive=%d observer=%d team=%d",get_member(gPlayer,m_iJoiningState),is_user_alive(gPlayer),get_entvar(gPlayer,var_iuser1),get_member(gPlayer,m_iTeam));
    return PLUGIN_HANDLED;
}
public MeleeMessage(message,destination,recipient)
{
    if(!gActive || recipient!=gPlayer || get_msg_args()!=6)return PLUGIN_CONTINUE;
    gCompletions++;gLastWeapon=get_msg_arg_int(2);gLastSerial=get_msg_arg_int(3);
    gLastAnimation=get_msg_arg_int(4);gLastContact=get_msg_arg_int(5);
    Emit("event=completion weapon=%d serial=%d animation=%d contact=%d",gLastWeapon,gLastSerial,gLastAnimation,gLastContact);
    return PLUGIN_CONTINUE;
}
// Restores eligibility fields before the next engine frame. The server runner
// stops the owned server immediately on any Pawn error, including native abort.
Grant(id,bool:expect)
{
    new flags=get_entvar(gPlayer,var_flags),protocol[32];get_user_info(gPlayer,"csol_protocol",protocol,charsmax(protocol));
    set_user_info(gPlayer,"csol_protocol","1");set_entvar(gPlayer,var_flags,flags&~FL_FAKECLIENT);
    new can=csol_weapon_can_buy(gPlayer,id),result=csol_weapon_buy(gPlayer,id);
    if(Owned()){
        set_entvar(gPlayer,var_flags,(get_entvar(gPlayer,var_flags)&~FL_FAKECLIENT)|(flags&FL_FAKECLIENT));
        set_user_info(gPlayer,"csol_protocol",protocol);
    }
    Emit("event=grant id=%d can=%d result=%d expected=%d",id,can,result,expect);
    return Check(bool:can==expect && bool:result==expect,"public_delivery_result");
}
bool:Knife(id)
{
    gKnife=get_member(gPlayer,m_rgpPlayerItems,KNIFE_SLOT);
    if(gKnife>MaxClients && pev_valid(gKnife)==2)
        Emit("event=knife entity=%d id=%d next=%d owner=%d active=%d",gKnife,get_member(gKnife,m_iId),
            get_member(gKnife,m_pNext),get_entvar(gKnife,var_owner),get_member(gPlayer,m_pActiveItem));
    return gKnife>MaxClients && pev_valid(gKnife)==2 && get_member(gKnife,m_iId)==id &&
        get_member(gKnife,m_pNext)==NULLENT && get_entvar(gKnife,var_owner)==gPlayer && get_member(gPlayer,m_pActiveItem)==gKnife;
}
Move(buttons)
{
    new Float:angles[3];get_entvar(gPlayer,var_v_angle,angles);
    engfunc(EngFunc_RunPlayerMove,gPlayer,angles,0.0,0.0,0.0,buttons,0,10);
}
Aim(Float:yaw)
{new Float:a[3];a[1]=yaw;set_entvar(gPlayer,var_v_angle,a);set_entvar(gPlayer,var_angles,a);}
bool:Ready()
{
    return is_user_alive(gPlayer) && !get_member_game(m_bFreezePeriod) && get_member(gPlayer,m_bCanShoot) &&
        Float:get_member(gPlayer,m_flNextAttack)<=0.0 && Float:get_member(gKnife,m_Weapon_flNextPrimaryAttack)<=0.0 &&
        Float:get_member(gKnife,m_Weapon_flNextSecondaryAttack)<=0.0;
}
bool:Fire(buttons,animation)
{
    Move(buttons);Move(0);gAnimations++;
    Emit("event=fire weapon=%d animation=%d nextthink=%.4f primary=%.4f secondary=%.4f",get_member(gKnife,m_iId),
        get_entvar(gPlayer,var_weaponanim),Float:get_entvar(gKnife,var_nextthink),
        Float:get_member(gKnife,m_Weapon_flNextPrimaryAttack),Float:get_member(gKnife,m_Weapon_flNextSecondaryAttack));
    return Check(get_entvar(gPlayer,var_weaponanim)==animation && Float:get_entvar(gKnife,var_nextthink)>get_gametime(),"native_input_attack_started");
}
CreateTarget(slot,Float:distance,Float:yaw,Float:health)
{
    new entity=rg_create_entity("func_breakable"),Float:eye[3],Float:view[3];
    if(entity<=MaxClients || pev_valid(entity)!=2)return 0;
    get_entvar(gPlayer,var_origin,eye);get_entvar(gPlayer,var_view_ofs,view);
    for(new j=0;j<3;j++)eye[j]+=view[j];
    eye[0]+=distance*floatcos(yaw,degrees);eye[1]+=distance*floatsin(yaw,degrees);
    set_entvar(entity,var_model,"models/w_usp.mdl");set_entvar(entity,var_health,health);
    dllfunc(DLLFunc_Spawn,entity);
    set_entvar(entity,var_solid,SOLID_BBOX);set_entvar(entity,var_movetype,MOVETYPE_NONE);
    engfunc(EngFunc_SetSize,entity,Float:{-4.0,-4.0,-4.0},Float:{4.0,4.0,4.0});
    engfunc(EngFunc_SetOrigin,entity,eye);set_entvar(entity,var_takedamage,DAMAGE_YES);
    gTargets[slot]=entity;gTargetSerial[slot]=pev_serial(entity);gHealth[slot]=health;
    return entity;
}
bool:Hurt(slot)
{return Target(slot) && Float:get_entvar(gTargets[slot],var_health)<gHealth[slot];}
bool:Unhurt(slot)
{return Target(slot) && Float:get_entvar(gTargets[slot],var_health)==gHealth[slot];}
OrdinaryTarget()
{
    new entity,model[160],count;new bool:report=get_gametime()>=gNextMonsterReport;
    if(report)gNextMonsterReport=get_gametime()+5.0;
    while((entity=rg_find_ent_by_class(entity,"monster_entity"))>0)
    {
        if(report&&pev_valid(entity)==2&&Float:get_entvar(entity,var_health)>0.0&&get_entvar(entity,var_solid)!=SOLID_NOT&&count++<4){
            get_entvar(entity,var_model,model,charsmax(model));
            Emit("event=monster_candidate entity=%d model=%s health=%.3f damage=%.3f solid=%d",entity,model,
                Float:get_entvar(entity,var_health),Float:get_entvar(entity,var_takedamage),get_entvar(entity,var_solid));
        }
        if(pev_valid(entity)!=2 || Float:get_entvar(entity,var_health)<=1.0 ||
            Float:get_entvar(entity,var_takedamage)==DAMAGE_NO || get_entvar(entity,var_solid)==SOLID_NOT)continue;
        get_entvar(entity,var_model,model,charsmax(model));
        // The original first-wave ordinary appearance. Do not substitute an
        // elite/Boss merely because it shares the pool entity classname.
        if(equal(model,"models/player/zombi_host/zombi_host.mdl"))return entity;
    }
    if(report)Emit("event=ordinary_wait living=%d",count);
    return 0;
}
bool:OrdinaryHit(entity)
{
    new serial=pev_serial(entity),Float:before=Float:get_entvar(entity,var_health),Float:velocity[3],Float:afterVelocity[3],model[160];
    get_entvar(entity,var_velocity,velocity);get_entvar(entity,var_model,model,charsmax(model));
    // Exercise the real pooled monster TakeDamage vcall with a nonlethal
    // amount. Native usercmd/Think/TraceAttack is checked separately above.
    new result=ExecuteHamB(Ham_TakeDamage,entity,gPlayer,gPlayer,1.0,DMG_BULLET|DMG_NEVERGIB);
    if(!Check(pev_valid(entity)==2&&pev_serial(entity)==serial,"ordinary_target_retains_exact_identity"))return false;
    new Float:after=Float:get_entvar(entity,var_health);get_entvar(entity,var_velocity,afterVelocity);
    Emit("event=ordinary_hit weapon=%d model=%s result=%d before=%.4f after=%.4f",get_member(gKnife,m_iId),model,result,before,after);
    return Check(after>0.0&&after<before,"native_ordinary_nonlethal_damage_commits") &&
        Check(velocity[0]==afterVelocity[0]&&velocity[1]==afterVelocity[1]&&velocity[2]==afterVelocity[2],"knife_ordinary_predicate_does_not_add_impulse");
}
bool:OrdinaryKill(entity)
{
    new serial=pev_serial(entity),Float:before=Float:get_entvar(entity,var_health);
    // The actual ordinary monster dispatcher owns death, rewards and callbacks.
    // Do not call Killed directly or alter the monster's authoritative health.
    new result=ExecuteHamB(Ham_TakeDamage,entity,gPlayer,gPlayer,1000000.0,DMG_BULLET|DMG_NEVERGIB);
    if(!Check(pev_valid(entity)==2&&pev_serial(entity)==serial,"ordinary_death_retains_pool_entity_identity"))return false;
    Emit("event=ordinary_kill weapon=%d result=%d before=%.4f after=%.4f dead=%d solid=%d",get_member(gKnife,m_iId),result,
        before,Float:get_entvar(entity,var_health),get_entvar(entity,var_deadflag),get_entvar(entity,var_solid));
    return Check(Float:get_entvar(entity,var_health)<=0.0&&get_entvar(entity,var_deadflag)==DEAD_DEAD,
        "native_ordinary_death_dispatch_completed");
}
bool:Area()
{
    // Use the map's proven free first-trigger location; the actual engine must
    // confirm each ray. No mocked trace or modified world BSP is involved.
    new Float:pos[3]={1088.0,-1728.0,36.0},trace=create_tr2();
    engfunc(EngFunc_TraceHull,pos,pos,0,HULL_HUMAN,gPlayer,trace);
    new blocked=get_tr2(trace,TR_StartSolid)||get_tr2(trace,TR_AllSolid);free_tr2(trace);
    if(blocked)return false;
    engfunc(EngFunc_SetOrigin,gPlayer,pos);get_entvar(gPlayer,var_origin,gOrigin);
    set_entvar(gPlayer,var_takedamage,DAMAGE_NO);Aim(0.0);return true;
}
public Frame()
{
    if(!gActive || gCleaning)return FMRES_IGNORED;
    if(!get_pcvar_num(gEnabled)||!Owned()){Stop("identity_lost");return FMRES_IGNORED;}
    for(new id=1;id<=MaxClients;id++)if(id!=gPlayer&&is_user_connected(id)){Stop("other_client_joined");return FMRES_IGNORED;}
    if(!(get_entvar(gPlayer,var_flags)&FL_FAKECLIENT)){Stop("qualification_scope_leaked");return FMRES_IGNORED;}
    new Float:now=get_gametime();
    if(now-gStart>100.0){Stop("timeout");return FMRES_IGNORED;}
    if(now>=gNextMove){gNextMove=now+0.01;Move(0);}
    if(now<gDue)return FMRES_IGNORED;
    switch(gStage)
    {
    case 0:
    {
        if(!is_user_alive(gPlayer)||get_member_game(m_bFreezePeriod))return FMRES_IGNORED;
        if(!Check(Area(),"actual_engine_open_test_position"))return FMRES_IGNORED;
        csol_weapon_zombie_mode(true);csol_weapon_player_role(gPlayer,1);
        new old=get_member(gPlayer,m_rgpPlayerItems,KNIFE_SLOT);
        if(!Grant(1022,true)||!Check(Knife(1022),"dragon_sole_active_knife"))return FMRES_IGNORED;
        if(!Check(old!=gKnife && (pev_valid(old)!=2 || (get_entvar(old,var_flags)&FL_KILLME)),"old_knife_retired"))return FMRES_IGNORED;
        gBeforePrimary=Float:get_member(gKnife,m_Weapon_flNextPrimaryAttack);gBeforeSecondary=Float:get_member(gKnife,m_Weapon_flNextSecondaryAttack);
        gBeforeNextAttack=Float:get_member(gPlayer,m_flNextAttack);
        if(!Grant(1022,false)||!Check(Knife(1022)&&Float:get_member(gKnife,m_Weapon_flNextPrimaryAttack)==gBeforePrimary &&
            Float:get_member(gKnife,m_Weapon_flNextSecondaryAttack)==gBeforeSecondary && Float:get_member(gPlayer,m_flNextAttack)==gBeforeNextAttack,"same_dragon_reject_preserves_timers"))return FMRES_IGNORED;
        if(gShort){Emit("event=success mode=map_reload_grant checks=%d",gChecks);Stop("completed");return FMRES_IGNORED;}
        if(gOrdinaryOnly){gStage=17;return FMRES_IGNORED;}
        // A duplicate GiveNamedItemEx can return its soon-to-be-removed pickup.
        // Use the authoritative existing pistol slot after normal delivery.
        rg_give_item(gPlayer,"weapon_usp");gStock=get_member(gPlayer,m_rgpPlayerItems,PISTOL_SLOT);
        if(!Check(gStock>MaxClients&&pev_valid(gStock)==2&&get_member(gStock,m_iId)==WEAPON_USP&&
            get_entvar(gStock,var_owner)==gPlayer,"stock_switch_target_is_owned_usp"))return FMRES_IGNORED;
        if(!Check(bool:rg_switch_weapon(gPlayer,gKnife),"initial_knife_selection"))return FMRES_IGNORED;
        CreateTarget(0,48.0,0.0,10000.0);CreateTarget(1,60.0,65.0,10000.0);gStage=1;
    }
    case 1:if(Ready()){if(Fire(IN_ATTACK,1)){gStage=2;gDue=now+0.18;}}
    case 2:
    {
        if(!Check(Unhurt(0)&&Unhurt(1),"dragon_damage_is_delayed"))return FMRES_IGNORED;
        gStage=3;gDue=now+0.53;
    }
    case 3:
    {
        if(!Check(Hurt(0)&&Unhurt(1),"dragon_primary_hits_front_excludes_65_degrees"))return FMRES_IGNORED;
        new Float:think=get_entvar(gKnife,var_nextthink);Move(IN_ATTACK);Move(0);
        if(!Check(Float:get_entvar(gKnife,var_nextthink)==think,"cooldown_rejects_repeated_input"))return FMRES_IGNORED;
        gHealth[0]=Float:get_entvar(gTargets[0],var_health);gStage=4;
    }
    case 4:if(Ready()){if(Fire(IN_ATTACK,2)){gStage=5;gDue=now+0.40;}}
    case 5:
    {
        if(!Check(Hurt(0),"dragon_second_slash_uses_short_delay"))return FMRES_IGNORED;
        gStage=6;
    }
    case 6:if(Ready())
    {
        if(!Fire(IN_ATTACK2,4))return FMRES_IGNORED;
        gHealth[0]=Float:get_entvar(gTargets[0],var_health);
        gBeforePrimary=Float:get_member(gKnife,m_Weapon_flNextPrimaryAttack);gBeforeSecondary=Float:get_member(gKnife,m_Weapon_flNextSecondaryAttack);
        if(!Check(pev_valid(gStock)==2&&get_entvar(gStock,var_owner)==gPlayer&&
            bool:rg_switch_weapon(gPlayer,gStock)&&bool:rg_switch_weapon(gPlayer,gKnife),"switch_to_stock_and_back"))return FMRES_IGNORED;
        if(!Check(Float:get_entvar(gKnife,var_nextthink)==0.0 &&
            Float:get_member(gKnife,m_Weapon_flNextPrimaryAttack)==gBeforePrimary &&
            Float:get_member(gKnife,m_Weapon_flNextSecondaryAttack)==gBeforeSecondary,"switch_cancels_pending_preserves_cooldown"))return FMRES_IGNORED;
        gStage=7;gDue=now+0.85;
    }
    case 7:
    {
        if(!Check(Unhurt(0),"cancelled_stab_does_not_damage"))return FMRES_IGNORED;
        ClearTargets();new old=gKnife;
        if(!Grant(1023,true)||!Check(Knife(1023)&&gKnife!=old,"skull_replaces_dragon_in_same_slot"))return FMRES_IGNORED;
        if(!Grant(1023,false))return FMRES_IGNORED;
        CreateTarget(0,60.0,30.0,10000.0);CreateTarget(1,60.0,-30.0,10000.0);CreateTarget(2,60.0,70.0,10000.0);gStage=8;
    }
    case 8:if(Ready()){if(Fire(IN_ATTACK,8)){gStage=9;gDue=now+1.12;}}
    case 9:
    {
        if(!Check(Hurt(0)&&Hurt(1)&&Unhurt(2),"skull_primary_multi_target_90_degree_sector"))return FMRES_IGNORED;
        ClearTargets();CreateTarget(0,60.0,80.0,10000.0);CreateTarget(1,60.0,-80.0,10000.0);CreateTarget(2,60.0,115.0,10000.0);gStage=10;
    }
    case 10:if(Ready()){if(Fire(IN_ATTACK2,2)){gStage=11;gDue=now+1.12;}}
    case 11:
    {
        if(!Check(Hurt(0)&&Hurt(1)&&Unhurt(2),"skull_secondary_200_degree_sector"))return FMRES_IGNORED;
        ClearTargets();CreateTarget(0,45.0,0.0,1.0);gStage=12;
    }
    case 12:if(Ready()){if(Fire(IN_ATTACK,8)){gStage=13;gDue=now+1.12;}}
    case 13:
    {
        if(!Check(!Target(0)||Float:get_entvar(gTargets[0],var_health)<=0.0||get_entvar(gTargets[0],var_solid)==SOLID_NOT,"native_breakable_glass_destroyed"))return FMRES_IGNORED;
        ClearTargets();CreateTarget(0,48.0,0.0,10000.0);gStage=14;
    }
    case 14:if(Ready())
    {
        if(!Fire(IN_ATTACK2,2))return FMRES_IGNORED;
        ExecuteHamB(Ham_Killed,gPlayer,gPlayer,0);gStage=15;gDue=now+1.15;
    }
    case 15:
    {
        if(!Check(!is_user_alive(gPlayer)&&Unhurt(0),"death_cancels_delayed_damage"))return FMRES_IGNORED;
        // The only actor died, so scenario rules correctly enter all-dead
        // failure. Use the administrator's public replay API; forcing
        // RoundRespawn would leave the authoritative rules in Failed phase.
        if(!Check(bool:get_member_game(m_bRoundTerminating),"sole_actor_death_ends_scenario_round"))return FMRES_IGNORED;
        ClearTargets();new active,requested,maximum,mapid;
        if(!Check(csol_scenario_difficulty_info(active,requested,maximum,mapid)&&
            csol_scenario_replay(mapid),"public_scenario_replay_accepted"))return FMRES_IGNORED;
        gStage=16;gDue=now+0.5;
    }
    case 16:
    {
        if(!is_user_alive(gPlayer)||get_member_game(m_bFreezePeriod))return FMRES_IGNORED;
        if(!Check(!get_member_game(m_bRoundTerminating)&&Area(),"native_replay_restores_playable_round"))return FMRES_IGNORED;
        csol_weapon_player_role(gPlayer,1);
        if(!Grant(1022,true)||!Check(Knife(1022),"respawn_has_fresh_knife_slot"))return FMRES_IGNORED;
        gStage=17;
    }
    case 17:
    {
        new target=OrdinaryTarget();if(!target)return FMRES_IGNORED;
        if(!OrdinaryHit(target)||!OrdinaryKill(target)||!Grant(1023,true)||!Check(Knife(1023),"ordinary_hit_skull_active"))return FMRES_IGNORED;
        gStage=18;
    }
    case 18:
    {
        new target=OrdinaryTarget();if(!target)return FMRES_IGNORED;
        if(!OrdinaryHit(target)||!OrdinaryKill(target))return FMRES_IGNORED;
        Emit("event=success checks=%d animations=%d completion_messages=%d",gChecks,gAnimations,gCompletions);Stop("completed");
    }
    }
    return FMRES_IGNORED;
}
