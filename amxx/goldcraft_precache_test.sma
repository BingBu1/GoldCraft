// Opt-in fixture, never enabled by normal server installation. Sources/API:
// AMXX1.9.0.5303, ReAPI5.29.0.358 and the paired GoldCraft precache-v2 engine.
#include <amxmodx>
#include <fakemeta>
#include <reapi>

new gModel, gSprite, gSoundA, gSoundB, gLowSprite;
new gModelCount, gSoundCount, gGenericCount, gProbes;
new gAmxxChecks, gReapiChecks, gFailures, gMode, bool:gSending;
new gEntities[2];
new const gHighModel[] = "models/gc_probe/high.mdl";
new const gHighSprite[] = "sprites/gc_probe/high.spr";
new const gHighSoundA[] = "gc_probe/high_a.wav";
new const gHighSoundB[] = "gc_probe/high_b.wav";

public plugin_precache()
{
    if(get_cvar_num("gc_precache_protocol")!=2){set_fail_state("GoldCraft precache v2 engine required");return;}
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
    register_plugin("GoldCraft precache boundary fixture","2.0","GoldCraft contributors");
    register_srvcmd("gc_precache_probe","Probe");
    register_srvcmd("gc_precache_probe_status","Status");
    register_message(SVC_TEMPENTITY,"AmxxMessage");
    RegisterMessage(SVC_TEMPENTITY,"EngineMessage");
}

stock Check(bool:ok){if(!ok)gFailures++;}

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

public Probe()
{
    new id;for(new i=1;i<=get_maxplayers();i++)if(is_user_connected(i)&&!is_user_bot(i)&&!is_user_hltv(i)){id=i;break;}
    if(!id){server_print("GoldCraft precache probe needs the single B client");return PLUGIN_HANDLED;}
    new Float:pos[3],Float:angles[3],Float:direction[3],Float:view[3];
    pev(id,pev_origin,pos);pev(id,pev_view_ofs,view);pev(id,pev_v_angle,angles);
    engfunc(EngFunc_MakeVectors,angles);global_get(glb_v_forward,direction);
    for(new i=0;i<3;i++)pos[i]+=view[i]+direction[i]*96.0;
    for(new i=0;i<2;i++){
        if(pev_valid(gEntities[i]))engfunc(EngFunc_RemoveEntity,gEntities[i]);
        new ent=engfunc(EngFunc_CreateNamedEntity,engfunc(EngFunc_AllocString,"info_target"));gEntities[i]=ent;
        set_pev(ent,pev_classname,"gc_precache_probe");set_pev(ent,pev_movetype,MOVETYPE_NONE);set_pev(ent,pev_solid,SOLID_NOT);
        engfunc(EngFunc_SetModel,ent,i?gHighSprite:gHighModel);
        new Float:p[3];for(new j=0;j<3;j++)p[j]=pos[j];p[2]+=float(i*16);
        engfunc(EngFunc_SetOrigin,ent,p);set_pev(ent,pev_renderamt,255.0);
    }
    new amxxBefore=gAmxxChecks,reapiBefore=gReapiChecks;
    for(new mode=0;mode<4;mode++)SpriteMessage(id,pos,mode);
    for(new mode=0;mode<6;mode++)SpriteMessage(id,pos,mode,true);
    Check(gAmxxChecks-amxxBefore==6 && gReapiChecks-reapiBefore==10);
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
    new file=fopen("addons/amxmodx/logs/goldcraft-precache.json","wt");if(!file)return PLUGIN_HANDLED;
    fprintf(file,"{^"models^":%d,^"sounds^":%d,^"generic^":%d,^"highModel^":%d,^"highSprite^":%d,^"highSounds^":[%d,%d],^"probes^":%d,^"amxxChecks^":%d,^"reapiChecks^":%d,^"failures^":%d,^"entities^":[%d,%d]}",gModelCount,gSoundCount,gGenericCount,gModel,gSprite,gSoundA,gSoundB,gProbes,gAmxxChecks,gReapiChecks,gFailures,gEntities[0],gEntities[1]);
    fclose(file);return PLUGIN_HANDLED;
}
