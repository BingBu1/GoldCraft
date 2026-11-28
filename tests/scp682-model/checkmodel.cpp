#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <initializer_list>
#include <extdll.h>
#include <studio.h>
#include <studio_event.h>
#include <monsterevent.h>
#include "sdk_event.inc"

namespace {
enginefuncs_t eng;
const char* const models[] = {"models/scp682/scp682.mdl"};
int precached[2]{};
bool activated = false;
void logLine(const char* text) {
    if (auto* output = std::fopen("model-test.log", "a")) {
        std::fputs(text, output);
        std::fclose(output);
    }
}
void keyValue(edict_t* entity, KeyValueData* kv) {
    if (std::strcmp(kv->szKeyName, "classname") == 0)
        entity->v.classname = eng.pfnAllocString(kv->szValue);
    kv->fHandled = 1;
}
int spawn(edict_t* ent) {
    if (eng.pfnIndexOfEdict(ent) == 0) {
        logLine("worldspawn\n");

        for (const char* cue : {"run_near", "run_far", "breath", "attack_r", "bite_r", "attack_l", "bite_l", "death", "fall", "settle"}) {
            char sample[64]{};
            std::snprintf(sample,sizeof(sample),"scp682/%s.wav",cue);
            if (eng.pfnPrecacheSound(sample)<=0) return -1;
        }
        precached[0]=eng.pfnPrecacheModel(models[0]);
    }
    return 0;
}
struct Cue {int frame; int event; const char* name;};
bool checkModel(int) {
    const char* clips[]={"SCP_862_Run","SCP_862_Idle","SCP_862_Attack_Right","SCP_862_Attack_Left","SCP_862_Death"};
    const int counts[]={13,41,27,27,36};
    const Cue cues[5][3]={
      {{1,1004,"run_near"},{7,1004,"run_far"},{-1,0,""}},
      {{5,1008,"breath"},{-1,0,""},{-1,0,""}},
      {{3,1008,"attack_r"},{14,1004,"bite_r"},{-1,0,""}},
      {{3,1008,"attack_l"},{14,1004,"bite_l"},{-1,0,""}},
      {{1,1008,"death"},{8,1004,"fall"},{18,1004,"settle"}}};
    if(precached[0]<=0)return false;
    auto* ent=eng.pfnCreateEntity();
    if(!ent)return false;
    eng.pfnSetModel(ent,models[0]);
    auto* hdr=static_cast<studiohdr_t*>(eng.pfnGetModelPtr(ent));
    if(!hdr||hdr->version!=10||hdr->numseq!=5||hdr->numbones<26||hdr->numbones>128||hdr->numattachments!=1||hdr->numhitboxes!=12)return false;
    auto* bytes=reinterpret_cast<unsigned char*>(hdr);
    auto* seq=reinterpret_cast<mstudioseqdesc_t*>(bytes+hdr->seqindex);
    char report[256];
    for(int k=0;k<5;++k){
        const auto& s=seq[k];
        if(std::strcmp(s.label,clips[k])||s.numframes!=counts[k]||s.fps!=24||bool(s.flags&1)!=(k<2))return false;
        ent->v.sequence=k;
        int events=0;
        float first[3]{},maxDist=0;
        for(int frame=0;frame<s.numframes-1;++frame){
            const float scale=256.f/(s.numframes-1);
            ent->v.frame=frame*scale;
            float origin[3]{};
            eng.pfnGetAttachment(ent,0,origin,nullptr);
            if(frame==0)std::memcpy(first,origin,sizeof(first));
            float distance=0;
            for(int i=0;i<3;++i){
                if(!std::isfinite(origin[i])||std::abs(origin[i])>500)return false;
                float delta=origin[i]-first[i];distance+=delta*delta;
            }
            maxDist=std::max(maxDist,std::sqrt(distance));
            MonsterEvent_t ev{};
            int cursor=0;
            while((cursor=GetAnimationEvent(hdr,&ent->v,&ev,(frame-.2f)*scale,(frame+.8f)*scale,cursor))){
                if(events>=3)return false;
                const Cue& expected=cues[k][events++];
                char path[80];std::snprintf(path,sizeof(path),"scp682/%s.wav",expected.name);
                if(frame!=expected.frame||ev.event!=expected.event||std::strcmp(path,ev.options))return false;
                eng.pfnEmitSound(ent,ev.event==1008 ? CHAN_VOICE : CHAN_BODY,ev.options,1.f,ATTN_NORM,0,100);
            }
        }
        if(events!=s.numevents||events!=(k==1?1:k==4?3:2)||maxDist<1.f)return false;
        std::snprintf(report,sizeof(report),"SEQUENCE %s frames %d events_emitted %d mouth_travel %.2f\n",s.label,s.numframes,events,maxDist);
        logLine(report);
    }
    eng.pfnRemoveEntity(ent);
    return true;
}
void activate(edict_t*, int, int) {
    const bool body = checkModel(0);
    logLine(body ? "PASS\n" : "FAIL\n");
    activated = true;
}
void frame() {
    if (activated) {
        activated = false;
        eng.pfnServerCommand("quit\n");
    }
}
}
extern "C" void __stdcall GiveFnptrsToDll(enginefuncs_t* api, globalvars_t*) { eng = *api; }
extern "C" void worldspawn(entvars_t*) {}
extern "C" void info_player_start(entvars_t*) {}
extern "C" void light(entvars_t*) {}
extern "C" int GetEntityAPI2(DLL_FUNCTIONS* out, int* version) {
    if (*version != INTERFACE_VERSION) { *version = INTERFACE_VERSION; return 0; }
    *out = {};
    out->pfnGameInit = [](){ logLine("GameInit\n"); };
    out->pfnPM_Init = [](playermove_s*){};
    out->pfnRegisterEncoders = [](){};
    out->pfnGetHullBounds = [](int, float*, float*){ return 0; };
    out->pfnGetGameDescription = [](){ return "GoldSrc studio load check"; };
    out->pfnKeyValue = keyValue;
    out->pfnSpawn = spawn;
    out->pfnSetAbsBox = [](edict_t*){};
    out->pfnThink = [](edict_t*){};
    out->pfnServerActivate = activate;
    out->pfnStartFrame = frame;
    out->pfnServerDeactivate = [](){};
    out->pfnCreateBaseline = [](int,int,entity_state_s*,edict_s*,int,vec3_t,vec3_t){};
    out->pfnCreateInstancedBaselines = [](){};
    return 1;
}
