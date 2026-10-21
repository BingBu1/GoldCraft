// Local isolated engine fixture; not a weapon/gameplay implementation.
#include <amxmodx>
#include <reapi>
#include <fakemeta>

new const Models[][] = {
    "models/v_revivegun.mdl", "models/v_soulreaperex.mdl", "models/v_gungnir.mdl",
    "models/v_leapstrikegunex.mdl", "models/v_leapstrikegunex_parts.mdl", "models/v_ak47.mdl",
    "models/v_balrog1b.mdl", "models/v_sfboomerang1.mdl", "models/v_alterationammogunex.mdl",
    "models/v_burstdischarge1.mdl", "models/v_bpythonzf.mdl"
};
new gIndices[sizeof Models], gSelected[33], gPartMessage, gViewMessage;
public plugin_precache()
{
    for(new i; i<sizeof Models; i++) gIndices[i]=precache_model(Models[i]);
    precache_model("sprites/ef_revivegun_idle1.spr");
}
public plugin_init()
{
    register_plugin("CSOL runtime fixture", "1.1", "GoldCraft contributors");
    register_clcmd("csol_test", "SelectModel");
    register_clcmd("csol_anim", "Animate");
    register_clcmd("csol_part", "PartState");
    register_clcmd("csol_skin", "SkinState");
    register_srvcmd("csol_fixture", "FixtureCommand");
    register_forward(FM_PlayerPreThink, "PlayerThink");
    gPartMessage=engfunc(EngFunc_RegUserMsg,"CsolPart",-1);
    gViewMessage=engfunc(EngFunc_RegUserMsg,"CsolView",11);
    server_print("[CSOL test] precache models=%d sprite=v3 loaded",sizeof Models);
}
public client_putinserver(id) { gSelected[id]=-1; }
public FixtureCommand()
{
    new command[128]; read_args(command, charsmax(command));trim(command);
    if(contain(command,";")>=0 || contain(command,"^n")>=0)return PLUGIN_HANDLED;
    new action[32], first[16], second[16];
    read_argv(1,action,charsmax(action));read_argv(2,first,charsmax(first));read_argv(3,second,charsmax(second));
    // Execute server-owned operations directly. Stufftext may reject an unknown
    // client command before it reaches AMXX's register_clcmd handler.
    if(equal(action,"csol_test") || equal(action,"csol_anim") || equal(action,"csol_part") || equal(action,"csol_skin"))
    {
        new players[32], count;get_players(players,count,"ch");
        for(new i;i<count;i++)
        {
            new id=players[i];
            if(equal(action,"csol_test"))SetModel(id,str_to_num(first));
            else if(equal(action,"csol_anim"))SendAnimation(id,str_to_num(first));
            else if(equal(action,"csol_skin"))SendSkin(id,str_to_num(first),str_to_num(second));
            else SendPart(id,str_to_num(first),str_to_num(second));
        }
        server_print("[CSOL test] direct %s targets=%d",command,count);
        return PLUGIN_HANDLED;
    }
    if(equal(command,"cl_righthand ",13)
        || equal(command,"csol_enable ",12) || equal(command,"csol_info")
        || equal(command,"snapshot") || equal(command,"jointeam 2") || equal(command,"joinclass 1"))
        client_cmd(0,"%s",command);
    return PLUGIN_HANDLED;
}
public PlayerThink(id)
{
    if(!is_user_alive(id) || gSelected[id]<0)return FMRES_IGNORED;
    set_pev(id,pev_viewmodel2,Models[gSelected[id]]);
    set_member(id,m_flNextAttack,9999.0);
    return FMRES_IGNORED;
}
public SelectModel(id)
{
    new arg[16];read_argv(1,arg,charsmax(arg));new selected=str_to_num(arg);
    return SetModel(id,selected);
}
SetModel(id,selected)
{
    if(selected<0 || selected>=sizeof Models)return PLUGIN_HANDLED;
    if(!is_user_alive(id)) { rg_set_user_team(id,TEAM_CT);rg_round_respawn(id); }
    rg_give_item(id,"weapon_ak47",GT_REPLACE);engclient_cmd(id,"weapon_ak47");
    gSelected[id]=selected;PlayerThink(id);
    SendSkin(id,0);
    message_begin(MSG_ONE,SVC_WEAPONANIM,_,id);write_byte(0);write_byte(0);message_end();
    client_print(id,print_console,"[CSOL test] model=%s precache=%d",Models[selected],gIndices[selected]);
    return PLUGIN_HANDLED;
}
public Animate(id)
{
    new arg[16];read_argv(1,arg,charsmax(arg));new sequence=str_to_num(arg);
    return SendAnimation(id,sequence);
}
SendAnimation(id,sequence)
{
    if(!is_user_alive(id))return PLUGIN_HANDLED;
    if(sequence<0 || sequence>255)return PLUGIN_HANDLED;
    message_begin(MSG_ONE,SVC_WEAPONANIM,_,id);write_byte(sequence);write_byte(0);message_end();
    client_print(id,print_console,"[CSOL test] sequence=%d",sequence);return PLUGIN_HANDLED;
}
public PartState(id)
{
    new arg[16];read_argv(1,arg,charsmax(arg));new slot=str_to_num(arg);read_argv(2,arg,charsmax(arg));new sequence=str_to_num(arg);
    return SendPart(id,slot,sequence);
}
SendPart(id,slot,sequence)
{
    if(!is_user_alive(id) || gSelected[id]<0 || slot<0 || slot>1 || sequence<0 || sequence>255)return PLUGIN_HANDLED;
    // Fakemeta registers with the engine directly, outside Metamod's named
    // message cache. Use the matching engine begin for this private message.
    engfunc(EngFunc_MessageBegin,MSG_ONE,gPartMessage,Float:{0.0,0.0,0.0},id);
    write_byte(2);write_byte(slot);write_long(gIndices[gSelected[id]]);write_short(sequence);
    write_short(0);write_short(0);write_short(30);write_short(30);write_short(0);message_end();
    server_print("[CSOL test] CsolPart player=%d msg=%d model=%d slot=%d sequence=%d",id,gPartMessage,gIndices[gSelected[id]],slot,sequence);
    return PLUGIN_HANDLED;
}
public SkinState(id)
{
    new arg[16],body[16];read_argv(1,arg,charsmax(arg));read_argv(2,body,charsmax(body));return SendSkin(id,str_to_num(arg),str_to_num(body));
}
SendSkin(id,skin,body=0)
{
    if(!is_user_alive(id) || gSelected[id]<0 || skin<0 || skin>65535 || body<0)return PLUGIN_HANDLED;
    engfunc(EngFunc_MessageBegin,MSG_ONE,gViewMessage,Float:{0.0,0.0,0.0},id);
    write_byte(1);write_long(gIndices[gSelected[id]]);write_short(skin);write_long(body);message_end();
    server_print("[CSOL test] CsolView player=%d model=%d skin=%d body=%d",id,gIndices[gSelected[id]],skin,body);
    return PLUGIN_HANDLED;
}
