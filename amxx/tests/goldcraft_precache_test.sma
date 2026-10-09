// Opt-in fixture, never enabled by normal server installation. Sources/API:
// AMXX1.9.0.5303, ReAPI5.29.0.358 and the paired GoldCraft precache-v3 engine.
#include <amxmodx>
#include <goldcraft_amxx>
#include <fakemeta>
#include <reapi>

new gModel, gSprite, gSoundA, gSoundB, gLowSprite;
new gModelCount, gSoundCount, gGenericCount, gProbes;
new gAmxxChecks, gReapiChecks, gFailures, gMode, bool:gSending;
new gEntities[2];
new gBrass, gLowModel, gBrassHigh, gAmxxBrassChecks, gReapiBrassChecks;
new gShadowExpected, gShadowObserved, gShadowMessages;
new const gHighModel[] = "models/gc_probe/high.mdl";
new const gHighSprite[] = "sprites/gc_probe/high.spr";
new const gHighSoundA[] = "gc_probe/high_a.wav";
new const gHighSoundB[] = "gc_probe/high_b.wav";

public plugin_precache()
{
    if(get_cvar_num("gc_precache_protocol")!=3){set_fail_state("GoldCraft precache v3 engine required");return;}
    gLowModel=precache_model("models/rshell.mdl");
    new path[64];
    for(new i=0;i<1150;i++){
        formatex(path,charsmax(path),"sprites/gc_probe/m%04d.spr",i);
        gLowSprite=precache_model(path);gModelCount++;
        formatex(path,charsmax(path),"gc_probe/s%04d.wav",i);
        precache_sound(path);gSoundCount++;
    }
    for(new i=0;i<2150;i++){
        formatex(path,charsmax(path),"gc_probe/g%04d.txt",i);
        precache_generic(path);gGenericCount++;
    }
    server_cmd("gc_precache_fixture_pad");server_exec();
    gModel=precache_model(gHighModel);gSprite=precache_model(gHighSprite);
    gSoundA=precache_sound(gHighSoundA);gSoundB=precache_sound(gHighSoundB);
    if(gModel!=65535 || gSprite!=65536 || gSoundA!=65535 || gSoundB!=65536){
        set_fail_state("Launch with -goldcraft_precache_test; fixture indices must cross65535");return;
    }
    force_unmodified(force_exactfile,{0,0,0},{0,0,0},gHighModel);
    force_unmodified(force_exactfile,{0,0,0},{0,0,0},gHighSprite);
}

public plugin_init()
{
    register_plugin("GoldCraft precache boundary fixture","3.0","GoldCraft contributors");
    register_srvcmd("gc_precache_probe","Probe");
    register_srvcmd("gc_precache_probe_status","Status");
    register_srvcmd("gc_precache_probe_clear","ClearProbe");
    register_message(SVC_TEMPENTITY,"AmxxMessage");
    RegisterMessage(SVC_TEMPENTITY,"EngineMessage");
    gBrass=get_user_msgid("Brass");
    if(!gBrass){set_fail_state("Matching CS Brass registration required");return;}
    register_message(gBrass,"AmxxBrass");
    RegisterMessage(gBrass,"EngineBrass");
    gShadowExpected=engfunc(EngFunc_ModelIndex,"sprites/shadow_circle.spr");
    register_message(get_user_msgid("ShadowIdx"),"ShadowIndex");
}

public ShadowIndex(type,dest,id)
{
    gShadowObserved=get_msg_arg_int(1);gShadowMessages++;
    Check(gShadowObserved==gShadowExpected);
    return PLUGIN_CONTINUE;
}

stock Check(bool:ok){if(!ok)gFailures++;}

public ClearProbe()
{
    remove_task(9301);
    for(new i=0;i<sizeof gEntities;i++){
        if(pev_valid(gEntities[i])){
            new classname[32];get_entvar(gEntities[i], var_classname, classname, charsmax(classname));
            if(equal(classname,"gc_precache_probe"))engfunc(EngFunc_RemoveEntity,gEntities[i]);
        }
        gEntities[i]=0;
    }
    Status();return PLUGIN_HANDLED;
}

