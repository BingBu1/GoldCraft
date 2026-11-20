// SPDX-License-Identifier: GPL-3.0-or-later
#include "../runtime.hpp"
#include "guest_cof_light_equipment.hpp"
#include "guest_cof_scene.hpp"
#include "weapon_inventory.hpp"
#include "../../../guest/cof/light_equipment.hpp"
#include "../../../guest/cof/client/muzzle_flash.hpp"
#include <event_api.h>
#include <r_efx.h>
#include <dlight.h>
#include <IMetaRendererScene.h>
#include <pmtrace.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace csol::client
{
namespace
{
namespace gear = guest::cof::light;
using Position = std::array<float,3>;
struct HeldModel
{
    const model_t* model{};
    const studiohdr_t* header{};
    const mstudioattachment_t* attachments{};
    int index{}, count{};
};
struct LightPose
{
    const HeldModel* source{};
    unsigned frame{};
    Position first{}, second{};
};
HeldModel lanternView, lanternPlayer, flareView, flarePlayer;
int lanternWorld{}, flareWorld{};
bool prepared{};
unsigned frame{};
LightPose localPose;
std::array<LightPose,33> playerPoses;

struct GroundFlare
{
    int entity{};
    unsigned lastFrame{};
    dlight_t* light{};
};
std::array<GroundFlare,32> groundFlares{};
unsigned groundCount{};
constexpr int FlareBaseRadius=220;
IMetaRendererScene* shadowScene{};

int LightKey(int entity) { return -0x4c00-entity; }

Position FlareColor()
{
    const float red=engine.pfnRandomFloat(-.5f,.5f);
    const float other=engine.pfnRandomFloat(-.05f,.05f);
    return {red+1.15f,other+.2f,other+.1f};
}

color24 NativeLightColor(const Position& color)
{
    // GoldSrc dlights store one byte per channel.
    const auto channel=[](float x){return static_cast<byte>(std::clamp(x,0.f,1.f)*255.f+.5f);};
    return {channel(color[0]),channel(color[1]),channel(color[2])};
}

void ReleaseGroundFlare(unsigned slot)
{
    const auto& flare=groundFlares[slot];
    const int key=LightKey(flare.entity);
    if(flare.light && flare.light->key==key)flare.light->radius=0;
    if(shadowScene)shadowScene->SetDynamicLightShadowSize(key,0);
    groundFlares[slot]=groundFlares[--groundCount];
}

void GroundLight(const cl_entity_s& entity)
{
    if(!engine.pEfxAPI || !engine.pEfxAPI->CL_AllocDlight)return;
    const int key=LightKey(entity.index);
    unsigned slot=0;
    while(slot<groundCount && groundFlares[slot].entity!=entity.index)++slot;
    if(slot==groundCount && slot==groundFlares.size())return;
    auto* light=engine.pEfxAPI->CL_AllocDlight(key);
    if(!light)return;
    if(slot==groundCount){
        ++groundCount;
        if(shadowScene)shadowScene->SetDynamicLightShadowSize(key,256);
    }
    groundFlares[slot]={entity.index,frame,light};
    std::copy_n(&entity.origin[0],3,&light->origin[0]);
    light->origin[2]+=5.f;
    // The renderer accumulates engine lights before multiplying by the
    // material albedo. Keep the radius stable to reuse its BSP shadow map.
    light->radius=FlareBaseRadius;
    light->color=NativeLightColor(FlareColor());
    light->die=engine.GetClientTime()+.12f;
}


int ModelIndex(const char* path)
{
    return engine.pEventAPI->EV_FindModelIndex(path);
}

HeldModel Held(const char* path, int count)
{
    const int index=ModelIndex(path);
    auto* model=index>0 ? studio.GetModelByIndex(index) : nullptr;
    if(!model || model->type!=mod_studio || std::strncmp(model->name,path,sizeof(model->name)))return {};
    const auto* header=static_cast<studiohdr_t*>(studio.Mod_Extradata(model));
    if(!header || header->length<int(sizeof(*header)) || header->numbones<=0 || header->numbones>MAXSTUDIOBONES ||
       header->numattachments<count || header->attachmentindex<int(sizeof(*header)) ||
       header->attachmentindex>header->length-int(sizeof(mstudioattachment_t))*count)return {};
    const auto* attachments=reinterpret_cast<const mstudioattachment_t*>(
        reinterpret_cast<const char*>(header)+header->attachmentindex);
    for(int i=0;i<count;++i)if(attachments[i].bone<0 || attachments[i].bone>=header->numbones)return {};
    return {model,header,attachments,index,count};
}

void Illuminate(int entity, const Position& origin, bool flare)
{
    if(!std::all_of(origin.begin(),origin.end(),[](float x){return std::isfinite(x);}))return;
    // Isolate these lights from the engine's player-keyed flashlight.
    const int key=LightKey(entity);
    auto* original=AllocateGuestCofPointLight(key);
    auto* native=(!original && engine.pEfxAPI && engine.pEfxAPI->CL_AllocDlight) ?
        engine.pEfxAPI->CL_AllocDlight(key) : nullptr;
    if(!original && !native)return;
    const Position color=flare ? FlareColor() : Position{.4f,.4f,.4f};
    // The scenario doubles the lantern's reach. CoF's per-frame jitter,
    // attenuation curve and flare radius are shared by both rendering backends.
    constexpr int lanternRadius=440;
    const int flicker=flare?15:5;
    const float radius=float((flare?FlareBaseRadius:lanternRadius)+engine.pfnRandomLong(-flicker,flicker));
    const float die=float(double(engine.GetClientTime())+.01);
    if(original){
        original->origin=origin; original->color=color; original->radius=radius; original->die=die;
    }else{
        std::copy(origin.begin(),origin.end(),&native->origin[0]);
        native->color=NativeLightColor(color);
        native->radius=radius;native->die=die;
    }
}

void HeldLight(int index, LightPose& pose, const HeldModel& expected, bool flare)
{
    // Pose matrices belong to the last main Studio pass, after animation and
    // bone merging. Refdef and AddEntity run before this frame's Studio pass.
    if(!expected.header || pose.source!=&expected || frame-pose.frame>1)return;
    if(flare){Illuminate(index,pose.first,true);return;}
    const auto* hit=engine.PM_TraceLine(pose.first.data(),pose.second.data(),0,1,-1);
    if(hit)Illuminate(index,{hit->endpos[0],hit->endpos[1],hit->endpos[2]},false);
}
}

void ResetCofLightEquipment()
{
    while(groundCount)ReleaseGroundFlare(groundCount-1);
    shadowScene=nullptr;
    prepared=false; lanternWorld=flareWorld=0;
    lanternView={};lanternPlayer={};flareView={};flarePlayer={};
    localPose={};playerPoses={};frame=0;
}

void PrepareCofLightEquipment()
{
    if(prepared || !FindWeaponDefinition(gear::Lantern.id) || !FindWeaponDefinition(gear::Flare.id) ||
       !engine.pEventAPI || !engine.pEventAPI->EV_FindModelIndex || !studio.GetModelByIndex ||
       !studio.Mod_Extradata || !engine.GetViewModel || !engine.GetLocalPlayer || !engine.PM_TraceLine ||
       !engine.GetClientTime || !engine.pfnRandomLong || !engine.pfnRandomFloat)return;
    if(api && api->GetPluginModuleHandleByBaseFileName){
        auto module=api->GetPluginModuleHandleByBaseFileName("Renderer");
        if(!module)module=api->GetPluginModuleHandleByBaseFileName("Renderer_AVX2");
        auto factory=module?reinterpret_cast<CreateInterfaceFn>(GetProcAddress(HMODULE(module),"CreateInterface")):nullptr;
        if(factory)shadowScene=static_cast<IMetaRendererScene*>(factory(METARENDERER_SCENE_INTERFACE_VERSION,nullptr));
    }
    lanternView=Held(gear::Lantern.view,2); lanternPlayer=Held(gear::Lantern.player,2);
    flareView=Held(gear::Flare.view,1); flarePlayer=Held(gear::FlareLitPlayer,1);
    lanternWorld=ModelIndex(gear::Lantern.world); flareWorld=ModelIndex(gear::Flare.world);
    prepared=true;
}

void CofLightEquipmentPose(const cl_entity_s* entity,const model_s* model,const void* header)
{
    if(!prepared || !entity || !header)return;
    const HeldModel* source{};
    LightPose* pose{};
    if(model && (model==lanternView.model || model==flareView.model) && entity==engine.GetViewModel()){
        source=model==lanternView.model?&lanternView:&flareView;
        pose=&localPose;
    }else if(entity->player && entity->index>0 && entity->index<int(playerPoses.size())){
        if(header==lanternPlayer.header && entity->curstate.weaponmodel==lanternPlayer.index)source=&lanternPlayer;
        else if(header==flarePlayer.header && entity->curstate.weaponmodel==flarePlayer.index)source=&flarePlayer;
        if(source)pose=&playerPoses[entity->index];
    }
    if(!source || source->header!=header || !studio.StudioGetLightTransform ||
       (pose->source==source && pose->frame==frame))return;
    const auto* bones=reinterpret_cast<const float (*)[3][4]>(studio.StudioGetLightTransform());
    if(!bones)return;
    for(int i=0;i<source->count;++i){
        const auto& attachment=source->attachments[i];
        auto& point=i?pose->second:pose->first;
        for(int j=0;j<3;++j){
            const auto& m=bones[attachment.bone][j];
            point[j]=((attachment.org[0]*m[0]+attachment.org[1]*m[1])+attachment.org[2]*m[2])+m[3];
        }
    }
    pose->source=source;pose->frame=frame;
}

void CofLightEquipmentView(const ref_params_s& params)
{
    if(!prepared || params.nextView)return;
    ++frame;
    for(unsigned i=groundCount;i>0;--i)
        if(frame-groundFlares[i-1].lastFrame>1)ReleaseGroundFlare(i-1);
    const auto* local=engine.GetLocalPlayer();
    const auto* item=engine.GetViewModel();
    if(!local || !item || !item->model || params.spectator || params.health<=0 ||
       params.viewentity!=local->index){localPose={};return;}
    if(item->model==lanternView.model)HeldLight(local->index,localPose,lanternView,false);
    else if(item->model==flareView.model && flarePlayer.index>0 && local->curstate.weaponmodel==flarePlayer.index)
        HeldLight(local->index,localPose,flareView,true);
    else localPose={};
}

void CofLightEquipmentEntity(const cl_entity_s& entity)
{
    if(!prepared || entity.index<=0 || (entity.curstate.effects&EF_NODRAW))return;
    if(entity.player){
        if(entity.index>=int(playerPoses.size()) || &entity==engine.GetLocalPlayer() ||
           entity.curstate.spectator || entity.curstate.solid==SOLID_NOT)return;
        if(lanternPlayer.index>0 && entity.curstate.weaponmodel==lanternPlayer.index)
            HeldLight(entity.index,playerPoses[entity.index],lanternPlayer,false);
        else if(flarePlayer.index>0 && entity.curstate.weaponmodel==flarePlayer.index)
            HeldLight(entity.index,playerPoses[entity.index],flarePlayer,true);
        return;
    }
    const int index=entity.curstate.modelindex;
    if(index<=0)return;
    const bool flare=index==flareWorld && entity.curstate.body==1;
    if(index==flareWorld && entity.curstate.solid==SOLID_TRIGGER){
        if(flare)GroundLight(entity);
        else for(unsigned i=0;i<groundCount;++i)
            if(groundFlares[i].entity==entity.index){ReleaseGroundFlare(i);break;}
        return;
    }
    if(!flare && index!=lanternWorld)return;
    Illuminate(entity.index,{entity.origin[0],entity.origin[1],entity.origin[2]+(flare?5.f:6.f)},flare);
}

bool CofLightEquipmentEvent(const mstudioevent_s* event,const cl_entity_s* entity)
{
    if(!prepared || !event || !entity || !flareView.model || entity->model!=flareView.model ||
       entity!=engine.GetViewModel())return false;
    if(event->event==5051){
        if(engine.pfnServerCmd)engine.pfnServerCmd(gear::ThrowCommand);
        return true;
    }
    if(event->event!=5050)return false;
    if(engine.pEventAPI->EV_PlaySound){
        const auto* player=engine.GetLocalPlayer();
        if(player)engine.pEventAPI->EV_PlaySound(player->index,const_cast<float*>(&entity->origin[0]),CHAN_WEAPON,
            engine.pfnRandomLong(0,1)?"guest/cof/flare/flare_strike1.wav":"guest/cof/flare/flare_strike2.wav",
            1.f,.8f,0,100);
    }
    return true;
}
}
