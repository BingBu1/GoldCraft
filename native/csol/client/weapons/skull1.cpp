// SPDX-License-Identifier: GPL-3.0-or-later
#include "skull1.hpp"
#include "weapon_module.hpp"
#include "../shared_random.hpp"
#include "weapon_inventory.hpp"
#include "../scenario/core/human_kick.hpp"
#include "gun_smoke.hpp"
#include "../../weapons/skull1.hpp"
#include <event_api.h>
#include <event_args.h>
#include <r_efx.h>
#include <pm_defs.h>
#include <in_buttons.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ostream>
#include <sstream>
#include <unordered_map>

namespace csol::client {
namespace {
int predictedClip=-1, predictedReserve=-1, presentedModel{}, presentedAnimation=-1;
bool presented{}, shot{}, emptySoundReady=true;
unsigned predictionCalls{}, effects{}, localEffects{}, remoteEffects{}, muzzleSmokes{}, impacts{}, rejected{};
std::unordered_map<std::string,char> materials;

int Random(int low,int high){return engine.pfnRandomLong?engine.pfnRandomLong(low,high):low;}
float Random(float low,float high){return engine.pfnRandomFloat?engine.pfnRandomFloat(low,high):low;}

std::string TextureKey(const char* name)
{
    if(!name)return {};
    if((name[0]=='+' || name[0]=='-') && name[1])name+=2;
    if(*name=='{' || *name=='!' || *name=='~' || *name==' ')++name;
    std::string key(name,strnlen(name,12));
    for(char& value:key)if(value>='A' && value<='Z')value+=32;
    return key;
}
void LoadMaterials()
{
    materials.clear();
    if(!engine.COM_LoadFile || !engine.COM_FreeFile)return;
    int size{};auto* bytes=engine.COM_LoadFile("sound/materials.txt",5,&size);
    if(!bytes)return;
    std::string contents;
    if(size>0 && size<=1024*1024)contents.assign(reinterpret_cast<const char*>(bytes),size);
    engine.COM_FreeFile(bytes);
    std::istringstream input(contents);
    std::string line;
    while(std::getline(input,line))
    {
        std::istringstream fields(line);char type{};std::string name;
        if(fields>>type>>name && type!='/' && type!='#')materials.emplace(TextureKey(name.c_str()),type);
    }
}
void ViewHeight(bool local,bool ducking,float* height)
{
    height[0]=height[1]=0;height[2]=ducking?12.f:28.f;
    if(local && engine.pEventAPI->EV_LocalPlayerViewheight)engine.pEventAPI->EV_LocalPlayerViewheight(height);
}
char Surface(pmtrace_t& trace,float* start,float* end)
{
    auto* ev=engine.pEventAPI;
    if(!ev->EV_TraceTexture || !ev->EV_IndexFromTrace)return 'C';
    const auto* texture=ev->EV_TraceTexture(ev->EV_IndexFromTrace(&trace),start,end);
    const auto found=materials.find(TextureKey(texture));
    return found==materials.end()?'C':found->second;
}
void Impact(pmtrace_t& trace,const physent_t* entity)
{
    // Flesh/blood and damage belong to the server. Emit brush effects only;
    // creating wall sparks on monsters was a previous scenario regression.
    if(!entity || entity->player || entity->studiomodel || entity->solid!=SOLID_BSP)return;
    auto* fx=engine.pEfxAPI;
    if(!fx)return;
    if(fx->R_BulletImpactParticles)fx->R_BulletImpactParticles(trace.endpos);
    if(Random(0,1)==0 && fx->R_RicochetSound)fx->R_RicochetSound(trace.endpos);
    if(fx->Draw_DecalIndexFromName && fx->Draw_DecalIndex && fx->R_DecalShoot &&
        engine.pEventAPI->EV_IndexFromTrace && (!engine.pfnGetCvarFloat || engine.pfnGetCvarFloat("r_decals")>0))
    {
        char decal[16];std::snprintf(decal,sizeof(decal),entity->rendermode==kRenderNormal?"{shot%d":"{break%d",
            Random(1,entity->rendermode==kRenderNormal?5:3));
        const int index=fx->Draw_DecalIndexFromName(decal);
        if(index>=0)fx->R_DecalShoot(fx->Draw_DecalIndex(index),engine.pEventAPI->EV_IndexFromTrace(&trace),0,trace.endpos,0);
    }
    ++impacts;
}
void Bullet(const event_args_t& args,bool local,const float* forward,const float* right,const float* up)
{
    auto* ev=engine.pEventAPI;
    if(!ev->EV_SetUpPlayerPrediction || !ev->EV_PushPMStates || !ev->EV_PopPMStates ||
        !ev->EV_SetSolidPlayers || !ev->EV_SetTraceHull || !ev->EV_PlayerTrace || !ev->EV_GetPhysent)return;
    float height[3];ViewHeight(local,args.ducking!=0,height);
    float start[3],direction[3];
    for(int i=0;i<3;++i){start[i]=args.origin[i]+height[i];direction[i]=forward[i]+args.fparam1*right[i]+args.fparam2*up[i];}
    ev->EV_SetUpPlayerPrediction(0,1);ev->EV_PushPMStates();ev->EV_SetSolidPlayers(args.entindex-1);ev->EV_SetTraceHull(2);
    float distance=8192;int power=csol::skull1::PenetrationPower;
    for(int penetration=2;penetration>0;--penetration)
    {
        float end[3];for(int i=0;i<3;++i)end[i]=start[i]+direction[i]*distance;
        pmtrace_t trace{};ev->EV_PlayerTrace(start,end,PM_NORMAL,-1,&trace);
        if(trace.fraction>=1 || trace.allsolid || trace.startsolid)break;
        const auto* entity=ev->EV_GetPhysent(trace.ent);
        Impact(trace,entity);
        const float travelled=distance*trace.fraction;
        if(travelled>csol::skull1::PenetrationDistance || trace.hitgroup==8)break;
        if(entity && entity->solid==SOLID_BSP)
        {
            switch(Surface(trace,start,end))
            {
            case 'M':power=int(power*.15f);break;
            case 'C':power=int(power*.25f);break;
            case 'G':case 'V':power=int(power*.5f);break;
            case 'T':power=int(power*.65f);break;
            case 'P':power=int(power*.4f);break;
            default:break;
            }
            distance=(distance-travelled)*.5f;
        }
        else {power=42;distance=(distance-travelled)*.75f;}
        for(int i=0;i<3;++i)start[i]=trace.endpos[i]+direction[i]*power;
    }
    ev->EV_PopPMStates();
}
void UpdateSmoke(TEMPENTITY* temp,float,float)
{
    // Original 0256F6D0: the non-drifting callback selected by event025625E0.
    if(temp->entity.curstate.frame<=7)return;
    for(float& value:temp->entity.baseline.origin)value=float(double(value)*.97);
    temp->entity.baseline.origin[2]=std::min(70.f,float(double(temp->entity.baseline.origin[2])+.7));
}
void HitSmoke(TEMPENTITY* temp,pmtrace_t* trace)
{
    if(!trace)return;
    const auto& v=temp->entity.baseline.origin;const auto& n=trace->plane.normal;
    const float length=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
    if(length<=0)return;
    const float x=v[0]/length,y=v[1]/length,z=v[2]/length;
    const float a=y*n[2]-z*n[1],b=z*n[0]-x*n[2],c=x*n[1]-y*n[0];
    const double speed=double(std::min(length,2000.f))*1.5;
    temp->entity.baseline.origin[0]=float(double(b*n[2]-c*n[1])*speed);
    temp->entity.baseline.origin[1]=float(double(c*n[0]-a*n[2])*speed);
    temp->entity.baseline.origin[2]=float(double(a*n[1]-b*n[0])*speed);
}
void MuzzleSmoke(float* origin,const float* forward,const float* velocity)
{
    auto* fx=engine.pEfxAPI;auto* ev=engine.pEventAPI;
    if(!fx || !fx->R_DefaultSprite || !ev->EV_FindModelIndex || !GunSmokeEnabled())return;
    constexpr const char* pistol[]{"sprites/pistol_smoke2.spr","sprites/pistol_smoke1.spr"};
    constexpr const char* wall[]{"sprites/wall_puff1.spr","sprites/wall_puff2.spr",
        "sprites/wall_puff3.spr","sprites/wall_puff4.spr"};
    for(int layer=0;layer<3;++layer)
    {
        const char* path=layer==0?pistol[Random(0,1)]:wall[Random(0,3)];
        const int index=ev->EV_FindModelIndex(path);if(index<=0)continue;
        auto* temp=fx->R_DefaultSprite(origin,index,35);if(!temp)continue;
        const float base=layer==0?.25f:layer==1?.3f:.2f;
        temp->flags|=FTENT_CLIENTCUSTOM|FTENT_PERSIST|FTENT_COLLIDEWORLD;
        temp->entity.curstate.scale=base+Random(0.f,base*.5f);
        temp->entity.curstate.rendermode=kRenderTransAdd;
        temp->entity.curstate.renderamt=150;temp->entity.curstate.renderfx=kRenderFxFadeSlow;
        const byte shade=layer==0?10:layer==1?15:25;
        temp->entity.curstate.rendercolor={shade,shade,shade};
        temp->callback=UpdateSmoke;temp->hitcallback=HitSmoke;
        const float speed=layer?float(layer*25+Random(0,5)):0;
        for(int i=0;i<3;++i)
            temp->entity.baseline.origin[i]=float(double(forward[i]*speed)+double(velocity[i])*(i==2?.5:.9));
        ++muzzleSmokes;
    }
}
void PlayFire(event_args_t* args,bool rapid,int animation=-1)
{
    if(!args || args->entindex<1 || args->entindex>32 || !engine.pEventAPI || !engine.pfnAngleVectors)
    {++rejected;return;}
    for(int i=0;i<3;++i)
        if(!std::isfinite(args->origin[i]) || !std::isfinite(args->angles[i]) || !std::isfinite(args->velocity[i]))
        {++rejected;return;}
    if(!std::isfinite(args->fparam1) || !std::isfinite(args->fparam2)){++rejected;return;}
    auto* ev=engine.pEventAPI;
    const bool local=ev->EV_IsLocal && ev->EV_IsLocal(args->entindex-1);
    const bool localView=local && WeaponPresentationAllowed(skull1::Id);
    float angles[3],forward[3],right[3],up[3];std::copy_n(args->angles,3,angles);
    angles[0]=float(double(angles[0])+double(args->iparam1)/100);
    angles[1]=float(double(angles[1])+double(args->iparam2)/100);
    engine.pfnAngleVectors(angles,forward,right,up);
    if(localView)
    {
        auto* model=engine.GetViewModel?engine.GetViewModel():nullptr;
        if(model)model->curstate.effects|=EF_MUZZLEFLASH;
        if(engine.pfnWeaponAnim)engine.pfnWeaponAnim(animation>=0?animation:
            (rapid?skull1::RapidShoot1:skull1::Shoot1)+Random(0,1),model?model->curstate.body:0);
        if(model)MuzzleSmoke(model->attachment[0],forward,args->velocity);
        shot=true;
    }
    // Original025625E0 computes shell information but never emits a shell.
    // Preserve its three discarded random draws: sound shares this engine
    // RNG stream. Omitting them changes later sound/effect choices.
    Random(50.f,90.f);Random(75.f,175.f);Random(25.f,150.f);
    // Its single sound RNG draw supplies both pitch and sample parity.
    const int random=Random(0,15);
    if(ev->EV_PlaySound)ev->EV_PlaySound(args->entindex,args->origin,CHAN_WEAPON,
        skull1::FireSounds[(random&1)?0:1],1,.6f,0,94+random);
    Bullet(*args,localView,forward,right,up);
    if(local)++localEffects;else ++remoteEffects;
    ++effects;
}
void FireEvent(event_args_t* args){PlayFire(args,false);}
void RapidEvent(event_args_t* args){PlayFire(args,true);}
}

void InitializeSkull1()
{
    InitializeGunSmoke();
    if(engine.pfnHookEvent){engine.pfnHookEvent(skull1::Event,FireEvent);engine.pfnHookEvent(skull1::RapidEvent,RapidEvent);}
    LoadMaterials();ResetSkull1();
}
void ResetSkull1()
{
    predictedClip=predictedReserve=-1;presentedModel=0;presentedAnimation=-1;presented=shot=false;emptySoundReady=true;
}
void ReceiveSkull1State(const WeaponCurrent& current)
{
    predictedClip=predictedReserve=-1;
    if(current.id!=skull1::Id || !current.state)ResetSkull1();
}
int Skull1PredictedAmmo(){return predictedClip;}
int Skull1PredictedReserve(){return predictedReserve;}
bool Skull1CrosshairShot(){const bool value=shot;shot=false;return value;}

bool PredictSkull1(local_state_t* from,local_state_t* to,usercmd_t* command,int runFunctions,double time,unsigned seed)
{
    const auto* definition=FindWeaponDefinition(skull1::Id);
    if(from->client.m_iId!=skull1::Id || from->weapondata[ActiveWeaponSlot].m_iId!=skull1::Id ||
        !definition || definition->name!=skull1::Classname)
    {if(runFunctions)ResetSkull1();return false;}
    ++predictionCalls;
    const auto source=from->client;auto weapon=from->weapondata[ActiveWeaponSlot];
    int shots=skull1::UnpackShots(weapon.fuser3);
    from->client.m_iId=0;downstream.HUD_PostRunCmd(from,to,command,runFunctions,time,seed);
    from->client.m_iId=skull1::Id;to->client.m_iId=skull1::Id;
    to->client.viewmodel=source.viewmodel;to->client.weaponanim=source.weaponanim;
    to->client.m_flNextAttack=source.m_flNextAttack;to->client.iuser3=source.iuser3;to->client.fov=source.fov;
    // Original 025D9341 -> ItemPostFrame -> 025D9A25 carries punch through
    // every prediction, including replay commands with runFunctions == 0.
    std::copy_n(source.punchangle,3,to->client.punchangle);
    std::copy_n(source.vuser4,3,to->client.vuser4);to->playerstate.oldbuttons=command->buttons;
    int animation=-1;bool fired=false,empty=false,rapid=false;float spread=0;
    const bool alive=source.health>0 && !source.deadflag;
    const bool predict=!engine.pfnGetCvarFloat || engine.pfnGetCvarFloat("cl_lw")!=0;
    const auto reload=[&]{
        if(weapon.m_iClip>=skull1::Clip || to->client.vuser4[1]<=0)return;
        weapon.m_fInReload=1;to->client.m_flNextAttack=skull1::ReloadTime;
        weapon.m_flTimeWeaponIdle=skull1::ReloadTime+.5f;animation=skull1::Reload;
        weapon.fuser1=skull1::Primary.accuracyMaximum;
    };
    if(predict && alive && source.m_flNextAttack<=0)
    {
        if(weapon.m_fInReload)
        {
            const int amount=std::max(0,std::min(skull1::Clip-weapon.m_iClip,int(to->client.vuser4[1])));
            weapon.m_iClip+=amount;to->client.vuser4[1]-=amount;weapon.m_fInReload=0;
        }
        const bool secondary=(command->buttons&IN_ATTACK2) && weapon.m_flNextSecondaryAttack<=0;
        const bool primary=(command->buttons&IN_ATTACK) && weapon.m_flNextPrimaryAttack<=0;
        const bool canShoot=(source.iuser3&1) && !(source.iuser3&2);
        if((secondary || primary) && canShoot)
        {
            rapid=secondary;weapon.m_iWeaponState=(weapon.m_iWeaponState&~1)|(rapid?1:0);
            weapon.fuser1=skull1::Parameters(rapid).accuracyMaximum;
            const float speed=std::hypot(source.velocity[0],source.velocity[1]);
            spread=skull1::Spread(rapid,(source.flags&FL_ONGROUND)!=0,speed,(source.flags&FL_DUCKING)!=0);
            if(skull1::TriggerAllowed(rapid,shots))
            {
                weapon.fuser1=skull1::AccuracyAfterAttempt(rapid,(weapon.m_iWeaponState&2)?1.f+weapon.fuser2:0.f,1.f);
                weapon.fuser2=0;weapon.m_iWeaponState|=2;
                if(weapon.m_iClip>0)
                {
                    --weapon.m_iClip;fired=true;
                    animation=(rapid?skull1::RapidShoot1:skull1::Shoot1)+(SharedSpread(seed+4)>=0?1:0);
                    weapon.m_flNextPrimaryAttack=weapon.m_flNextSecondaryAttack=skull1::Parameters(rapid).interval;
                    weapon.m_flTimeWeaponIdle=skull1::FireIdle;
                }
                else {empty=true;weapon.m_flNextPrimaryAttack=.2f;}
            }
        }
        else if((command->buttons&IN_RELOAD) && !weapon.m_fInReload && weapon.m_flNextPrimaryAttack<0)reload();
        else if(!(command->buttons&(IN_ATTACK|IN_ATTACK2)))
        {
            shots=0;if(runFunctions)emptySoundReady=true;
            if(!weapon.m_iClip && to->client.vuser4[1]>0 && weapon.m_flNextPrimaryAttack<0)reload();
            else if(weapon.m_iClip>0 && weapon.m_flTimeWeaponIdle<=0)
            {animation=skull1::Idle;weapon.m_flTimeWeaponIdle=skull1::IdleTime;}
        }
    }
    if(animation>=0)to->client.weaponanim=animation;
    if(runFunctions && alive)
    {
        auto* model=engine.GetViewModel?engine.GetViewModel():nullptr;const int sequence=to->client.weaponanim;
        // Original 025D9A4A also reconciles an already-reloading state.
        // Compare the last presented output, not just this command's events.
        const bool reconcile=animation>=0 || sequence!=presentedAnimation;
        if(!fired && sequence>=skull1::Idle && sequence<=skull1::Draw && engine.pfnWeaponAnim &&
            (reconcile || !presented || presentedModel!=source.viewmodel))
            ReconcileWeaponAnimation(source.m_iId,source.viewmodel,sequence,model?model->curstate.body:0,animation>=0);
        presented=true;presentedModel=source.viewmodel;presentedAnimation=sequence;
        const auto* local=engine.GetLocalPlayer?engine.GetLocalPlayer():nullptr;
        if(fired && local)
        {
            event_args_t args{};args.entindex=local->index;args.ducking=(source.flags&FL_DUCKING)!=0;
            std::copy_n(to->client.origin,3,args.origin);std::copy_n(command->viewangles,3,args.angles);
            std::copy_n(to->client.velocity,3,args.velocity);
            args.iparam1=int(source.punchangle[0]*100);args.iparam2=int(source.punchangle[1]*100);
            args.fparam1=(SharedSpread(seed)+SharedSpread(seed+1))*spread;
            args.fparam2=(SharedSpread(seed+2)+SharedSpread(seed+3))*spread;
            PlayFire(&args,rapid,animation);
        }
        if(empty && emptySoundReady && local && engine.pEventAPI && engine.pEventAPI->EV_PlaySound)
        {
            engine.pEventAPI->EV_PlaySound(local->index,to->client.origin,CHAN_WEAPON,"weapons/dryfire_pistol.wav",.8f,.8f,0,100);
            emptySoundReady=false;
        }
    }
    else if(runFunctions)presented=false;
    // Original Fire dispatches its event before applying recoil. Keep the
    // unmodified punch in the event and the new punch in prediction output.
    if(fired)
    {
        if(rapid)
        {
            std::array<int,6> random{};
            for(auto& value:random)value=ClientCRTRandom();
            const auto punch=skull1::RapidRecoil(skull1::Recoil(
                (source.flags&FL_ONGROUND)!=0,std::hypot(source.velocity[0],source.velocity[1])>0,
                (source.flags&FL_DUCKING)!=0),shots,{source.punchangle[0],source.punchangle[1]},random);
            to->client.punchangle[0]=punch[0];to->client.punchangle[1]=punch[1];
        }
        else to->client.punchangle[0]-=2;
    }
    const float dt=command->msec/1000.f;
    weapon.m_flNextPrimaryAttack=std::max(-.001f,weapon.m_flNextPrimaryAttack-dt);
    weapon.m_flNextSecondaryAttack=std::max(-.001f,weapon.m_flNextSecondaryAttack-dt);
    weapon.m_flTimeWeaponIdle=std::max(-.001f,weapon.m_flTimeWeaponIdle-dt);
    weapon.fuser2=std::max(-1.f,weapon.fuser2-dt);
    to->client.m_flNextAttack=std::max(-.001f,to->client.m_flNextAttack-dt);
    weapon.fuser3=skull1::PackShots(shots);to->weapondata[ActiveWeaponSlot]=weapon;
    if(runFunctions && predict){predictedClip=weapon.m_iClip;predictedReserve=int(to->client.vuser4[1]);}
    return true;
}
void Skull1Diagnostics(std::ostream& out)
{
    out<<",\"skull1\":{\"predictedClip\":"<<predictedClip<<",\"predictedReserve\":"<<predictedReserve
        <<",\"predictionCalls\":"<<predictionCalls<<",\"effects\":"<<effects
        <<",\"localEffects\":"<<localEffects<<",\"remoteEffects\":"<<remoteEffects
        <<",\"muzzleSmokes\":"<<muzzleSmokes<<",\"impacts\":"<<impacts<<",\"rejected\":"<<rejected<<'}';
}
}

namespace csol::client {
namespace {
constexpr int ModuleIds[]{skull1::Id};
constinit const WeaponModule Module{
    .ids=ModuleIds,.order=60,
    .initialize=InitializeSkull1,.reset=ResetSkull1,
    .videoInit=ResetSkull1,.receive=ReceiveSkull1State,
    .predict=PredictSkull1,.diagnostics=Skull1Diagnostics,
    .ammoKind=hud_style::AmmoKind::Magazine,
    .ammoSprite="sprites/csol/640hud2.spr",.ammoRect={25,49,231,255},
    .crosshair={8,3,0,7},.crosshairShot=Skull1CrosshairShot,
    .predictedClip=Skull1PredictedAmmo,
    .predictedReserve=Skull1PredictedReserve};
const WeaponModuleRegistration Registration{Module};
}
}