public AmxxMessage(type,dest,id)
{
    if(!gSending)return PLUGIN_CONTINUE;
    if(get_msg_arg_int(1)==TE_SPRITE){
        Check(get_msg_arg_int(5)==((gMode==1 || gMode==4)?gLowSprite:gSprite));
        if(gMode==4){set_msg_arg_int(5,ARG_SHORT,gSprite);Check(get_msg_arg_int(5)==gSprite);}
        else if(gMode==5){set_msg_arg_int(5,ARG_SHORT,gLowSprite);Check(get_msg_arg_int(5)==gLowSprite);}
        Check(get_msg_arg_int(6)==8 && get_msg_arg_int(7)==255);gAmxxChecks++;
    }
    return PLUGIN_CONTINUE;
}

public EngineMessage(type,dest,id)
{
    if(!gSending || GetMessageData(MsgArg,1)!=TE_SPRITE)return HC_CONTINUE;
    new original=GetMessageData(MsgArg,5);
    Check(original==((gMode==1 || gMode==5)?gLowSprite:gSprite));
    if(gMode==1){
        SetMessageData(MsgArg,5,gSprite);
        Check(GetMessageData(MsgArg,5)==gSprite && GetMessageOrigData(MsgArg,5)==gLowSprite);
    }else if(gMode==2){
        SetMessageData(MsgArg,5,gLowSprite);
        Check(GetMessageData(MsgArg,5)==gLowSprite && GetMessageOrigData(MsgArg,5)==gSprite);
    }else if(gMode==3){
        SetMessageData(MsgArg,5,gLowSprite);
        ResetModifiedMessageData(MsgArg,5);
        Check(GetMessageData(MsgArg,5)==gSprite && !IsMessageDataModified(MsgArg,5));
    }
    Check(GetMessageData(MsgArg,6)==8 && GetMessageData(MsgArg,7)==255);
    gReapiChecks++;
    return HC_CONTINUE;
}

stock SpriteMessage(id,const Float:pos[3],mode,bool:withAmxxHooks=false)
{
    gMode=mode;gSending=true;
    if(withAmxxHooks){
        // Matching AMXX messages.inc: only emessage/ewrite traverse AMXX's
        // own register_message hooks. Ordinary message_* intentionally bypasses them.
        emessage_begin(MSG_ONE,SVC_TEMPENTITY,_,id);
        ewrite_byte(TE_SPRITE);ewrite_coord(floatround(pos[0]));ewrite_coord(floatround(pos[1]));ewrite_coord(floatround(pos[2]));
        ewrite_short((mode==1 || mode==4)?gLowSprite:gSprite);ewrite_byte(8);ewrite_byte(255);emessage_end();
    }else{
        message_begin(MSG_ONE,SVC_TEMPENTITY,_,id);
        write_byte(TE_SPRITE);write_coord(floatround(pos[0]));write_coord(floatround(pos[1]));write_coord(floatround(pos[2]));
        write_short(mode==1?gLowSprite:gSprite);write_byte(8);write_byte(255);message_end();
    }
    gSending=false;
}

public AmxxBrass(type,dest,id)
{
    if(!gSending)return PLUGIN_CONTINUE;
    Check(get_msg_arg_int(12)==((gMode==1 || gMode==4)?gLowModel:gBrassHigh));
    if(gMode==4){set_msg_arg_int(12,ARG_SHORT,gBrassHigh);Check(get_msg_arg_int(12)==gBrassHigh);}
    else if(gMode==5){set_msg_arg_int(12,ARG_SHORT,gLowModel);Check(get_msg_arg_int(12)==gLowModel);}
    Check(get_msg_arg_int(13)==0 && get_msg_arg_int(14)==25 && get_msg_arg_int(15)==id);
    gAmxxBrassChecks++;return PLUGIN_CONTINUE;
}

