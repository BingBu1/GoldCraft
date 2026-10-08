// ZP 5.0.8a forwards and SyPB 1.50 API from the pinned source trees.
#include <amxmodx>
#include <fakemeta>
#include <reapi>
#include <zp50_core>
#include <zp50_gamemodes>
#include <sypb>

new bool:gReady;
new gMaxPlayers, gInfections, gCures, gNaturalInfections, gBotInfections, gDamageEvents;
new Float:gBeforeHealth[33];
new bool:gInDamage[33], gDamageAttacker[33], gDamageInflictor[33], gDamageBits[33];
new Float:gDamageAmount[33];
new gBotKnifeInfections, gLastVictim, gLastAttacker, gLastMode;
new Float:gLastInfectionTime;

public plugin_init()
{
    register_plugin("GoldCraft ZP SyPB integration", "0.1.0", "GoldCraft contributors");
    gMaxPlayers=get_maxplayers();
    register_event("HLTV", "OnNewRound", "a", "1=0", "2=0");
    register_srvcmd("gc_zp_status", "Status");
    register_srvcmd("gc_zp_locale", "LocaleStatus");
    RegisterHookChain(RG_CBasePlayer_TakeDamage, "BeforeDamage", false);
    RegisterHookChain(RG_CBasePlayer_TakeDamage, "AfterDamage", true);
}
public plugin_cfg()
{
    if(!is_run_sypb() || sypb_api_version()<1.50){
        set_fail_state("Matching SyPB 1.50 bot + AMXX API are required");
        return;
    }
    // MODE_ZP = 2 in the pinned SyPB version.h. ZP remains the authority.
    set_cvar_num("sypb_gamemod",2);
    // ReGameDLL PlayerThink skips the menu when auto-join chooses a human team,
    // then explicitly excludes fakeclients from that join. SyPB needs the menu.
    set_cvar_num("mp_auto_join_team",0);
    set_cvar_string("humans_join_team","any");
    gReady=true;
    set_task(0.5,"Reconcile",70150,_,_,"b");
}
stock SyncPlayer(id)
{
    if(gReady && id>=1 && id<=gMaxPlayers && is_user_connected(id))
        sypb_set_zombie_player(id,zp_core_is_zombie(id)?1:0);
}
public Reconcile()
{
    // SyPB initializes its client table after ClientPutInServer. Reconcile late
    // joins and slot reuse as well as forwarding each change immediately.
    for(new id=1;id<=gMaxPlayers;id++)SyncPlayer(id);
}
public zp_fw_core_spawn_post(id){SyncPlayer(id);}
public zp_fw_core_infect_post(id,attacker)
{
    SyncPlayer(id);gInfections++;
    if(attacker>0 && attacker!=id && is_user_connected(attacker)){
        gNaturalInfections++;
        if(is_user_bot(attacker)){
            gBotInfections++;
            // ReGameDLL knife applies TraceAttack / ApplyMultiDamage with
            // the player as attacker AND inflictor, DMG_BULLET | DMG_NEVERGIB.
            // Count only an infection inside that actual TakeDamage chain.
            if(gInDamage[id] && gDamageAttacker[id]==attacker && gDamageInflictor[id]==attacker
               && get_user_weapon(attacker)==CSW_KNIFE && (gDamageBits[id]&DMG_BULLET)
               && gDamageAmount[id]>0.0){
                gBotKnifeInfections++;
                gLastVictim=get_user_userid(id);gLastAttacker=get_user_userid(attacker);
                gLastMode=zp_gamemodes_get_current();gLastInfectionTime=get_gametime();
            }
        }
    }
}
public zp_fw_core_cure_post(id,attacker){SyncPlayer(id);gCures++;}
public OnNewRound(){if(gReady)sypb_zombie_game_start(0);}
public zp_fw_gamemodes_start(mode){Reconcile();if(gReady)sypb_zombie_game_start(1);}
public zp_fw_gamemodes_end(mode){if(gReady)sypb_zombie_game_start(0);}
public BeforeDamage(id,inflictor,attacker,Float:amount,bits)
{
    gBeforeHealth[id]=Float:get_entvar(id,var_health);
    gInDamage[id]=true;gDamageAttacker[id]=attacker;gDamageInflictor[id]=inflictor;
    gDamageBits[id]=bits;gDamageAmount[id]=amount;
    return HC_CONTINUE;
}
public AfterDamage(id,inflictor,attacker,Float:amount,bits)
{
    gInDamage[id]=false;
    if(attacker>=1 && attacker<=gMaxPlayers && attacker!=id && is_user_connected(attacker)
       && is_user_bot(attacker) && Float:get_entvar(id,var_health)<gBeforeHealth[id])gDamageEvents++;
    return HC_CONTINUE;
}
public Status()
{
    new file=fopen("addons/amxmodx/logs/goldcraft-zombie.json","wt");
    if(!file)return PLUGIN_HANDLED;
    fprintf(file,"{^"time^":%.3f,^"api^":%.2f,^"mode^":%d,^"started^":%d,^"infections^":%d,^"cures^":%d,^"naturalInfections^":%d,^"botInfections^":%d,^"botDamageEvents^":%d",get_gametime(),sypb_api_version(),get_cvar_num("sypb_gamemod"),sypb_zombie_game_start(),gInfections,gCures,gNaturalInfections,gBotInfections,gDamageEvents);
    // 'mode' above is SyPB's MODE_ZP, not ZP's current round mode.
    // Keep both so a non-infecting special round cannot look like a failed
    // standard-infection test merely because the Bot mode remains 2.
    fprintf(file,",^"gameMode^":%d,^"allowInfection^":%d,^"botKnifeInfections^":%d,^"lastKnifeInfection^":{^"time^":%.3f,^"victimUserId^":%d,^"attackerUserId^":%d,^"gameMode^":%d},^"players^":[",zp_gamemodes_get_current(),zp_gamemodes_get_allow_infect(),gBotKnifeInfections,gLastInfectionTime,gLastVictim,gLastAttacker,gLastMode);
    new count,Float:origin[3],Float:velocity[3];
    for(new id=1;id<=gMaxPlayers;id++){
        if(!is_user_connected(id))continue;
        pev(id,pev_origin,origin);
        pev(id,pev_velocity,velocity);
        new bot=is_user_sypb(id)==1;
        fprintf(file,"%s{^"slot^":%d,^"userid^":%d,^"bot^":%d,^"alive^":%d,^"team^":%d,^"zombie^":%d,^"sypbZombie^":%d,^"health^":%.3f,^"enemy^":%d,^"moveTarget^":%d,^"weapon^":%d,^"spawns^":%d",count++?",":"",id,get_user_userid(id),bot,is_user_alive(id),get_user_team(id),zp_core_is_zombie(id),sypb_is_zombie_player(id),Float:get_entvar(id,var_health),bot?sypb_get_enemy(id):-1,bot?sypb_get_movetarget(id):-1,get_user_weapon(id),get_member(id,m_iNumSpawns));
        fprintf(file,",^"armor^":%.3f,^"joining^":%d,^"menu^":%d,^"flags^":%d,^"origin^":[%.3f,%.3f,%.3f],^"velocity^":[%.3f,%.3f,%.3f]}",Float:get_entvar(id,var_armorvalue),get_member(id,m_iJoiningState),get_member(id,m_iMenu),pev(id,pev_flags),origin[0],origin[1],origin[2],velocity[0],velocity[1],velocity[2]);
    }
    fprintf(file,"]}^n");fclose(file);
    server_print("[GoldCraft ZP] SyPB API=%.2f mode=%d players=%d naturalInfections=%d botDamage=%d",sypb_api_version(),get_cvar_num("sypb_gamemod"),count,gNaturalInfections,gDamageEvents);
    return PLUGIN_HANDLED;
}

// Query AMXX's loaded dictionaries, rather than inferring language from a cfg file.
public LocaleStatus()
{
    new language[8], clients[32], count;
    get_cvar_string("amx_language",language,charsmax(language));
    get_players(clients,count,"ch");
    new player=count?clients[0]:LANG_SERVER;
    server_print("[GoldCraft ZP] language=%s clientLanguages=%d player=%d",language,get_cvar_num("amx_client_languages"),player);
    server_print("[GoldCraft ZP] server-buy=%L | player-buy=%L | cn-buy=%L",LANG_SERVER,"MENU_BUY1_TITLE",player,"MENU_BUY1_TITLE","cn","MENU_BUY1_TITLE");
    server_print("[GoldCraft ZP] zombie=%L | weapon=%L",player,"GC_ZOMBIENAME_Classic_Zombie",player,"GC_WEAPONNAME_weapon_m4a1");
    server_print("[GoldCraft ZP] human=%L | item=%L | mode=%L | title=%L",player,"GC_HUMANNAME_Classic_Human",player,"GC_ITEMNAME_Infection_Bomb",player,"GC_MODENAME_Infection_Mode",player,"GC_ZP_TITLE");
    return PLUGIN_HANDLED;
}
