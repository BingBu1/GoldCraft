// SPDX-License-Identifier: GPL-3.0-or-later
// Actual ReHLDS SDK and extracted production hull/trace bodies. Bone transforms
// are deliberately controlled; MDL hitboxes and all collision selection are real.
#include "precompiled.h"
#include "precache.h"
#include <cmath>
#include <cstring>
#include <stdexcept>
#include "bridge.h"

GoldCraftPrecache gc_precache;
globalvars_t gGlobalVariables{};
GameType_e g_eGameType=GT_CStrike;
vec3_t vec3_origin{};
extern "C" int A_memcmp(const void* a,const void* b,size_t n){return std::memcmp(a,b,n);}
extern "C" void* A_memcpy(void* a,const void* b,size_t n){return std::memcpy(a,b,n);}
extern "C" void* A_memset(void* a,int b,size_t n){return std::memset(a,b,n);}
cvar_t sv_clienttrace={"sv_clienttrace","1",0,1,nullptr};
static const studiohdr_t* selectedHeader{};
static int selectedPose{};
static void Bones(model_t*,float,int,const float*,const float*,const unsigned char*,const unsigned char*,int,const edict_t*);
#include "engine_globals.inc"
sv_blending_interface_t svBlending={1,Bones};
sv_blending_interface_t* g_pSvBlendingAPI=&svBlending;

void* Mod_Extradata(model_t*) {return const_cast<studiohdr_t*>(selectedHeader);}
modtype_t Mod_GetType(int) {return mod_studio;}
model_t* Mod_Handle(int index) {return gc_precache.models.at(index);}
bool GoldCraftStudioTagged(model_t*,const studiohdr_t*) {return true;}
void Sys_Error(const char*,...) {throw std::runtime_error("unexpected hull error");}
void Con_DPrintf(const char*,...) {}
int NUM_FOR_EDICT(const edict_t*) {return 1;}
bool GoldCraft_MapTrace(int,model_t*,hull_t*,const float*,const float*,trace_t*) {return false;}
hull_t* SV_HullForEntity(edict_t*,const float*,const float*,float*) {throw std::runtime_error("non-studio path");}
void AngleVectors(const float*,float*,float*,float*) {throw std::runtime_error("rotated BSP path");}
void AngleVectorsTranspose(const float*,float*,float*,float*) {throw std::runtime_error("rotated BSP path");}
#include "engine_production.inc"

static void Bones(model_t*,float,int,const float*,const float*,const unsigned char*,const unsigned char*,int,const edict_t*)
{
    for(int bone=0;bone<MAXSTUDIOBONES;++bone) {
        std::memset(bonetransform[bone],0,sizeof(bonetransform[bone]));
        for(int axis=0;axis<3;++axis)bonetransform[bone][axis][axis]=1;
        bonetransform[bone][1][3]=1000;
    }
    const auto* boxes=reinterpret_cast<const mstudiobbox_t*>(reinterpret_cast<const byte*>(selectedHeader)+selectedHeader->hitboxindex);
    bool head{},chest{};
    for(int i=0;i<selectedHeader->numhitboxes;++i) {
        const auto& b=boxes[i];
        if(b.bone<0||b.bone>=MAXSTUDIOBONES)throw std::runtime_error("invalid actual model bone");
        float center[3]{};bool select{};
        if(b.group==1&&!head){head=true;select=true;center[2]=64;}
        if(b.group==2&&!chest){chest=true;select=true;center[2]=40;}
        if(b.group==8){select=true;center[0]=-30;center[2]=52;}
        if(selectedPose==1&&i==selectedHeader->numhitboxes-1){select=true;center[0]=0;center[2]=40;}
        if(select)for(int axis=0;axis<3;++axis)bonetransform[b.bone][axis][3]=center[axis]-(b.bbmin[axis]+b.bbmax[axis])/2;
    }
}
extern "C" __declspec(dllexport) int __cdecl ShieldTrace(const void* bytes,int gamestate,int player,int pose,const float* start,const float* end,ShieldTraceResult* out)
{
    try {
        selectedHeader=static_cast<const studiohdr_t*>(bytes);selectedPose=pose;
        if(!selectedHeader||!out||selectedHeader->numhitboxes<0||selectedHeader->numhitboxes>STUDIO_NUM_HULLS)return 0;
        model_t model{};model.type=mod_studio;gc_precache.models={nullptr,&model};
        edict_t entity{};entity.v.modelindex=1;entity.v.gamestate=gamestate;entity.v.solid=SOLID_SLIDEBOX;
        entity.v.flags=player?FL_CLIENT:FL_MONSTER;
        trace_t trace{};const float zero[3]{};int count{};vec3_t offset{};
        SV_HullForStudioModel(&entity,zero,zero,offset,&count);
        SV_SingleClipMoveToEntity(&entity,start,zero,zero,end,&trace);
        *out={};out->count=count;out->group=trace.hitgroup;out->fraction=trace.fraction;
        out->hit=trace.ent==&entity;out->startsolid=trace.startsolid;out->allsolid=trace.allsolid;
        for(int i=0;i<3;++i){out->end[i]=trace.endpos[i];out->normal[i]=trace.plane.normal[i];}
        return 1;
    } catch(...) {return 0;}
}
