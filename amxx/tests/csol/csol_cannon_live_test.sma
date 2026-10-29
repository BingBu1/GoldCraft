// SPDX-License-Identifier: GPL-3.0-or-later
// Temporary local acceptance fixture. Firing always comes from real client input.
// This observes native weapon logic and never rewrites clip, cooldown or weapon ID.
#include <amxmodx>
#include <reapi>
#include <fakemeta>
#include <hamsandwich>
#include <csol_weapons>
#if !defined CANNON_TEST_NATIVE
    #define CANNON_TEST_NATIVE 0
#endif
#if !CANNON_TEST_NATIVE
#include <zp50_core>
#include <zp50_items>
#include <zp50_ammopacks>
#endif

new gEnabled, gPlayer, gTarget, gGlass, gEffectMessage, gCharacterMessage, gShots[33], gLastSeed;
new gTraceCount, gDamageCount, gKills, gDeathMessages, gLastKiller, gLastVictim;
new gLastWeapon[32], gSelected, gChecks, gFailed;
new Float:gBeforeHealth[33], Float:gBeforeArmor[33], Float:gGlassHealth;
new bool:gSavedFunds, gSavedUserId, gSavedAccount;
#if !CANNON_TEST_NATIVE
new gSavedPacks;
new bool:gRejectPurchaseTouch;
new gRejectedTouches;
#endif