public EngineBrass(type,dest,id)
{
    if(!gSending)return HC_CONTINUE;
    Check(GetMessageData(MsgArg,12)==((gMode==1 || gMode==5)?gLowModel:gBrassHigh));
    if(gMode==1){
        SetMessageData(MsgArg,12,gBrassHigh);
        Check(GetMessageData(MsgArg,12)==gBrassHigh && GetMessageOrigData(MsgArg,12)==gLowModel);
    }else if(gMode==2){
        SetMessageData(MsgArg,12,gLowModel);
        Check(GetMessageData(MsgArg,12)==gLowModel && GetMessageOrigData(MsgArg,12)==gBrassHigh);
    }else if(gMode==3){
        SetMessageData(MsgArg,12,gLowModel);ResetModifiedMessageData(MsgArg,12);
        Check(GetMessageData(MsgArg,12)==gBrassHigh && !IsMessageDataModified(MsgArg,12));
    }
    Check(GetMessageData(MsgArg,13)==0 && GetMessageData(MsgArg,14)==25 && GetMessageData(MsgArg,15)==id);
    gReapiBrassChecks++;return HC_CONTINUE;
}

stock BrassMessage(id,const Float:pos[3],mode,high,bool:withAmxxHooks=false)
{
    gMode=mode;gBrassHigh=high;gSending=true;
    // Exact ReGameDLL EjectBrass CS schema: subtype, origin, left, velocity,
    // rotation, model, bounce sound type, lifetime, player. Only model expands.
    if(withAmxxHooks){
        emessage_begin(MSG_ONE,gBrass,_,id);ewrite_byte(TE_MODEL);
        for(new i=0;i<3;i++)ewrite_coord(floatround(pos[i]));
        for(new i=0;i<6;i++)ewrite_coord(0);
        ewrite_angle(0);ewrite_short((mode==1 || mode==4)?gLowModel:high);
        ewrite_byte(0);ewrite_byte(25);ewrite_byte(id);emessage_end();
    }else{
        message_begin(MSG_ONE,gBrass,_,id);write_byte(TE_MODEL);
        for(new i=0;i<3;i++)write_coord(floatround(pos[i]));
        for(new i=0;i<6;i++)write_coord(0);
        write_angle(0);write_short(mode==1?gLowModel:high);
        write_byte(0);write_byte(25);write_byte(id);message_end();
    }
    gSending=false;
}

