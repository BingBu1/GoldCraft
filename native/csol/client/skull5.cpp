// SPDX-License-Identifier: GPL-3.0-or-later
#include "skull5.hpp"
#include "weapon_module.hpp"
#include "shared_random.hpp"
#include "weapon_inventory.hpp"
#include "gun_smoke.hpp"
#include "hud_style.hpp"
#include "sniper_scope.hpp"
#include "../skull5.hpp"
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
int scopeSprite{};
bool ownsScopeCenter{};
void ClearScopeCenter()
{
    if(!ownsScopeCenter)return;
    if(engine.pfnSetCrosshair)engine.pfnSetCrosshair(0,{},0,0,0);
    ownsScopeCenter=false;
}
void NativeWeaponChanged(int id)
{
    if(id!=skull5::Id)ClearScopeCenter();
}
void DrawScope(int hidden,int intermission,bool)
{
    if(ActiveWeaponHudId()!=skull5::Id || intermission || (hidden&69) ||
        view.health<=0 || view.spectator || WeaponFieldOfView()>=90 ||
        engine.pfnGetCvarFloat("hud_draw")==0)
    {ClearScopeCenter();return;}
    SCREENINFO screen{};screen.iSize=sizeof(screen);
    int gap{};
    // The archived CSOL scheme has an extended ABI. Use the host's actual
    // proportional metric rather than guessing that original v40's base.
    if(!engine.pfnGetScreenInfo(&screen) || !HudProportionalValue(10,gap))return;
    if(!scopeSprite && engine.pfnSPR_Load)scopeSprite=engine.pfnSPR_Load("sprites/csol/sniper_scope.spr");
    // Preserve the native engine's center projection and SPR alpha consumer.
    // Apply after the stock HUD, which suppresses unknown extended IDs.
    if(scopeSprite && engine.pfnSetCrosshair)
    {
        engine.pfnSetCrosshair(scopeSprite,{0,256,0,256},255,255,255);
        ownsScopeCenter=true;
    }
    DrawSniperScope(screen.iWidth,screen.iHeight,gap);
}
bool presented{}, shot{}, emptySoundReady=true;
unsigned predictionCalls{}, effects{}, localEffects{}, remoteEffects{}, muzzleSmokes{}, shells{}, impacts{}, rejected{};
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
    constexpr const char* rifle[]{"sprites/rifle_smoke1.spr","sprites/rifle_smoke2.spr","sprites/rifle_smoke3.spr"};
    constexpr const char* wall[]{"sprites/wall_puff1.spr","sprites/wall_puff2.spr","sprites/wall_puff3.spr","sprites/wall_puff4.spr"};
    constexpr float scales[]{.3f,.35f,.3f},speeds[]{3,35,70};
    constexpr byte shades[]{35,30,25};
    for(int layer=0;layer<3;++layer)
    {
        const char* path=layer==0?rifle[Random(0,2)]:wall[Random(0,3)];
        const int index=ev->EV_FindModelIndex(path);if(index<=0)continue;
        auto* temp=fx->R_DefaultSprite(origin,index,35);if(!temp)continue;
        temp->flags|=FTENT_CLIENTCUSTOM|FTENT_PERSIST|FTENT_COLLIDEWORLD;
        temp->entity.curstate.scale=scales[layer]+Random(0.f,scales[layer]*.5f);
        temp->entity.curstate.rendermode=kRenderTransAdd;
        temp->entity.curstate.renderamt=150;temp->entity.curstate.renderfx=kRenderFxFadeSlow;
        temp->entity.curstate.rendercolor={shades[layer],shades[layer],shades[layer]};
        temp->callback=UpdateSmoke;temp->hitcallback=HitSmoke;
        const float speed=speeds[layer]+Random(0,5);
        for(int i=0;i<3;++i)
            temp->entity.baseline.origin[i]=float(double(forward[i]*speed)+double(velocity[i])*(i==2?.5:.9));
        ++muzzleSmokes;
    }
}
void EjectShell(const event_args_t& args,bool local,const float* angles,
    const float* forward,const float* right,const float* up)
{
    auto* ev=engine.pEventAPI;auto* fx=engine.pEfxAPI;
    if(!fx || !fx->R_TempModel || !ev->EV_FindModelIndex)return;
    const int model=ev->EV_FindModelIndex(skull5::ShellModel);if(model<=0)return;
    float height[3];ViewHeight(local,args.ducking!=0,height);
    const bool rightHand=!engine.pfnGetCvarFloat || engine.pfnGetCvarFloat("cl_righthand")!=0;
    const float side=local?(rightHand?10.f:-10.f):4.f;
    const float ahead=local && rightHand?17.f:20.f,vertical=local?-8.f:-12.f;
    const float r=Random(50.f,90.f)*(side>0?-1.f:1.f),u=Random(75.f,175.f),f=Random(25.f,150.f);
    float origin[3],velocity[3],rotation[]{0,angles[1],0};
    for(int i=0;i<3;++i)
    {
        const float halfVelocity=args.velocity[i]*.5f;
        velocity[i]=halfVelocity+right[i]*r+up[i]*u+forward[i]*f;
        origin[i]=args.origin[i]+height[i]+halfVelocity/5.f+up[i]*vertical+forward[i]*ahead+right[i]*side;
        if(local)velocity[i]*=1.5f;
    }
    if(local)
    {
        velocity[2]-=50;
        if(const auto* view=engine.GetViewModel?engine.GetViewModel():nullptr)std::copy_n(view->attachment[1],3,origin);
    }
    if(auto* shell=fx->R_TempModel(origin,velocity,rotation,2,model,1))
    {
        for(float& value:shell->entity.baseline.angles)value*=17;
        shell->entity.index=0;++shells;
    }
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
    // Original client event uses visual bullet11 (power 39 doubled to 78,
    // distance limit 5000) and two traces. MP separately uses bullet19/8.
    float distance=8192;int power=78;
    for(int penetration=2;penetration>0;--penetration)
    {
        float end[3];for(int i=0;i<3;++i)end[i]=start[i]+direction[i]*distance;
        pmtrace_t trace{};ev->EV_PlayerTrace(start,end,PM_NORMAL,-1,&trace);
        if(trace.fraction>=1 || trace.allsolid || trace.startsolid)break;
        const auto* entity=ev->EV_GetPhysent(trace.ent);Impact(trace,entity);
        const float travelled=distance*trace.fraction;
        if(travelled>5000 || trace.hitgroup==8)break;
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
void PlayFire(event_args_t* args)
{
    if(!args || args->entindex<1 || args->entindex>32 || !engine.pEventAPI || !engine.pfnAngleVectors)
    {++rejected;return;}
    for(int i=0;i<3;++i)
        if(!std::isfinite(args->origin[i]) || !std::isfinite(args->angles[i]) || !std::isfinite(args->velocity[i]))
        {++rejected;return;}
    if(!std::isfinite(args->fparam1) || !std::isfinite(args->fparam2)){++rejected;return;}
    auto* ev=engine.pEventAPI;
    const bool local=ev->EV_IsLocal && ev->EV_IsLocal(args->entindex-1);
    const bool localView=local && WeaponPresentationAllowed(skull5::Id);
    float angles[3],forward[3],right[3],up[3];std::copy_n(args->angles,3,angles);
    // Original SKULL-5 constructs the bullet basis before adding the event's
    // punch angles. The adjusted yaw is subsequently used for shell rotation.
    engine.pfnAngleVectors(angles,forward,right,up);
    angles[0]=float(double(angles[0])+double(args->iparam1)/100);
    angles[1]=float(double(angles[1])+double(args->iparam2)/100);
    if(localView)
    {
        auto* model=engine.GetViewModel?engine.GetViewModel():nullptr;
        if(model)model->curstate.effects|=EF_MUZZLEFLASH;
        if(engine.pfnWeaponAnim)engine.pfnWeaponAnim(skull5::Shoot1,model?model->curstate.body:0);
        if(model)MuzzleSmoke(model->attachment[0],forward,args->velocity);
        shot=true;
    }
    EjectShell(*args,localView,angles,forward,right,up);
    if(ev->EV_PlaySound)ev->EV_PlaySound(args->entindex,args->origin,CHAN_WEAPON,
        skull5::FireSound,1,.4f,0,94+Random(0,15));
    Bullet(*args,localView,forward,right,up);
    if(local)++localEffects;else ++remoteEffects;
    ++effects;
}
void FireEvent(event_args_t* args){PlayFire(args);}
}

void InitializeSkull5()
{
    if(engine.pfnHookEvent)engine.pfnHookEvent(skull5::Event,FireEvent);
    InitializeGunSmoke();
    LoadMaterials();ResetSkull5();
}
void ResetSkull5()
{
    ClearScopeCenter();
    predictedClip=predictedReserve=-1;presentedModel=0;authoritativeAnimation=-1;
    presented=shot=false;emptySoundReady=true;
}
void ReceiveSkull5State(const WeaponCurrent& current)
{
    predictedClip=predictedReserve=-1;
    if(current.id!=skull5::Id || !current.state)ResetSkull5();
}
int Skull5PredictedAmmo(){return predictedClip;}
int Skull5PredictedReserve(){return predictedReserve;}
bool Skull5CrosshairShot(){const bool value=shot;shot=false;return value;}
void Skull5VideoInit(){ResetSkull5();scopeSprite=0;ResetSniperScope();}
void Skull5Shutdown(){ResetSkull5();scopeSprite=0;ResetSniperScope();}

bool PredictSkull5(local_state_t* from,local_state_t* to,usercmd_t* command,int runFunctions,double time,unsigned seed)
{
    const auto* definition=FindWeaponDefinition(skull5::Id);
    if(from->client.m_iId!=skull5::Id || from->weapondata[ActiveWeaponSlot].m_iId!=skull5::Id ||
        !definition || definition->name!=skull5::Classname)
    {if(runFunctions)ResetSkull5();return false;}
    ++predictionCalls;
    const auto source=from->client;auto weapon=from->weapondata[ActiveWeaponSlot];
    from->client.m_iId=0;downstream.HUD_PostRunCmd(from,to,command,runFunctions,time,seed);
    from->client.m_iId=skull5::Id;to->client.m_iId=skull5::Id;
    to->client.viewmodel=source.viewmodel;to->client.weaponanim=source.weaponanim;
    to->client.m_flNextAttack=source.m_flNextAttack;to->client.iuser3=source.iuser3;to->client.fov=source.fov;
    std::copy_n(source.vuser4,3,to->client.vuser4);to->playerstate.oldbuttons=command->buttons;
    int animation=-1;bool fired=false,empty=false,zoomChanged=false;float spread=0;
    const bool alive=source.health>0 && !source.deadflag;
    const bool predict=!engine.pfnGetCvarFloat || engine.pfnGetCvarFloat("cl_lw")!=0;
    const auto reload=[&]{
        if(weapon.m_iClip>=skull5::Clip || to->client.vuser4[1]<=0)return;
        weapon.m_fInReload=1;to->client.m_flNextAttack=skull5::ReloadTime;
        weapon.m_flTimeWeaponIdle=skull5::ReloadTime+.5f;animation=skull5::Reload;
        // Unzooming calls SecondaryAttack and resets accuracy. An unzoomed
        // reload leaves accuracy and last-fire time untouched in the original.
        if(source.fov!=skull5::NormalFov)
        {
            to->client.fov=skull5::NormalFov;weapon.fuser1=skull5::InitialAccuracy;
            weapon.m_flNextSecondaryAttack=skull5::ZoomTime;zoomChanged=true;
        }
    };
    if(predict && alive && source.m_flNextAttack<=0)
    {
        if(weapon.m_fInReload)
        {
            const int amount=std::max(0,std::min(skull5::Clip-weapon.m_iClip,int(to->client.vuser4[1])));
            weapon.m_iClip+=amount;to->client.vuser4[1]-=amount;weapon.m_fInReload=0;
        }
        const bool canShoot=(source.iuser3&1) && !(source.iuser3&2);
        if((command->buttons&IN_ATTACK2) && weapon.m_flNextSecondaryAttack<=0)
        {
            to->client.fov=skull5::NextFov(source.fov);weapon.fuser1=skull5::InitialAccuracy;
            weapon.m_flNextSecondaryAttack=skull5::ZoomTime;zoomChanged=true;
        }
        else if((command->buttons&IN_ATTACK) && weapon.m_flNextPrimaryAttack<=0 && canShoot)
        {
            const float speed=std::hypot(source.velocity[0],source.velocity[1]);
            const float base=skull5::SpreadBase((source.flags&FL_ONGROUND)!=0,speed,(source.flags&FL_DUCKING)!=0);
            const bool zoomed=source.fov!=skull5::NormalFov;
            weapon.fuser1=skull5::AccuracyAfterAttempt(weapon.fuser1,(weapon.m_iWeaponState&1)?2.f+weapon.fuser2:0.f,2.f);
            weapon.fuser2=0;weapon.m_iWeaponState|=1;
            spread=skull5::Spread(base,weapon.fuser1,zoomed);
            if(weapon.m_iClip>0)
            {
                --weapon.m_iClip;fired=true;animation=skull5::Shoot1;
                weapon.m_flNextPrimaryAttack=weapon.m_flNextSecondaryAttack=zoomed?skull5::ZoomInterval:skull5::Interval;
                weapon.m_flTimeWeaponIdle=skull5::FireIdle;
            }
            else {empty=true;weapon.m_flNextPrimaryAttack=skull5::EmptyInterval;}
        }
        else if((command->buttons&IN_RELOAD) && !weapon.m_fInReload && weapon.m_flNextPrimaryAttack<0)reload();
        else if(!(command->buttons&(IN_ATTACK|IN_ATTACK2)))
        {
            if(runFunctions)emptySoundReady=true;
            if(!weapon.m_iClip && to->client.vuser4[1]>0 && weapon.m_flNextPrimaryAttack<0)reload();
            else if(weapon.m_iClip>0 && weapon.m_flTimeWeaponIdle<=0)
            {animation=skull5::Idle;weapon.m_flTimeWeaponIdle=skull5::IdleTime;}
        }
    }
    if(animation>=0)to->client.weaponanim=animation;
    if(runFunctions && alive)
    {
        auto* model=engine.GetViewModel?engine.GetViewModel():nullptr;const int sequence=to->client.weaponanim;
        const bool reconcile=predict?animation>=0:source.weaponanim!=authoritativeAnimation;
        if(!fired && sequence>=skull5::Idle && sequence<=skull5::Draw && engine.pfnWeaponAnim &&
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
            args.bparam1=TRUE;PlayFire(&args);
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
    weapon.fuser2=std::max(-1.f,weapon.fuser2-dt);
    to->client.m_flNextAttack=std::max(-.001f,to->client.m_flNextAttack-dt);
    to->weapondata[ActiveWeaponSlot]=weapon;
    if(runFunctions && predict){predictedClip=weapon.m_iClip;predictedReserve=int(to->client.vuser4[1]);}
    return true;
}
void Skull5Diagnostics(std::ostream& out)
{
    out<<",\"skull5\":{\"predictedClip\":"<<predictedClip<<",\"predictedReserve\":"<<predictedReserve
        <<",\"predictionCalls\":"<<predictionCalls<<",\"effects\":"<<effects
        <<",\"localEffects\":"<<localEffects<<",\"remoteEffects\":"<<remoteEffects
        <<",\"muzzleSmokes\":"<<muzzleSmokes<<",\"shells\":"<<shells
        <<",\"impacts\":"<<impacts<<",\"rejected\":"<<rejected<<'}';
}
}

namespace csol::client {
namespace {
constexpr int ModuleIds[]{skull5::Id};
constinit const WeaponModule Module{
    .ids=ModuleIds,.order=70,
    .initialize=InitializeSkull5,.reset=ResetSkull5,
    .videoInit=Skull5VideoInit,.shutdown=Skull5Shutdown,
    .nativeWeaponChanged=NativeWeaponChanged,.receive=ReceiveSkull5State,
    .predict=PredictSkull5,.diagnostics=Skull5Diagnostics,
    .ammoKind=hud_style::AmmoKind::Magazine,
    .ammoSprite="sprites/csol/640hud7_v2.spr",.ammoRect={230,254,96,120},
    // Original record flags 0x18097: client 02A226D6 suppresses the
    // ordinary dynamic crosshair when bit 4 is set, including unzoomed.
    .dynamicCrosshair=false,
    .crosshair={6,4,0,3},.crosshairShot=Skull5CrosshairShot,
    .predictedClip=Skull5PredictedAmmo,
    .predictedReserve=Skull5PredictedReserve,.drawHud=DrawScope};
const WeaponModuleRegistration Registration{Module};
}
}