public plugin_init()
{
#if CANNON_TEST_NATIVE
    register_plugin("CSOL cannon native fixture", "1.0.1", "GoldCraft contributors");
#else
    register_plugin("CSOL cannon acceptance fixture", "1.0.1", "GoldCraft contributors");
#endif
    gEnabled = create_cvar("csol_cannon_test_enable", "0");
    register_srvcmd("csol_cannon_test", "Command");
    register_srvcmd("csol_cannon_settings", "Settings");
    RegisterHookChain(RG_CBasePlayer_TraceAttack, "TraceAfter", true);
    RegisterHookChain(RG_CBasePlayer_TakeDamage, "DamageBefore", false);
    RegisterHookChain(RG_CBasePlayer_TakeDamage, "DamageAfter", true);
    RegisterHookChain(RG_CBasePlayer_Killed, "KilledAfter", true);
    RegisterHookChain(RG_CSGameRules_RestartRound, "RoundAfter", true);
    // The pinned ReAPI has no CBreakable::TakeDamage chain.
    RegisterHam(Ham_TakeDamage, "func_breakable", "GlassBefore", false);
    RegisterHam(Ham_TakeDamage, "func_breakable", "GlassAfter", true);
    // DispatchTouch invokes the entity virtual Touch directly, so an engine
    // FM_Touch hook would not exercise this internal purchase dispatch.
#if !CANNON_TEST_NATIVE
    RegisterHam(Ham_Touch, "weapon_cannon", "CannonTouchBefore", false);
#endif
    register_message(get_user_msgid("DeathMsg"), "DeathMessage");
    BindEffectMessage();
}
public plugin_cfg() { BindEffectMessage(); }
BindEffectMessage()
{
    if (!gEffectMessage) {
        gEffectMessage = get_user_msgid("CsoCannon");
        if (gEffectMessage) register_message(gEffectMessage, "CannonMessage");
    }
    if (!gCharacterMessage) {
        gCharacterMessage=get_user_msgid("CSOChar");
        if(gCharacterMessage)register_message(gCharacterMessage,"CharacterMessage");
    }
}
bool:Enabled() { return get_pcvar_num(gEnabled) != 0; }
bool:Joined(id)
{
    return is_user_connected(id) && !get_member(id,m_bJustConnected) &&
        get_member(id,m_iJoiningState)==JOINED && !get_member(id,has_disconnected);
}
bool:IsZombie(id)
{
#if CANNON_TEST_NATIVE
    #pragma unused id
    return false;
#else
    return bool:zp_core_is_zombie(id);
#endif
}
Packs(id)
{
#if CANNON_TEST_NATIVE
    #pragma unused id
    return -1;
#else
    return zp_ammopacks_get(id);
#endif
}
bool:LivingHuman(id) { return Joined(id) && is_user_alive(id) && !IsZombie(id); }
public Settings()
{
    new directory[256], plugins[256];
    get_localinfo("amxx_configsdir",directory,charsmax(directory));
    get_localinfo("amxx_plugins",plugins,charsmax(plugins));
    server_print("[CannonTest] settings native=%d configs=^"%s^" plugins=^"%s^"",CANNON_TEST_NATIVE,directory,plugins);
    return PLUGIN_HANDLED;
}
bool:IsCannon(entity)
{
    if (entity <= MaxClients || !is_entity(entity)) return false;
    new name[32];get_entvar(entity,var_classname,name,charsmax(name));
    return equal(name,"weapon_cannon") && get_member(entity,m_iId)==144;
}
Held(id) { return is_user_connected(id) ? rg_find_weapon_bpack_by_name(id,"weapon_cannon") : 0; }
StockAnimationId(const name[])
{
    if(equal(name,"weapon_p90"))return CSW_P90;
    if(equal(name,"weapon_g3sg1"))return CSW_G3SG1;
    if(equal(name,"weapon_m249"))return CSW_M249;
    if(equal(name,"weapon_m4a1"))return CSW_M4A1;
    if(equal(name,"weapon_c4"))return CSW_C4;
    return 0;
}
Check(bool:value,const label[])
{
    ++gChecks;if(!value)++gFailed;
    server_print("[CannonTest] check result=%s label=%s",value?"PASS":"FAIL",label);
}
RestoreFunds()
{
    if(gSavedFunds && is_user_connected(gPlayer) && get_user_userid(gPlayer)==gSavedUserId) {
#if !CANNON_TEST_NATIVE
        zp_ammopacks_set(gPlayer,gSavedPacks);
#endif
        rg_add_account(gPlayer,gSavedAccount,AS_SET,false);
    }
    gSavedFunds=false;
}
public plugin_end() { RestoreFunds(); }
public client_disconnected(id)
{
    gShots[id]=0;
    if(id==gPlayer){gPlayer=0;gSavedFunds=false;}
    if(id==gTarget)gTarget=0;
}
Status()
{
    if(!is_user_connected(gPlayer)){server_print("[CannonTest] status player=0");return;}
    new held=Held(gPlayer),active=get_member(gPlayer,m_pActiveItem),name[32],extension[32];
    if(active>MaxClients && is_entity(active))get_entvar(active,var_classname,name,charsmax(name));
    new clip=-1,reserve=-1;new Float:nextPrimary=-1.0,Float:nextSecondary=-1.0,Float:idle=-1.0;
    if(IsCannon(held)) {
        clip=get_member(held,m_Weapon_iClip);new ammo=get_member(held,m_Weapon_iPrimaryAmmoType);
        if(ammo>=0 && ammo<32)reserve=get_member(gPlayer,m_rgAmmo,ammo);
        nextPrimary=get_member(held,m_Weapon_flNextPrimaryAttack);
        nextSecondary=get_member(held,m_Weapon_flNextSecondaryAttack);idle=get_member(held,m_Weapon_flTimeWeaponIdle);
    }
    server_print("[CannonTest] status player=%d alive=%d zombie=%d active=%d held=%d weapon=%s clip=%d reserve=%d shots=%d nextPrimary=%.5f nextSecondary=%.5f idle=%.5f nextAttack=%.5f",
        gPlayer,is_user_alive(gPlayer),IsZombie(gPlayer),active,held,name,clip,reserve,gShots[gPlayer],nextPrimary,nextSecondary,idle,Float:get_member(gPlayer,m_flNextAttack));
    new signals[2];get_member(gPlayer,m_signals,signals);
    server_print("[CannonTest] nativeState mode=%d team=%d signal=%d state=%d flags=%d takedamage=%.1f",CANNON_TEST_NATIVE,get_member(gPlayer,m_iTeam),signals[0],signals[1],get_entvar(gPlayer,var_flags),Float:get_entvar(gPlayer,var_takedamage));
    if(active>MaxClients && is_entity(active)) {
        new ammo=get_member(active,m_Weapon_iPrimaryAmmoType);
        server_print("[CannonTest] activeWeapon id=%d clip=%d reserve=%d reload=%d",get_member(active,m_iId),get_member(active,m_Weapon_iClip),
            ammo>=0 && ammo<32?get_member(gPlayer,m_rgAmmo,ammo):-1,get_member(active,m_Weapon_fInReload));
    }
    new Float:origin[3],Float:angles[3],Float:view[3],Float:velocity[3];
    get_entvar(gPlayer,var_origin,origin);get_entvar(gPlayer,var_v_angle,angles);get_entvar(gPlayer,var_view_ofs,view);get_entvar(gPlayer,var_velocity,velocity);
    get_member(gPlayer,m_szAnimExtention,extension,charsmax(extension));
    server_print("[CannonTest] pose sequence=%d gait=%d extension=%s selected=%d origin=%.3f,%.3f,%.3f angles=%.3f,%.3f,%.3f eye=%.3f,%.3f,%.3f velocity=%.3f,%.3f,%.3f flags=%d",
        get_entvar(gPlayer,var_sequence),get_entvar(gPlayer,var_gaitsequence),extension,gSelected,origin[0],origin[1],origin[2],angles[0],angles[1],angles[2],origin[0]+view[0],origin[1]+view[1],origin[2]+view[2],velocity[0],velocity[1],velocity[2],get_entvar(gPlayer,var_flags));
    server_print("[CannonTest] counters trace=%d damage=%d kills=%d deathMessages=%d killer=%d victim=%d deathWeapon=%s seed=%d checks=%d failed=%d packs=%d money=%d joining=%d just=%d",
        gTraceCount,gDamageCount,gKills,gDeathMessages,gLastKiller,gLastVictim,gLastWeapon,gLastSeed,gChecks,gFailed,Packs(gPlayer),get_member(gPlayer,m_iAccount),get_member(gPlayer,m_iJoiningState),get_member(gPlayer,m_bJustConnected));
    if(is_user_connected(gTarget)) {
        get_entvar(gTarget,var_origin,origin);
        server_print("[CannonTest] target id=%d alive=%d zombie=%d team=%d health=%.3f armor=%.3f takedamage=%.1f origin=%.3f,%.3f,%.3f",gTarget,is_user_alive(gTarget),IsZombie(gTarget),get_member(gTarget,m_iTeam),Float:get_entvar(gTarget,var_health),Float:get_entvar(gTarget,var_armorvalue),Float:get_entvar(gTarget,var_takedamage),origin[0],origin[1],origin[2]);
    }
}
ListEntities(const classname[])
{
    new ent=-1,count;new Float:origin[3];
    while((ent=engfunc(EngFunc_FindEntityByString,ent,"classname",classname))>0 && count<64) {
        get_entvar(ent,var_origin,origin);new model[80];get_entvar(ent,var_model,model,charsmax(model));
        new clip=IsCannon(ent)?get_member(ent,m_Weapon_iClip):-1;
        server_print("[CannonTest] entity class=%s id=%d owner=%d clip=%d health=%.2f solid=%d model=%s origin=%.2f,%.2f,%.2f",classname,ent,get_entvar(ent,var_owner),clip,Float:get_entvar(ent,var_health),get_entvar(ent,var_solid),model,origin[0],origin[1],origin[2]);
        ++count;
    }
    server_print("[CannonTest] entities class=%s count=%d",classname,count);
}
public Command()
{
    if(!Enabled()){server_print("[CannonTest] disabled; set csol_cannon_test_enable 1");return PLUGIN_HANDLED;}
    new action[24],arg[48];read_argv(1,action,charsmax(action));read_argv(2,arg,charsmax(arg));
    if(equal(action,"prepare")) {
        RestoreFunds();gPlayer=0;gTarget=0;gGlass=0;gChecks=gFailed=0;
        for(new i=1;i<=MaxClients;++i)if(is_user_connected(i)&&!is_user_bot(i)){gPlayer=i;break;}
        Check(gPlayer>0,"human_connected");if(!gPlayer)return PLUGIN_HANDLED;
        Check(Joined(gPlayer),"normal_join_complete");if(!Joined(gPlayer))return PLUGIN_HANDLED;
        if(!is_user_alive(gPlayer))rg_round_respawn(gPlayer);
#if !CANNON_TEST_NATIVE
        if(is_user_alive(gPlayer)&&zp_core_is_zombie(gPlayer))zp_core_force_cure(gPlayer);
#endif
        Check(LivingHuman(gPlayer),"living_human_ready");BindEffectMessage();
        Check(csol_weapon_api_version()==1 && gEffectMessage>0,"native_service_and_message_ready");
    }
    if(!gPlayer || !is_user_connected(gPlayer)){server_print("[CannonTest] prepare_required");return PLUGIN_HANDLED;}
    if(equal(action,"fund")&&LivingHuman(gPlayer)) {
        if(!gSavedFunds){
            gSavedFunds=true;gSavedUserId=get_user_userid(gPlayer);gSavedAccount=get_member(gPlayer,m_iAccount);
#if !CANNON_TEST_NATIVE
            gSavedPacks=zp_ammopacks_get(gPlayer);
#endif
        }
#if CANNON_TEST_NATIVE
        new amount=arg[0]?clamp(str_to_num(arg),0,16000):16000;
        rg_add_account(gPlayer,amount,AS_SET,false);
#else
        new amount=arg[0]?clamp(str_to_num(arg),0,100000):1000;
        zp_ammopacks_set(gPlayer,amount);rg_add_account(gPlayer,16000,AS_SET,false);
#endif
    }
    if(equal(action,"restore"))RestoreFunds();
    if(equal(action,"select")&&LivingHuman(gPlayer))engclient_cmd(gPlayer,"csol_character_select",arg);
#if CANNON_TEST_NATIVE
    if(equal(action,"give")&&LivingHuman(gPlayer)) {
        if(Held(gPlayer)){server_print("[CannonTest] give requires no held cannon");return PLUGIN_HANDLED;}
        new item=rg_give_item(gPlayer,"weapon_cannon",GT_APPEND);
        new bool:valid=IsCannon(item) && get_entvar(item,var_owner)==gPlayer && Held(gPlayer)==item;
        Check(valid,"native_give_inventory_identity");
        if(valid)engclient_cmd(gPlayer,"weapon_cannon");
    }
#else
    if(equal(action,"buy")&&LivingHuman(gPlayer)) {
        new item=zp_items_get_id("Cannon"),before=zp_ammopacks_get(gPlayer),old=Held(gPlayer);
        new result=item>=0?zp_items_force_buy(gPlayer,item):0;
        server_print("[CannonTest] buy item=%d nativeCan=%d result=%d before=%d after=%d old=%d new=%d",item,csol_weapon_can_buy(gPlayer,144),result,before,zp_ammopacks_get(gPlayer),old,Held(gPlayer));
    }
    if(equal(action,"rejectbuy")&&LivingHuman(gPlayer)) {
        new old=Held(gPlayer),active=get_member(gPlayer,m_pActiveItem),before=zp_ammopacks_get(gPlayer);
        if(!IsCannon(old)){server_print("[CannonTest] rejectbuy requires held cannon");return PLUGIN_HANDLED;}
        new clip=get_member(old,m_Weapon_iClip),item=zp_items_get_id("Cannon");
        new Float:primary=get_member(old,m_Weapon_flNextPrimaryAttack),Float:secondary=get_member(old,m_Weapon_flNextSecondaryAttack);
        new Float:idle=get_member(old,m_Weapon_flTimeWeaponIdle),Float:attack=get_member(gPlayer,m_flNextAttack);
        gRejectedTouches=0;gRejectPurchaseTouch=true;
        new result=item>=0?zp_items_force_buy(gPlayer,item):0;
        gRejectPurchaseTouch=false;
        new now=Held(gPlayer);
        server_print("[CannonTest] rejectbuy result=%d rejected=%d before=%d after=%d old=%d new=%d oldValid=%d oldOwner=%d oldClip=%d oldPrimary=%.5f oldSecondary=%.5f oldIdle=%.5f oldActive=%d oldAttack=%.5f",
            result,gRejectedTouches,before,zp_ammopacks_get(gPlayer),old,now,IsCannon(old),is_entity(old)?get_entvar(old,var_owner):0,clip,primary,secondary,idle,active,attack);
        Check(gRejectedTouches==1,"one_purchase_touch_rejected");
        Check(!result,"rejected_purchase_reports_failure");
        Check(before==zp_ammopacks_get(gPlayer),"rejected_purchase_no_charge");
        Check(now==old && IsCannon(old),"rejected_purchase_preserves_entity");
        if(IsCannon(old)) {
            Check(get_member(old,m_Weapon_iClip)==clip,"rejected_purchase_preserves_clip");
            Check(Float:get_member(old,m_Weapon_flNextPrimaryAttack)==primary && Float:get_member(old,m_Weapon_flNextSecondaryAttack)==secondary,"rejected_purchase_preserves_cooldown");
            Check(Float:get_member(old,m_Weapon_flTimeWeaponIdle)==idle,"rejected_purchase_preserves_idle");
        }
        Check(get_member(gPlayer,m_pActiveItem)==active,"rejected_purchase_preserves_active");
        Check(Float:get_member(gPlayer,m_flNextAttack)==attack,"rejected_purchase_preserves_player_attack");
        ListEntities("weapon_cannon");ListEntities("weaponbox");
    }
    if(equal(action,"buyammo")&&LivingHuman(gPlayer))engclient_cmd(gPlayer,"buyammo1");
    if(equal(action,"refill")&&LivingHuman(gPlayer))server_print("[CannonTest] refill added=%d",csol_weapon_refill_ammo(gPlayer));
#endif
    if(equal(action,"switch")&&is_user_alive(gPlayer)) {
        if(equal(arg,"weapon_cannon")||equal(arg,"weapon_knife")||equal(arg,"weapon_usp")||equal(arg,"weapon_deagle")||StockAnimationId(arg))engclient_cmd(gPlayer,arg);
    }
    if(equal(action,"stock")&&LivingHuman(gPlayer)&&StockAnimationId(arg)) {
        new item=rg_give_item(gPlayer,arg,GT_REPLACE);
        new bool:valid=item>MaxClients && is_entity(item) && get_entvar(item,var_owner)==gPlayer && get_member(item,m_iId)==StockAnimationId(arg);
        Check(valid,"stock_animation_inventory_identity");
        if(valid)engclient_cmd(gPlayer,arg);
    }
    if(equal(action,"clientenable")&&is_user_connected(gPlayer)) {
        if(equal(arg,"0")||equal(arg,"1"))client_cmd(gPlayer,"csol_enable %s",arg);
    }
    if(equal(action,"place")&&LivingHuman(gPlayer)&&read_argc()==6) {
        new Float:pos[3],Float:ang[3];new bool:valid=true;
        for(new i=0;i<3;++i){read_argv(i+2,arg,charsmax(arg));pos[i]=str_to_float(arg);if(floatabs(pos[i])>8192.0)valid=false;}
        read_argv(5,arg,charsmax(arg));ang[1]=str_to_float(arg);
        if(valid){engfunc(EngFunc_SetOrigin,gPlayer,pos);set_entvar(gPlayer,var_velocity,Float:{0.0,0.0,0.0});set_entvar(gPlayer,var_v_angle,ang);set_entvar(gPlayer,var_angles,ang);set_entvar(gPlayer,var_fixangle,1);}
    }
    // Uses an existing bot and actual team/role APIs. AI pause is owned by the driver.
    if(equal(action,"target")&&LivingHuman(gPlayer)) {
        new target=str_to_num(arg);
        if(!target)for(new i=1;i<=MaxClients;++i)if(is_user_connected(i)&&is_user_bot(i)&&is_user_alive(i)){target=i;break;}
        if(is_user_connected(target)&&is_user_bot(target)&&Joined(target)) {
            gTarget=target;if(!is_user_alive(target))rg_round_respawn(target);
            new role[16];read_argv(5,role,charsmax(role));
#if CANNON_TEST_NATIVE
            new TeamName:team=get_member(gPlayer,m_iTeam);
            if((team!=TEAM_CT && team!=TEAM_TERRORIST) || (!equal(role,"enemy") && !equal(role,"same"))) {
                server_print("[CannonTest] target requires joined T/CT and enemy/same role");return PLUGIN_HANDLED;
            }
            if(equal(role,"enemy"))team=team==TEAM_CT?TEAM_TERRORIST:TEAM_CT;
            rg_set_user_team(target,team,MODEL_AUTO,true,false);
#else
            if(equal(role,"zombie"))zp_core_force_infect(target);
            else if(equal(role,"human"))zp_core_force_cure(target);
#endif
            read_argv(3,arg,charsmax(arg));new health=clamp(str_to_num(arg),1,100000);
            read_argv(4,arg,charsmax(arg));new armor=clamp(str_to_num(arg),0,1000);
            if(is_user_alive(target)){set_entvar(target,var_health,float(health));rg_set_user_armor(target,armor,armor?ARMOR_VESTHELM:ARMOR_NONE);}
        }
    }
    if(equal(action,"targetplace")&&is_user_alive(gTarget)&&read_argc()==5) {
        new Float:pos[3];new bool:valid=true;
        for(new i=0;i<3;++i){read_argv(i+2,arg,charsmax(arg));pos[i]=str_to_float(arg);if(floatabs(pos[i])>8192.0)valid=false;}
        if(valid){engfunc(EngFunc_SetOrigin,gTarget,pos);set_entvar(gTarget,var_velocity,Float:{0.0,0.0,0.0});}
    }
    if(equal(action,"glass")) {
        new ent=str_to_num(arg),name[32];if(ent>MaxClients && is_entity(ent))get_entvar(ent,var_classname,name,charsmax(name));
        if(equal(name,"func_breakable")){gGlass=ent;server_print("[CannonTest] selectedGlass=%d health=%.3f",ent,Float:get_entvar(ent,var_health));}
        else ListEntities("func_breakable");
    }
    if(equal(action,"entities")){ListEntities("weapon_cannon");ListEntities("weaponbox");}
    if(equal(action,"drop")&&is_user_alive(gPlayer))engclient_cmd(gPlayer,"drop","weapon_cannon");
#if !CANNON_TEST_NATIVE
    if(equal(action,"infect")&&is_user_alive(gPlayer))zp_core_force_infect(gPlayer);
    if(equal(action,"cure")&&is_user_alive(gPlayer))zp_core_force_cure(gPlayer);
#endif
    if(equal(action,"kill")&&is_user_alive(gPlayer))user_kill(gPlayer,1);
    if(equal(action,"respawn")&&Joined(gPlayer))rg_round_respawn(gPlayer);
    if(equal(action,"expectclear"))Check(!Held(gPlayer),"cannon_removed_from_inventory");
    if(equal(action,"resetcounts")){gShots[gPlayer]=gTraceCount=gDamageCount=gKills=gDeathMessages=0;gLastKiller=gLastVictim=gLastSeed=0;gLastWeapon[0]=0;}
    Status();return PLUGIN_HANDLED;
}
#if !CANNON_TEST_NATIVE
public CannonTouchBefore(entity,other)
{
    if(Enabled() && gRejectPurchaseTouch && other==gPlayer && IsCannon(entity)) {
        ++gRejectedTouches;
        server_print("[CannonTest] rejectedTouch entity=%d player=%d clip=%d",entity,other,get_member(entity,m_Weapon_iClip));
        return HAM_SUPERCEDE;
    }
    return HAM_IGNORED;
}
#endif
public CannonMessage(message,destination,recipient)
{
    if(!Enabled()||get_msg_args()!=13)return;
    new owner=get_msg_arg_int(2);if(owner<1||owner>MaxClients)return;
    ++gShots[owner];gLastSeed=get_msg_arg_int(13);
    if(owner==gPlayer)server_print("[CannonTest] fired count=%d version=%d owner=%d duck=%d seed=%d time=%.5f",gShots[owner],get_msg_arg_int(1),owner,get_msg_arg_int(3),gLastSeed,get_gametime());
}
public CharacterMessage(message,destination,recipient)
{
    if(Enabled()&&get_msg_args()==10&&get_msg_arg_int(2)==gPlayer)
        gSelected=get_msg_arg_int(5)|(get_msg_arg_int(6)<<8);
}
public TraceAfter(victim,attacker,Float:damage,Float:direction[3],trace,bits)
{
    if(!Enabled()||attacker!=gPlayer||!is_user_connected(attacker)||!IsCannon(get_member(attacker,m_pActiveItem)))return;
    ++gTraceCount;
    server_print("[CannonTest] trace victim=%d attacker=%d damage=%.5f bits=%d hit=%d group=%d original=%d direction=%.4f,%.4f,%.4f",victim,attacker,damage,bits,get_tr2(trace,TR_pHit),get_tr2(trace,TR_iHitgroup),IsReapiHookOriginalWasCalled(RG_CBasePlayer_TraceAttack),direction[0],direction[1],direction[2]);
}
public DamageBefore(victim,inflictor,attacker,Float:damage,bits)
{
    if(!Enabled()||attacker!=gPlayer||!is_user_connected(attacker)||!IsCannon(get_member(attacker,m_pActiveItem)))return;
    gBeforeHealth[victim]=get_entvar(victim,var_health);gBeforeArmor[victim]=get_entvar(victim,var_armorvalue);
    server_print("[CannonTest] damagePre victim=%d inflictor=%d attacker=%d amount=%.5f bits=%d health=%.5f armor=%.5f",victim,inflictor,attacker,damage,bits,gBeforeHealth[victim],gBeforeArmor[victim]);
}
public DamageAfter(victim,inflictor,attacker,Float:damage,bits)
{
    if(!Enabled()||attacker!=gPlayer||!is_user_connected(attacker)||!IsCannon(get_member(attacker,m_pActiveItem)))return;
    ++gDamageCount;
    server_print("[CannonTest] damagePost victim=%d inflictor=%d attacker=%d amount=%.5f bits=%d healthBefore=%.5f healthAfter=%.5f armorBefore=%.5f armorAfter=%.5f original=%d alive=%d",victim,inflictor,attacker,damage,bits,gBeforeHealth[victim],Float:get_entvar(victim,var_health),gBeforeArmor[victim],Float:get_entvar(victim,var_armorvalue),IsReapiHookOriginalWasCalled(RG_CBasePlayer_TakeDamage),is_user_alive(victim));
}
public KilledAfter(victim,attacker,gib)
{
    if(Enabled()&&attacker==gPlayer){++gKills;server_print("[CannonTest] killed victim=%d attacker=%d gib=%d",victim,attacker,gib);}
}
public DeathMessage(message,destination,recipient)
{
    if(!Enabled()||get_msg_args()<4||get_msg_arg_int(1)!=gPlayer)return;
    ++gDeathMessages;gLastKiller=get_msg_arg_int(1);gLastVictim=get_msg_arg_int(2);
    get_msg_arg_string(4,gLastWeapon,charsmax(gLastWeapon));
    server_print("[CannonTest] deathmsg killer=%d victim=%d head=%d weapon=%s",gLastKiller,gLastVictim,get_msg_arg_int(3),gLastWeapon);
}
public GlassBefore(entity,inflictor,attacker,Float:damage,bits)
{
    if(!Enabled()||entity!=gGlass||attacker!=gPlayer)return HAM_IGNORED;
    gGlassHealth=get_entvar(entity,var_health);
    server_print("[CannonTest] glassPre entity=%d inflictor=%d attacker=%d damage=%.5f bits=%d health=%.5f",entity,inflictor,attacker,damage,bits,gGlassHealth);
    return HAM_IGNORED;
}
public GlassAfter(entity,inflictor,attacker,Float:damage,bits)
{
    if(!Enabled()||entity!=gGlass||attacker!=gPlayer)return HAM_IGNORED;
    if(!is_entity(entity)){server_print("[CannonTest] glassPost entity=%d valid=0 before=%.5f",entity,gGlassHealth);return HAM_IGNORED;}
    server_print("[CannonTest] glassPost entity=%d valid=%d before=%.5f after=%.5f solid=%d effects=%d",entity,is_entity(entity),gGlassHealth,Float:get_entvar(entity,var_health),get_entvar(entity,var_solid),get_entvar(entity,var_effects));
    return HAM_IGNORED;
}
public RoundAfter() { if(Enabled()&&is_user_connected(gPlayer)){server_print("[CannonTest] roundRestarted");Status();} }
#if !CANNON_TEST_NATIVE
public zp_fw_core_spawn_post(id) { if(Enabled()&&id==gPlayer)server_print("[CannonTest] spawned held=%d zombie=%d",Held(id),zp_core_is_zombie(id)); }
public zp_fw_core_infect_post(id,attacker) { if(Enabled()&&id==gPlayer)server_print("[CannonTest] infected held=%d",Held(id)); }
public zp_fw_core_cure_post(id,attacker) { if(Enabled()&&id==gPlayer)server_print("[CannonTest] cured held=%d",Held(id)); }
#endif
