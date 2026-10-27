// SPDX-License-Identifier: GPL-3.0-or-later
#include "cannon.hpp"
#include "camera.hpp"
#include "weapon_inventory.hpp"
#include "../cannon.hpp"
#include <r_efx.h>
#include <event_api.h>
#include <triangleapi.h>
#include <pmtrace.h>
#include <in_buttons.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ostream>

namespace csol::client {
namespace {
int predictedClip=-1, presentedModel{};
bool presented{}, shot{};
int predictionCalls{}, effects{}, rejected{}, flames{};
struct PredictedEffect { std::uint32_t seed{}; int owner{}; };
std::array<PredictedEffect,8> predictedEffects{};
unsigned nextPredictedEffect{};
int predictedFires{}, suppressedFires{};
struct Smoke
{
    model_t* model{};
    float born{};
    std::array<float,3> origin{}, velocity{};
};
std::array<Smoke,64> smoke;
int smokes{}, droppedSmoke{};
struct MuzzleDrift { float x{}, y{}; int directionX{1}, directionY{1}; } muzzleDrift;
int muzzleSmokes{}, muzzleFlashes{};

void UpdateMuzzleSmoke(TEMPENTITY* temp, float, float)
{
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
    // Original client 0256f750 advances per temp-entity update, not per second.
    auto& velocity=temp->entity.baseline.origin;
    const float frame=temp->entity.curstate.frame;
    if(frame>7.f)
    {
        for(float& value:velocity)value=float(double(value)*.97);
        velocity[2]=std::min(70.f,float(double(velocity[2])+.7));
    }
    if(frame<=6.f)return;
    muzzleDrift.x=std::min(5.f,float(double(muzzleDrift.x)+.075));
    muzzleDrift.y=std::min(5.f,float(double(muzzleDrift.y)+.075));
    velocity[0]+=muzzleDrift.directionX?muzzleDrift.x:-muzzleDrift.x;
    velocity[1]+=muzzleDrift.directionY?muzzleDrift.y:-muzzleDrift.y;
    if(!engine.pfnRandomLong(0,10) && muzzleDrift.y>3.f)
    {muzzleDrift.directionY=1-muzzleDrift.directionY;muzzleDrift.y=0;}
    if(!engine.pfnRandomLong(0,10) && muzzleDrift.x>3.f)
    {muzzleDrift.directionX=1-muzzleDrift.directionX;muzzleDrift.x=0;}
}
void HitMuzzleSmoke(TEMPENTITY* temp, pmtrace_s* trace)
{
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
    if(!trace)return;
    // 0256efe0: two cross products against the contact plane, scaled by
    // the capped incoming speed. This is separate from the flame hit rule.
    const cannon::Point velocity{temp->entity.baseline.origin[0],temp->entity.baseline.origin[1],temp->entity.baseline.origin[2]};
    const float length=cannon::Length(velocity);
    if(length<=0)return;
    const cannon::Point direction{velocity[0]/length,velocity[1]/length,velocity[2]/length};
    const auto& normal=trace->plane.normal;
    const cannon::Point side{direction[1]*normal[2]-direction[2]*normal[1],
        direction[2]*normal[0]-direction[0]*normal[2],direction[0]*normal[1]-direction[1]*normal[0]};
    const double speed=double(std::min(length,2000.f))*1.5;
    temp->entity.baseline.origin[0]=float(double(side[1]*normal[2]-side[2]*normal[1])*speed);
    temp->entity.baseline.origin[1]=float(double(side[2]*normal[0]-side[0]*normal[2])*speed);
    temp->entity.baseline.origin[2]=float(double(side[0]*normal[1]-side[1]*normal[0])*speed);
}
void EmitMuzzleSmoke(float* origin,const float* forward,const float* velocity)
{
    if(!engine.pEfxAPI->R_DefaultSprite || !engine.pEventAPI->EV_FindModelIndex)return;
    if(engine.pfnRandomLong(0,5))return;
    const char* path=engine.pfnRandomLong(0,1)?"sprites/pistol_smoke1.spr":"sprites/pistol_smoke2.spr";
    const int model=engine.pEventAPI->EV_FindModelIndex(path);
    if(model<=0)return;
    auto* temp=engine.pEfxAPI->R_DefaultSprite(origin,model,35.f);
    if(!temp)return;
    temp->flags|=FTENT_CLIENTCUSTOM|FTENT_PERSIST|FTENT_COLLIDEWORLD;
    const float scale=.26f+engine.pfnRandomFloat(0,.26f*.5f);
    temp->entity.curstate.scale=scale;
    temp->entity.curstate.rendermode=kRenderTransAdd;
    temp->entity.curstate.renderamt=150;
    temp->entity.baseline.renderamt=150;
    temp->entity.curstate.renderfx=kRenderFxFadeSlow;
    temp->entity.curstate.rendercolor={15,15,15};
    temp->callback=UpdateMuzzleSmoke;temp->hitcallback=HitMuzzleSmoke;
    const float speed=float(50+engine.pfnRandomLong(0,5));
    for(int i=0;i<3;++i)
        temp->entity.baseline.origin[i]=float(double(forward[i]*speed)+double(velocity[i])*(i==2?.5:.9));
    ++muzzleSmokes;
}

void EmitSmoke(const float* origin, const float* forward)
{
    if(!engine.CL_LoadModel || !engine.GetClientTime)return;
    const float now=float(engine.GetClientTime());
    auto slot=std::find_if(smoke.begin(),smoke.end(),[&](const auto& p){return !p.model || now-p.born>=1.5f || now<p.born;});
    if(slot==smoke.end()){++droppedSmoke;return;}
    int index{};
    auto* model=engine.CL_LoadModel("sprites/black_smoke4.spr",&index);
    if(!model || model->numframes<=0)return;
    *slot={model,now};
    std::copy_n(origin,3,slot->origin.begin());
    for(int i=0;i<3;++i)slot->velocity[i]=forward[i]*60.f;
    ++smokes;
}

void GrowFlame(TEMPENTITY* temp, float, float)
{
    // CSO 0256e980 adds once per temp-entity update, without a delta-time
    // multiplier. The original comparison permits the last .03 overshoot.
    if(temp->entity.curstate.scale<temp->entity.prevstate.scale)
        temp->entity.curstate.scale+=.03f;
}
void HitFlame(TEMPENTITY* temp, pmtrace_s*)
{
    for(float& velocity:temp->entity.baseline.origin)velocity*=.01f;
    temp->entity.curstate.framerate=60;
}
bool FireEffect(int owner, bool ducking, float* origin, float* angles, const float* velocity)
{
    auto* ev=engine.pEventAPI;
    auto* fx=engine.pEfxAPI;
    if(!ev || !fx || !ev->EV_FindModelIndex || !ev->EV_PlaySound || !fx->R_DefaultSprite ||
        !engine.pfnAngleVectors || !engine.pfnRandomLong || !engine.pfnRandomFloat)return false;
    const int sprite=ev->EV_FindModelIndex("sprites/flame_puff01.spr");
    if(sprite<=0)return false;
    float forward[3]{},right[3]{},up[3]{},muzzle[3]{};
    engine.pfnAngleVectors(angles,forward,right,up);
    const auto* local=engine.GetLocalPlayer?engine.GetLocalPlayer():nullptr;
    const bool isLocal=local && local->index==owner;
    const auto* model=engine.GetViewModel?engine.GetViewModel():nullptr;
    const bool viewMuzzle=isLocal && !CameraIsThirdPerson() && model && model->model &&
        !std::strcmp(model->model->name,"models/v_cannon.mdl");
    for(int i=0;i<3;++i)
        muzzle[i]=viewMuzzle ? model->attachment[0][i]+forward[i]*7+velocity[i]*.015f :
            origin[i]+forward[i]*40-up[i]*(ducking?3.f:9.f);
    if(isLocal)
    {
        float smokeOrigin[3];
        std::copy_n(viewMuzzle?model->attachment[0]:muzzle,3,smokeOrigin);
        EmitMuzzleSmoke(smokeOrigin,forward,velocity);
    }
    for(int i=0;i<12;++i)
    {
        auto* temp=fx->R_DefaultSprite(muzzle,sprite,20);
        if(!temp)break;
        temp->flags|=FTENT_CLIENTCUSTOM|FTENT_COLLIDEALL|FTENT_ROTATE;
        temp->entity.baseline.angles[0]=engine.pfnRandomFloat(-512,511);
        temp->entity.baseline.angles[1]=engine.pfnRandomFloat(-256,255);
        temp->entity.baseline.angles[2]=engine.pfnRandomFloat(-256,255);
        temp->entity.curstate.scale=engine.pfnRandomFloat(.3f,.8f);
        temp->entity.prevstate.scale=engine.pfnRandomFloat(1.5f,2);
        temp->callback=GrowFlame;temp->hitcallback=HitFlame;
        temp->entity.curstate.rendermode=kRenderTransAdd;
        temp->entity.curstate.renderamt=150;
        temp->entity.baseline.renderamt=150;
        temp->entity.curstate.renderfx=kRenderFxFadeSlow;
        temp->entity.curstate.rendercolor={255,255,255};
        constexpr double radians=3.14159265358979323846/180;
        const float angle=float((engine.pfnRandomLong(0,4)-2)*10*radians);
        const float cosine=float(std::cos(double(angle))),sine=float(std::sin(double(angle)));
        temp->entity.baseline.origin[0]=(forward[0]*cosine-forward[1]*sine)*engine.pfnRandomLong(130,400)+velocity[0]*.9f;
        temp->entity.baseline.origin[1]=(forward[0]*sine+forward[1]*cosine)*engine.pfnRandomLong(130,400)+velocity[1]*.9f;
        temp->entity.baseline.origin[2]=forward[2]*engine.pfnRandomLong(30,200)+velocity[2]*.3f;
        temp->clientIndex=short(owner);
        ++flames;
    }
    if(isLocal && fx->CL_AllocDlight && engine.GetClientTime)if(auto* light=fx->CL_AllocDlight(0))
    {
        std::copy(std::begin(muzzle),std::end(muzzle),light->origin);
        light->radius=50;light->decay=25;light->color={225,190,80};
        light->die=float(engine.GetClientTime()+.05);
    }
    if(ev->EV_PlaySound)ev->EV_PlaySound(owner,origin,CHAN_WEAPON,"weapons/cannon-1.wav",1,.52f,0,engine.pfnRandomLong(0,15)+94);
    return true;
}
int ReceiveEffect(const char*,int size,void* data)
{
    if(size!=cannon::EffectBytes || !data || !FindWeaponDefinition(cannon::Id)){++rejected;return 0;}
    const auto* bytes=static_cast<const unsigned char*>(data);
    if(bytes[0]!=2 || bytes[1]<1 || bytes[1]>32 || bytes[2]>1){++rejected;return 0;}
    float values[9]{};
    for(int i=0;i<9;++i){int value=bytes[3+i*2]|(bytes[4+i*2]<<8);if(value>=32768)value-=65536;values[i]=value/8.f;}
    // The original black-smoke TempEmit is authoritative. Flame and sound
    // also run through the original client's predicted event callback.
    if(engine.pfnAngleVectors)
    {
        float forward[3],right[3],up[3];engine.pfnAngleVectors(values+3,forward,right,up);
        EmitSmoke(values,forward);
    }
    std::uint32_t seed{};
    for(int i=0;i<4;++i)seed|=std::uint32_t(bytes[21+i])<<(i*8);
    auto pending=std::find_if(predictedEffects.begin(),predictedEffects.end(),[&](const auto& item){return item.owner==bytes[1] && item.seed==seed;});
    if(pending!=predictedEffects.end()){*pending={};++suppressedFires;}
    else FireEffect(bytes[1],bytes[2]!=0,values,values+3,values+6);
    ++effects;return 1;
}
}

void InitializeCannon(){engine.pfnHookUserMsg(cannon::EffectMessage,ReceiveEffect);}
bool CannonStudioEvent(const mstudioevent_t* event,const cl_entity_t* entity)
{
    if(!event || !entity || !entity->model || event->event!=5001 ||
        std::strcmp(entity->model->name,"models/v_cannon.mdl") ||
        std::string_view(event->options,strnlen(event->options,64))!="103112")return false;
    // Original HUD_StudioEvent adds 10000 for the viewmodel. HW decodes
    // 103112/113112 as muzzleflash12, scale .1, fixed roll, local mode 3.
    // This numeric event is separate from the predicted flame-cloud event.
    auto* fx=engine.pEfxAPI;
    if(!fx || !fx->CL_TempEntAlloc || !engine.CL_LoadModel || !engine.GetClientTime)return true;
    const bool local=engine.GetViewModel && entity==engine.GetViewModel();
    if(!local && !engine.pfnRandomLong)return true;
    int index{};
    auto* model=engine.CL_LoadModel("sprites/muzzleflash12.spr",&index);
    if(!model || model->type!=mod_sprite || model->numframes<=0)return true;
    float origin[3];std::copy_n(entity->attachment[0],3,origin);
    auto* temp=fx->CL_TempEntAlloc(origin,model);
    if(!temp)return true;
    temp->flags=local?(FTENT_FADEOUT|FTENT_SPRANIMATE|FTENT_PERSIST|FTENT_SPRANIMATELOOP):0;
    temp->die=float(double(engine.GetClientTime())+(local?.5:.01));
    temp->frameMax=float(model->numframes);
    temp->fadeSpeed=.5f;temp->bounceFactor=1;
    temp->callback=nullptr;temp->hitcallback=nullptr;
    temp->entity.curstate.scale=.1f;
    temp->entity.curstate.rendermode=kRenderTransAdd;
    temp->entity.curstate.renderamt=255;
    // Original mode 3 leaves baseline alpha zero. Its FADEOUT branch thus
    // removes the flash at .5 seconds rather than adding a two-second fade.
    temp->entity.baseline.renderamt=0;
    temp->entity.curstate.renderfx=kRenderFxNone;
    temp->entity.curstate.rendercolor={255,255,255};
    temp->entity.curstate.frame=local?0.f:float(engine.pfnRandomLong(0,model->numframes-1));
    temp->entity.curstate.framerate=local?20.f:0.f;
    std::fill_n(temp->entity.baseline.origin,3,0.f);
    std::fill_n(temp->entity.angles,3,0.f);
    std::fill_n(temp->entity.curstate.angles,3,0.f);
    ++muzzleFlashes;return true;
}
void ResetCannon(){predictedClip=-1;presented=false;presentedModel=0;shot=false;}
void ResetCannonEffects(){smoke={};muzzleDrift={};predictedEffects={};nextPredictedEffect=0;}
void DrawCannonSmoke()
{
    auto* tri=engine.pTriAPI;
    if(!tri || !engine.pfnAngleVectors || !engine.GetClientTime)return;
    const float now=float(engine.GetClientTime());
    struct Draw { Smoke* particle; cannon::SmokeFrame sample; std::array<float,3> origin; float distance{}; };
    std::array<Draw,64> draws{};
    int count{};
    for(auto& p:smoke)
    {
        if(!p.model)continue;
        const float age=now-p.born;
        const auto sample=cannon::SampleSmoke(age,p.model->numframes);
        if(sample.alpha<=0){p={};continue;}
        auto& draw=draws[count++];draw.particle=&p;draw.sample=sample;
        for(int i=0;i<3;++i)
        {
            draw.origin[i]=p.origin[i]+p.velocity[i]*age;
            const float delta=draw.origin[i]-view.vieworg[i];draw.distance+=delta*delta;
        }
    }
    if(!count)return;
    std::sort(draws.begin(),draws.begin()+count,[](const auto& a,const auto& b){return a.distance>b.distance;});
    float angles[]{view.viewangles[0],view.viewangles[1],1.f},forward[3]{},right[3]{},up[3]{};
    engine.pfnAngleVectors(angles,forward,right,up);
    // DrawWorldEffects supplies the existing GL state guard for both weapons.
    tri->RenderMode(kRenderTransAlpha);tri->CullFace(TRI_NONE);
    constexpr float uv[4][2]{{0,0},{0,1},{1,1},{1,0}};
    constexpr float side[4][2]{{-1,1},{-1,-1},{1,-1},{1,1}};
    for(int n=0;n<count;++n)
    {
        const auto& draw=draws[n];
        if(!tri->SpriteTexture(draw.particle->model,draw.sample.frame))continue;
        tri->Color4f(1,1,1,draw.sample.alpha);tri->Begin(TRI_QUADS);
        for(int corner=0;corner<4;++corner)
        {
            float point[3];
            for(int i=0;i<3;++i)point[i]=draw.origin[i]+draw.sample.width*.5f*(side[corner][0]*right[i]+side[corner][1]*up[i]);
            tri->TexCoord2f(uv[corner][0],uv[corner][1]);tri->Vertex3f(point[0],point[1],point[2]);
        }
        tri->End();
    }
    tri->Color4f(1,1,1,1);tri->RenderMode(kRenderNormal);tri->CullFace(TRI_FRONT);
}
void ReceiveCannonState(const WeaponCurrent& current)
{
    predictedClip=-1;
    if(current.id!=cannon::Id || !current.state){presented=false;presentedModel=0;shot=false;}
}
int CannonPredictedAmmo(){return predictedClip;}
bool CannonCrosshairShot(){const bool value=shot;shot=false;return value;}
bool PredictCannon(local_state_t* from,local_state_t* to,usercmd_t* command,int runFunctions,double time,unsigned seed)
{
    const auto* definition=FindWeaponDefinition(cannon::Id);
    if(from->client.m_iId!=cannon::Id || from->weapondata[ActiveWeaponSlot].m_iId!=cannon::Id ||
        !definition || definition->name!="weapon_cannon")
    {if(runFunctions)ResetCannon();return false;}
    ++predictionCalls;
    const auto source=from->client;
    auto weapon=from->weapondata[ActiveWeaponSlot];
    from->client.m_iId=0;
    downstream.HUD_PostRunCmd(from,to,command,runFunctions,time,seed);
    from->client.m_iId=cannon::Id;to->client.m_iId=cannon::Id;
    to->client.viewmodel=source.viewmodel;to->client.weaponanim=source.weaponanim;
    to->client.m_flNextAttack=source.m_flNextAttack;to->client.iuser3=source.iuser3;
    std::copy(std::begin(source.vuser4),std::end(source.vuser4),std::begin(to->client.vuser4));
    to->playerstate.oldbuttons=command->buttons;
    int animation=-1;
    const bool alive=source.health>0 && !source.deadflag;
    if(alive && source.m_flNextAttack<=0)
    {
        const bool canShoot=(source.iuser3&1) && !(source.iuser3&2);
        if((command->buttons&IN_ATTACK) && !(command->buttons&IN_ATTACK2) && weapon.m_flNextPrimaryAttack<=0 && canShoot)
        {
            if(weapon.m_iClip>0)
            {
                --weapon.m_iClip;animation=1;
                weapon.m_flNextPrimaryAttack=weapon.m_flNextSecondaryAttack=weapon.m_flTimeWeaponIdle=cannon::Interval;
                if(runFunctions)shot=true;
            }
            else weapon.m_flNextPrimaryAttack=weapon.m_flNextSecondaryAttack=cannon::EmptyInterval;
        }
        else if(!(command->buttons&(IN_ATTACK|IN_ATTACK2)) && weapon.m_flTimeWeaponIdle<=0)
        {animation=0;weapon.m_flTimeWeaponIdle=20;}
    }
    if(animation>=0)to->client.weaponanim=animation;
    if(runFunctions && alive)
    {
        auto* model=engine.GetViewModel?engine.GetViewModel():nullptr;
        const int sequence=to->client.weaponanim;
        if(sequence>=0 && sequence<=2 && (animation>=0 || !presented || presentedModel!=source.viewmodel ||
            (model && model->curstate.sequence!=sequence)))
            engine.pfnWeaponAnim(sequence,model?model->curstate.body:0);
        presented=true;presentedModel=source.viewmodel;
        if(animation==1)
        {
            const auto* local=engine.GetLocalPlayer?engine.GetLocalPlayer():nullptr;
            if(local && local->index>=1 && local->index<=32)
            {
                float origin[3],angles[3];
                for(int i=0;i<3;++i){origin[i]=to->client.origin[i]+source.view_ofs[i];angles[i]=command->viewangles[i]+source.punchangle[i];}
                if(FireEffect(local->index,(source.flags&FL_DUCKING)!=0,origin,angles,to->client.velocity))
                {predictedEffects[nextPredictedEffect++%predictedEffects.size()]={seed,local->index};++predictedFires;}
            }
        }
    }
    else if(runFunctions)presented=false;
    const float dt=command->msec/1000.f;
    weapon.m_flNextPrimaryAttack=std::max(-.001f,weapon.m_flNextPrimaryAttack-dt);
    weapon.m_flNextSecondaryAttack=std::max(-.001f,weapon.m_flNextSecondaryAttack-dt);
    weapon.m_flTimeWeaponIdle=std::max(-.001f,weapon.m_flTimeWeaponIdle-dt);
    to->client.m_flNextAttack=std::max(-.001f,to->client.m_flNextAttack-dt);
    to->weapondata[ActiveWeaponSlot]=weapon;
    if(runFunctions)predictedClip=weapon.m_iClip;
    return true;
}
void CannonDiagnostics(std::ostream& out)
{
    out<<",\"cannon\":{\"predictedClip\":"<<predictedClip<<",\"predictionCalls\":"<<predictionCalls
        <<",\"effects\":"<<effects<<",\"flames\":"<<flames<<",\"smokes\":"<<smokes
        <<",\"muzzleSmokes\":"<<muzzleSmokes<<",\"muzzleFlashes\":"<<muzzleFlashes
        <<",\"predictedFires\":"<<predictedFires<<",\"suppressedFires\":"<<suppressedFires
        <<",\"droppedSmoke\":"<<droppedSmoke<<",\"rejected\":"<<rejected<<'}';
}
}