public Probe()
{
    new id;for(new i=1;i<=MaxClients;i++)if(is_user_connected(i)&&!is_user_bot(i)&&!is_user_hltv(i)){id=i;break;}
    if(!id){server_print("GoldCraft precache probe needs the single B client");return PLUGIN_HANDLED;}
    ClearProbe();
    // Automatic cleanup also runs when the external observer exits or fails.
    set_task_ex(10.0, "ClearProbe", 9301);
    new Float:pos[3],Float:angles[3],Float:direction[3],Float:view[3];
    get_entvar(id, var_origin, pos);get_entvar(id, var_view_ofs, view);get_entvar(id, var_v_angle, angles);
    engfunc(EngFunc_MakeVectors,angles);global_get(glb_v_forward,direction);
    for(new i=0;i<3;i++)pos[i]+=view[i]+direction[i]*96.0;
    for(new i=0;i<2;i++){
        if(pev_valid(gEntities[i]))engfunc(EngFunc_RemoveEntity,gEntities[i]);
        new ent=rg_create_entity("info_target");gEntities[i]=ent;
        set_entvar(ent, var_classname, "gc_precache_probe");set_entvar(ent, var_movetype, MOVETYPE_NONE);set_entvar(ent, var_solid, SOLID_NOT);
        engfunc(EngFunc_SetModel,ent,i?gHighSprite:gHighModel);
        new Float:p[3];for(new j=0;j<3;j++)p[j]=pos[j];p[2]+=float(i*16);
        engfunc(EngFunc_SetOrigin,ent,p);set_entvar(ent, var_renderamt, 255.0);
    }
    new amxxBefore=gAmxxChecks,reapiBefore=gReapiChecks;
    for(new mode=0;mode<4;mode++)SpriteMessage(id,pos,mode);
    for(new mode=0;mode<6;mode++)SpriteMessage(id,pos,mode,true);
    Check(gAmxxChecks-amxxBefore==6 && gReapiChecks-reapiBefore==10);
    amxxBefore=gAmxxBrassChecks;reapiBefore=gReapiBrassChecks;
    for(new mode=0;mode<4;mode++)BrassMessage(id,pos,mode,gModel);
    for(new mode=0;mode<6;mode++)BrassMessage(id,pos,mode,gSprite,true);
    Check(gAmxxBrassChecks-amxxBefore==6 && gReapiBrassChecks-reapiBefore==10);
    // Two consecutive expanded model fields test positional bookkeeping.
    message_begin(MSG_ONE,SVC_TEMPENTITY,_,id);write_byte(TE_BLOODSPRITE);
    for(new i=0;i<3;i++)write_coord(floatround(pos[i]));
    write_short(gSprite);write_short(gSprite);write_byte(70);write_byte(8);message_end();
    message_begin(MSG_ONE,SVC_TEMPENTITY,_,id);write_byte(TE_MODEL);
    for(new i=0;i<3;i++)write_coord(floatround(pos[i]));
    write_coord(0);write_coord(0);write_coord(0);write_angle(0);
    write_short(gModel);write_byte(0);write_byte(100);message_end();
    message_begin(MSG_ONE,SVC_TEMPENTITY,_,id);write_byte(TE_BEAMPOINTS);
    for(new i=0;i<3;i++)write_coord(floatround(pos[i]));
    write_coord(floatround(pos[0]));write_coord(floatround(pos[1]));write_coord(floatround(pos[2]+48.0));
    write_short(gSprite);write_byte(0);write_byte(0);write_byte(30);write_byte(10);write_byte(0);
    write_byte(255);write_byte(120);write_byte(50);write_byte(255);write_byte(0);message_end();
    emit_sound(id,CHAN_ITEM,gHighSoundA,1.0,ATTN_NORM,0,PITCH_NORM);
    engfunc(EngFunc_EmitAmbientSound,id,pos,gHighSoundB,1.0,ATTN_NORM,0,PITCH_NORM);
    gProbes++;Status();
    server_print("GoldCraft precache probe: model=%d sprite=%d sounds=%d/%d failures=%d",gModel,gSprite,gSoundA,gSoundB,gFailures);
    return PLUGIN_HANDLED;
}

public Status()
{
    // Query actual live edicts, not only our cached IDs after cleanup.
    new active,ent;
    while((ent=rg_find_ent_by_class(ent,"gc_precache_probe"))>0)active++;
    new file=fopen("addons/amxmodx/logs/goldcraft-precache.json","wt");if(!file)return PLUGIN_HANDLED;
    fprintf(file,"{^"models^":%d,^"sounds^":%d,^"generic^":%d,^"highModel^":%d,^"highSprite^":%d,^"highSounds^":[%d,%d],^"probes^":%d,^"amxxChecks^":%d,^"reapiChecks^":%d,^"amxxBrassChecks^":%d,^"reapiBrassChecks^":%d,^"failures^":%d,^"entities^":[%d,%d],^"shadowExpected^":%d,^"shadowObserved^":%d,^"shadowMessages^":%d,^"activeEntities^":%d}",gModelCount,gSoundCount,gGenericCount,gModel,gSprite,gSoundA,gSoundB,gProbes,gAmxxChecks,gReapiChecks,gAmxxBrassChecks,gReapiBrassChecks,gFailures,gEntities[0],gEntities[1],gShadowExpected,gShadowObserved,gShadowMessages,active);
    fclose(file);return PLUGIN_HANDLED;
}
