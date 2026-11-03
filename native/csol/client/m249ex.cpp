// SPDX-License-Identifier: GPL-3.0-or-later
#include "m249ex.hpp"
#include "weapon_module.hpp"
#include "shared_random.hpp"
#include "weapon_inventory.hpp"
#include "../m249ex.hpp"
#include "../skull_ballistics.hpp"
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
int predictedClip=-1, predictedReserve=-1, presentedModel{}, authoritativeAnimation=-1;
bool presented{}, shot{}, emptySoundReady=true;
unsigned predictionCalls{}, effects{}, localEffects{}, remoteEffects{}, shells{}, impacts{}, rejected{};
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
void EjectShell(const event_args_t& args,bool local,const float* forward,const float* right,const float* up)
{
    auto* ev=engine.pEventAPI;auto* fx=engine.pEfxAPI;
    if(!fx || !fx->R_TempModel || !ev->EV_FindModelIndex)return;
    const int model=ev->EV_FindModelIndex(m249ex::ShellModel);
    if(model<=0)return;
    float height[3];ViewHeight(local,args.ducking!=0,height);
    const float side=local?(engine.pfnGetCvarFloat && engine.pfnGetCvarFloat("cl_righthand")==0?-13.f:13.f):4.f;
    const float vertical=local?-10.f:-12.f;
    const float r=Random(50.f,90.f)*(side>0?-1.f:1.f), u=Random(75.f,175.f), f=Random(25.f,150.f);
    float origin[3],velocity[3],angles[]{0,args.angles[1],0};
    for(int i=0;i<3;++i)
    {
        const float halfVelocity=args.velocity[i]*.5f;
        velocity[i]=halfVelocity+(local?-1.f:1.f)*right[i]*r+up[i]*u+forward[i]*f;
        origin[i]=args.origin[i]+height[i]+halfVelocity/5.f+up[i]*vertical+forward[i]*20.f+right[i]*side;
        if(local)velocity[i]*=1.2f;
    }
    if(local)
    {
        velocity[2]-=75.f;
        if(const auto* model=engine.GetViewModel?engine.GetViewModel():nullptr)
            std::copy_n(model->attachment[1],3,origin);
    }
    if(fx->R_TempModel(origin,velocity,angles,2.f,model,1))++shells;
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
    float distance=m249ex::Distance;int power=csol::skull_ballistics::PenetrationPower;
    for(int penetration=csol::m249ex::Penetration;penetration>0;--penetration)
    {
        float end[3];for(int i=0;i<3;++i)end[i]=start[i]+direction[i]*distance;
        pmtrace_t trace{};ev->EV_PlayerTrace(start,end,PM_NORMAL,-1,&trace);
        if(trace.fraction>=1 || trace.allsolid || trace.startsolid)break;
        const auto* entity=ev->EV_GetPhysent(trace.ent);
        Impact(trace,entity);
        const float travelled=distance*trace.fraction;
        if(travelled>csol::skull_ballistics::PenetrationDistance || trace.hitgroup==8)break;
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
void PlayFire(event_args_t* args,int animation=-1)
{
    if(!args || args->entindex<1 || args->entindex>32 || !engine.pEventAPI || !engine.pfnAngleVectors)
    {++rejected;return;}
    for(int i=0;i<3;++i)if(!std::isfinite(args->origin[i]) || !std::isfinite(args->angles[i]) || !std::isfinite(args->velocity[i]))
    {++rejected;return;}
    if(!std::isfinite(args->fparam1) || !std::isfinite(args->fparam2)){++rejected;return;}
    auto* ev=engine.pEventAPI;
    const bool local=ev->EV_IsLocal && ev->EV_IsLocal(args->entindex-1);
    const bool localView=local && WeaponPresentationAllowed(m249ex::Id);
    float angles[3],forward[3],right[3],up[3];std::copy_n(args->angles,3,angles);
    angles[0]+=args->iparam1/100.f;angles[1]+=args->iparam2/100.f;
    engine.pfnAngleVectors(angles,forward,right,up);
    if(localView)
    {
        auto* model=engine.GetViewModel?engine.GetViewModel():nullptr;
        if(model)model->curstate.effects|=EF_MUZZLEFLASH;
        // Preserve character/hand bodygroups; CSO's fixed body 2 cannot be
        // copied into this engine's remapped character hand representation.
        if(engine.pfnWeaponAnim)engine.pfnWeaponAnim(animation>=0?animation:m249ex::Shoot1+Random(0,1),model?model->curstate.body:0);
        shot=true;
    }
    if(local)++localEffects;
    else ++remoteEffects;
    // A late server shot still exists in the world, but its shell must not
    // originate at the newly selected weapon's viewmodel attachment.
    EjectShell(*args,localView,forward,right,up);
    if(ev->EV_PlaySound)ev->EV_PlaySound(args->entindex,args->origin,CHAN_WEAPON,m249ex::FireSound,1,.52f,0,94+Random(0,15));
    Bullet(*args,localView,forward,right,up);++effects;
}
void FireEvent(event_args_t* args){PlayFire(args);}
}

void InitializeM249EX()
{
    if(engine.pfnHookEvent)engine.pfnHookEvent(m249ex::Event,FireEvent);
    LoadMaterials();ResetM249EX();
}
void ResetM249EX()
{
    predictedClip=predictedReserve=-1;presentedModel=0;authoritativeAnimation=-1;presented=shot=false;emptySoundReady=true;
}
void ReceiveM249EXState(const WeaponCurrent& current)
{
    predictedClip=predictedReserve=-1;
    if(current.id!=m249ex::Id || !current.state)ResetM249EX();
}
int M249EXPredictedAmmo(){return predictedClip;}
int M249EXPredictedReserve(){return predictedReserve;}
bool M249EXCrosshairShot(){const bool value=shot;shot=false;return value;}

bool PredictM249EX(local_state_t* from,local_state_t* to,usercmd_t* command,int runFunctions,double time,unsigned seed)
{
    const auto* definition=FindWeaponDefinition(m249ex::Id);
    if(from->client.m_iId!=m249ex::Id || from->weapondata[ActiveWeaponSlot].m_iId!=m249ex::Id ||
        !definition || definition->name!=m249ex::Classname)
    {if(runFunctions)ResetM249EX();return false;}
    ++predictionCalls;
    const auto source=from->client;auto weapon=from->weapondata[ActiveWeaponSlot];
    int shots=m249ex::UnpackShots(weapon.fuser3);
    from->client.m_iId=0;
    downstream.HUD_PostRunCmd(from,to,command,runFunctions,time,seed);
    from->client.m_iId=m249ex::Id;to->client.m_iId=m249ex::Id;
    to->client.viewmodel=source.viewmodel;to->client.weaponanim=source.weaponanim;
    to->client.m_flNextAttack=source.m_flNextAttack;to->client.iuser3=source.iuser3;to->client.fov=source.fov;
    std::copy_n(source.vuser4,3,to->client.vuser4);to->playerstate.oldbuttons=command->buttons;
    int animation=-1;bool fired=false,empty=false;
    float spread=0;bool zoomChanged=false;
    const bool alive=source.health>0 && !source.deadflag;
    const bool predict=!engine.pfnGetCvarFloat || engine.pfnGetCvarFloat("cl_lw")!=0;
    if(predict && alive && source.m_flNextAttack<=0)
    {
        if(weapon.m_fInReload)
        {
            const int amount=std::max(0,std::min(m249ex::Clip-weapon.m_iClip,int(to->client.vuser4[1])));
            weapon.m_iClip+=amount;to->client.vuser4[1]-=amount;weapon.m_fInReload=0;
        }
        const bool canShoot=(source.iuser3&1) && !(source.iuser3&2);
        if((command->buttons&IN_ATTACK2) && weapon.m_flNextSecondaryAttack<=0)
        {
            to->client.fov=source.fov==m249ex::NormalFov?m249ex::ZoomFov:m249ex::NormalFov;
            weapon.m_flNextSecondaryAttack=m249ex::ZoomTime;zoomChanged=true;
        }
        else if((command->buttons&IN_ATTACK) && weapon.m_flNextPrimaryAttack<=0 && canShoot)
        {
            const float speed=std::hypot(source.velocity[0],source.velocity[1]);
            spread=m249ex::Spread(weapon.fuser1,(source.flags&FL_ONGROUND)!=0,
                speed,source.fov==m249ex::ZoomFov,(source.flags&FL_DUCKING)!=0);
            weapon.m_iWeaponState|=1;
            // Only five shots are required to reach the accuracy cap. Keep
            // the server count, but compute without a large integer cube.
            shots=std::min(shots,1000000)+1;
            weapon.fuser1=m249ex::AccuracyAfterTrigger(shots);
            if(weapon.m_iClip>0)
            {
                --weapon.m_iClip;fired=true;animation=m249ex::Shoot1+(SharedSpread(seed+4)>=0?1:0);
                weapon.m_flNextPrimaryAttack=weapon.m_flNextSecondaryAttack=
                    source.fov==m249ex::ZoomFov?m249ex::ZoomInterval:m249ex::Interval;
                weapon.m_flTimeWeaponIdle=m249ex::FireIdle;
            }
            else {empty=true;weapon.m_flNextPrimaryAttack=m249ex::EmptyInterval;}
        }
        else if((command->buttons&IN_RELOAD) && !weapon.m_fInReload && weapon.m_flNextPrimaryAttack<0)
        {
            if(weapon.m_iClip<m249ex::Clip && to->client.vuser4[1]>0)
            {
                weapon.m_fInReload=1;to->client.m_flNextAttack=m249ex::ReloadTime;
                weapon.m_flTimeWeaponIdle=m249ex::ReloadTime+.5f;
                weapon.fuser1=m249ex::InitialAccuracy;shots=0;weapon.m_iWeaponState&=~1;animation=m249ex::Reload;to->client.fov=m249ex::NormalFov;
            }
        }
        else if(!(command->buttons&(IN_ATTACK|IN_ATTACK2)))
        {
            if(runFunctions)emptySoundReady=true;
            if(weapon.m_iWeaponState&1){weapon.m_iWeaponState&=~1;shots=std::min(15,shots);weapon.fuser2=.4f;}
            if(shots>0 && weapon.fuser2<0)
            {weapon.fuser2=.0225f;--shots;}
            if(!weapon.m_iClip && to->client.vuser4[1]>0 && weapon.m_flNextPrimaryAttack<0)
            {
                weapon.m_fInReload=1;to->client.m_flNextAttack=m249ex::ReloadTime;
                weapon.m_flTimeWeaponIdle=m249ex::ReloadTime+.5f;
                weapon.fuser1=m249ex::InitialAccuracy;shots=0;weapon.m_iWeaponState&=~1;animation=m249ex::Reload;to->client.fov=m249ex::NormalFov;
            }
            else if(weapon.m_flTimeWeaponIdle<=0){animation=m249ex::Idle;weapon.m_flTimeWeaponIdle=m249ex::IdleTime;}
        }
    }
    if(animation>=0)to->client.weaponanim=animation;
    if(runFunctions && alive)
    {
        auto* model=engine.GetViewModel?engine.GetViewModel():nullptr;
        const int sequence=to->client.weaponanim;
        // WeaponAnim updates the engine's weapon sequence immediately, but
        // the viewmodel's render state is committed later. Comparing that
        // stale state here restarts an attack on intervening user commands.
        const bool reconcile=predict?animation>=0:
            source.weaponanim!=authoritativeAnimation;
        if(!fired && sequence>=m249ex::Idle && sequence<=m249ex::Draw && engine.pfnWeaponAnim &&
            (reconcile || !presented || presentedModel!=source.viewmodel))
            engine.pfnWeaponAnim(sequence,model?model->curstate.body:0);
        presented=true;presentedModel=source.viewmodel;authoritativeAnimation=source.weaponanim;
        const auto* local=engine.GetLocalPlayer?engine.GetLocalPlayer():nullptr;
        if(fired && local)
        {
            event_args_t args{};args.entindex=local->index;args.ducking=(source.flags&FL_DUCKING)!=0;
            std::copy_n(to->client.origin,3,args.origin);std::copy_n(command->viewangles,3,args.angles);
            std::copy_n(to->client.velocity,3,args.velocity);
            args.iparam1=int(source.punchangle[0]*100);args.iparam2=int(source.punchangle[1]*100);
            args.fparam1=(SharedSpread(seed)+SharedSpread(seed+1))*spread;
            args.fparam2=(SharedSpread(seed+2)+SharedSpread(seed+3))*spread;
            PlayFire(&args,animation);
        }
        if(zoomChanged && local && engine.pEventAPI && engine.pEventAPI->EV_PlaySound)
            engine.pEventAPI->EV_PlaySound(local->index,to->client.origin,CHAN_ITEM,"weapons/zoom.wav",.2f,2.4f,0,100);
        if(empty && emptySoundReady && local && engine.pEventAPI && engine.pEventAPI->EV_PlaySound)
        {
            engine.pEventAPI->EV_PlaySound(local->index,to->client.origin,CHAN_WEAPON,"weapons/dryfire_rifle.wav",.8f,.8f,0,100);
            emptySoundReady=false;
        }
    }
    else if(runFunctions)presented=false;
    const float dt=command->msec/1000.f;
    weapon.m_flNextPrimaryAttack=std::max(-.001f,weapon.m_flNextPrimaryAttack-dt);
    weapon.m_flNextSecondaryAttack=std::max(-.001f,weapon.m_flNextSecondaryAttack-dt);
    weapon.m_flTimeWeaponIdle=std::max(-.001f,weapon.m_flTimeWeaponIdle-dt);
    weapon.fuser2=std::max(-.001f,weapon.fuser2-dt);
    to->client.m_flNextAttack=std::max(-.001f,to->client.m_flNextAttack-dt);
    weapon.fuser3=m249ex::PackShots(shots);
    to->weapondata[ActiveWeaponSlot]=weapon;
    if(runFunctions && predict){predictedClip=weapon.m_iClip;predictedReserve=int(to->client.vuser4[1]);}
    return true;
}
void M249EXDiagnostics(std::ostream& out)
{
    out<<",\"m249ex\":{\"predictedClip\":"<<predictedClip<<",\"predictedReserve\":"<<predictedReserve
        <<",\"predictionCalls\":"<<predictionCalls<<",\"effects\":"<<effects
        <<",\"localEffects\":"<<localEffects<<",\"remoteEffects\":"<<remoteEffects
        <<",\"shells\":"<<shells<<",\"impacts\":"<<impacts<<",\"rejected\":"<<rejected<<'}';
}
}

namespace csol::client {
namespace {
constexpr int ModuleIds[]{m249ex::Id};
constinit const WeaponModule Module{
    .ids=ModuleIds,.order=50,
    .initialize=InitializeM249EX,.reset=ResetM249EX,
    .videoInit=ResetM249EX,.receive=ReceiveM249EXState,
    .predict=PredictM249EX,.diagnostics=M249EXDiagnostics,
    .ammoKind=hud_style::AmmoKind::Magazine,
    .ammoSprite="sprites/csol/640hud7_v2.spr",.ammoRect={230,254,96,120},
    .crosshair={6,3,195,3},.crosshairShot=M249EXCrosshairShot,
    .predictedClip=M249EXPredictedAmmo,
    .predictedReserve=M249EXPredictedReserve};
const WeaponModuleRegistration Registration{Module};
}
}
