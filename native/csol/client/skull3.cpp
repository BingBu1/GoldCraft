// SPDX-License-Identifier: GPL-3.0-or-later
#include "skull3.hpp"
#include "weapon_module.hpp"
#include "weapon_inventory.hpp"
#include "shared_random.hpp"
#include "gun_smoke.hpp"
#include "../skull3.hpp"
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

namespace csol::client {
namespace {
int predictedClip=-1,predictedReserve=-1,presentedId{},presentedModel{},authoritativeAnimation=-1;
bool presented{},shot{};
unsigned predictionCalls{},effects{},localEffects{},remoteEffects{},muzzleSmokes{},shells{},impacts{},rejected{};
// Original .data directions are1 and BSS magnitudes0; 0256F750 owns updates.
bool smokeWindDirection[2]{true,true};
float smokeWind[2]{};

int Random(int low,int high){return engine.pfnRandomLong?engine.pfnRandomLong(low,high):low;}
float Random(float low,float high){return engine.pfnRandomFloat?engine.pfnRandomFloat(low,high):low;}

void UpdateSmoke(TEMPENTITY* temp,float,float)
{
    // Original client 0256F6D0; type 3/4 chooses this non-drifting callback.
    if(temp->entity.curstate.frame<=7)return;
    for(float& value:temp->entity.baseline.origin)value=float(double(value)*.97);
    temp->entity.baseline.origin[2]=std::min(70.f,float(double(temp->entity.baseline.origin[2])+.7));
}
void HitSmoke(TEMPENTITY* temp,pmtrace_t* trace)
{
    // Original 0256EFE0 projects velocity onto the collision plane.
    if(!trace)return;
    const auto& v=temp->entity.baseline.origin;const auto& n=trace->plane.normal;
    const float length=float(std::sqrt(double(v[0]*v[0]+v[1]*v[1]+v[2]*v[2])));
    const float x=length!=0?v[0]/length:0,y=length!=0?v[1]/length:0,z=length!=0?v[2]/length:0;
    const float a=y*n[2]-z*n[1],b=z*n[0]-x*n[2],c=x*n[1]-y*n[0];
    const double speed=double(std::min(length,2000.f))*1.5;
    temp->entity.baseline.origin[0]=float(double(b*n[2]-c*n[1])*speed);
    temp->entity.baseline.origin[1]=float(double(c*n[0]-a*n[2])*speed);
    temp->entity.baseline.origin[2]=float(double(a*n[1]-b*n[0])*speed);
}
void UpdateWallSmoke(TEMPENTITY* temp,float frameTime,float time)
{
    UpdateSmoke(temp,frameTime,time);
    if(temp->entity.curstate.frame<=6)return;
    for(int i=0;i<2;++i)
    {
        smokeWind[i]=std::min(5.f,float(double(smokeWind[i])+.075));
        temp->entity.baseline.origin[i]+=smokeWindDirection[i]?smokeWind[i]:-smokeWind[i];
    }
    // Original changes Y before X, consuming one random value for each.
    for(int i=1;i>=0;--i)
        if(Random(0,10)==0 && smokeWind[i]>3)
        {smokeWind[i]=0;smokeWindDirection[i]=!smokeWindDirection[i];}
}
void WallSmoke(float* origin,const float* normal,float base,color24 color)
{
    auto* fx=engine.pEfxAPI;auto* ev=engine.pEventAPI;
    if(!fx || !fx->R_DefaultSprite || !ev->EV_FindModelIndex)return;
    constexpr const char* paths[]{"sprites/wall_puff1.spr","sprites/wall_puff2.spr",
        "sprites/wall_puff3.spr","sprites/wall_puff4.spr"};
    const int model=ev->EV_FindModelIndex(paths[Random(0,3)]);if(model<=0)return;
    auto* temp=fx->R_DefaultSprite(origin,model,35);if(!temp)return;
    temp->flags|=FTENT_CLIENTCUSTOM|FTENT_PERSIST|FTENT_COLLIDEWORLD;
    temp->entity.curstate.scale=base+Random(0.f,base*.5f);
    temp->entity.curstate.rendermode=kRenderTransAdd;
    temp->entity.curstate.renderamt=150;temp->entity.curstate.renderfx=kRenderFxFadeSlow;
    temp->entity.curstate.rendercolor=color;temp->callback=UpdateWallSmoke;temp->hitcallback=HitSmoke;
    const float speed=float(25+Random(0,5));
    if(!std::equal(origin,origin+3,normal))
        for(int i=0;i<3;++i)temp->entity.baseline.origin[i]=normal[i]*speed;
}
char Surface(pmtrace_t& trace,float* start,float* end)
{
    auto* ev=engine.pEventAPI;
    if(!ev->EV_IndexFromTrace || !ev->EV_TraceTexture)return 0;
    // Original world path uses the trace physent index for TraceTexture.
    // Host texture classification is supplied through its actual PM callback.
    if(ev->EV_IndexFromTrace(&trace)!=0)return 0;
    const char* texture=ev->EV_TraceTexture(trace.ent,start,end);if(!texture)return 0;
    if((texture[0]=='+' || texture[0]=='-') && texture[1])texture+=2;
    if(*texture=='{' || *texture=='!' || *texture=='~' || *texture==' ')++texture;
    char name[17]{};std::memcpy(name,texture,strnlen(texture,16));
    return downstream.HUD_PlayerMoveTexture?downstream.HUD_PlayerMoveTexture(name):'C';
}
void MaterialSound(pmtrace_t& trace,char material)
{
    if(!material || material=='Z' || !engine.pEventAPI->EV_PlaySound)return;
    constexpr const char* concrete[]{"player/pl_step1.wav","player/pl_step2.wav"};
    constexpr const char* metal[]{"player/pl_metal1.wav","player/pl_metal2.wav"};
    constexpr const char* dirt[]{"player/pl_dirt1.wav","player/pl_dirt2.wav","player/pl_dirt3.wav"};
    constexpr const char* duct[]{"player/pl_duct1.wav","player/pl_duct1.wav"};
    constexpr const char* grate[]{"player/pl_grate1.wav","player/pl_grate4.wav"};
    constexpr const char* tile[]{"player/pl_tile1.wav","player/pl_tile3.wav","player/pl_tile2.wav","player/pl_tile4.wav"};
    constexpr const char* slosh[]{"player/pl_slosh1.wav","player/pl_slosh3.wav","player/pl_slosh2.wav","player/pl_slosh4.wav"};
    constexpr const char* snow[]{"player/pl_snow1.wav","player/pl_snow2.wav","player/pl_snow3.wav","player/pl_snow4.wav"};
    constexpr const char* wood[]{"debris/wood1.wav","debris/wood2.wav","debris/wood3.wav"};
    constexpr const char* glass[]{"debris/glass1.wav","debris/glass2.wav","debris/glass3.wav"};
    constexpr const char* flesh[]{"weapons/bullet_hit1.wav","weapons/bullet_hit2.wav"};
    std::span<const char* const> samples=concrete;float volume=.9f,attenuation=.8f;
    switch(material)
    {
    case 'M':samples=metal;break;
    case 'D':samples=dirt;break;
    case 'V':samples=duct;volume=.5f;break;
    case 'G':samples=grate;break;
    case 'T':samples=tile;volume=.8f;break;
    case 'S':samples=slosh;break;
    case 'N':samples=snow;volume=.7f;break;
    case 'W':case 'I':samples=wood;break;
    case 'P':case 'Y':samples=glass;volume=.8f;break;
    case 'F':samples=flesh;volume=attenuation=1;break;
    default:break;
    }
    const int pitch=96+Random(0,15);
    const auto* sample=samples[Random(0,int(samples.size())-1)];
    engine.pEventAPI->EV_PlaySound(0,trace.endpos,6,sample,volume,attenuation,0,pitch);
}
void WallImpact(pmtrace_t& trace,const physent_t& entity,char material,bool streak,float scale,color24 color)
{
    auto* fx=engine.pEfxAPI;auto* ev=engine.pEventAPI;if(!fx)return;
    if(streak && fx->R_StreakSplash)
    {
        float direction[3];for(int i=0;i<3;++i)direction[i]=trace.plane.normal[i]*Random(4.f,10.f);
        const float speed=Random(4.f,10.f);const int count=Random(15,30);
        fx->R_StreakSplash(trace.endpos,direction,30,count,speed,-75,75);
    }
    char decal[16];
    if(entity.classnumber==1)std::snprintf(decal,sizeof(decal),"{break%d",1+Random(0,2));
    else if(entity.rendermode!=kRenderNormal)std::snprintf(decal,sizeof(decal),"{bproof1");
    else std::snprintf(decal,sizeof(decal),"{shot%d",1+Random(0,4));
    if(fx->R_BulletImpactParticles)fx->R_BulletImpactParticles(trace.endpos);
    const int ricochet=Random(0,32767);
    if(ricochet<16383 && ev->EV_PlaySound)
    {
        constexpr const char* standard[]{"weapons/ric1.wav","weapons/ric2.wav","weapons/ric3.wav",
            "weapons/ric4.wav","weapons/ric5.wav","weapons/ric_conc-1.wav"};
        const char* sample=material=='M'||material=='V'?
            (ricochet&1?"weapons/ric_metal-1.wav":"weapons/ric_metal-2.wav"):standard[ricochet%6];
        ev->EV_PlaySound(-1,trace.endpos,0,sample,1,.8f,0,100);
    }
    if(fx->Draw_DecalIndexFromName && fx->Draw_DecalIndex && fx->R_DecalShoot && ev->EV_IndexFromTrace &&
        engine.pfnGetCvarFloat && engine.pfnGetCvarFloat("r_decals")!=0)
        fx->R_DecalShoot(fx->Draw_DecalIndex(fx->Draw_DecalIndexFromName(decal)),ev->EV_IndexFromTrace(&trace),0,trace.endpos,0x800);
    float origin[3];for(int i=0;i<3;++i)origin[i]=trace.endpos[i]+trace.plane.normal[i]*5;
    WallSmoke(origin,trace.plane.normal,scale,color);++impacts;
}
void Bullet(const event_args_t& args,bool local,const float* forward,const float* right,const float* up)
{
    auto* ev=engine.pEventAPI;
    if(!ev->EV_SetUpPlayerPrediction || !ev->EV_PushPMStates || !ev->EV_PopPMStates ||
        !ev->EV_SetSolidPlayers || !ev->EV_SetTraceHull || !ev->EV_PlayerTrace || !ev->EV_GetPhysent)return;
    float height[]{0,0,args.ducking==1?12.f:17.f};
    if(local && ev->EV_LocalPlayerViewheight)ev->EV_LocalPlayerViewheight(height);
    float start[3],direction[3];
    for(int i=0;i<3;++i){start[i]=args.origin[i]+height[i];direction[i]=forward[i]+args.fparam1*right[i]+args.fparam2*up[i];}
    ev->EV_SetUpPlayerPrediction(0,1);ev->EV_PushPMStates();ev->EV_SetSolidPlayers(args.entindex-1);ev->EV_SetTraceHull(2);
    float distance=8192;int power=70,remaining=2;
    while(remaining>0)
    {
        float end[3];for(int i=0;i<3;++i)end[i]=start[i]+direction[i]*distance;
        pmtrace_t trace{};ev->EV_PlayerTrace(start,end,16,-1,&trace);
        const float travelled=trace.fraction*distance;
        // 02569FCF tests zero travel; it does not discard fraction1 here.
        if(travelled==0)break;
        remaining=travelled>4000?0:remaining-1;
        const auto* entity=ev->EV_GetPhysent(trace.ent);
        // Monster damage and blood use authoritative CsoMonFX. Original
        // model+1F8 classifications have no host ABI counterpart: do not
        // reinterpret studio hitgroups as brush sparks or duplicate blood.
        if(entity && !entity->player && !entity->studiomodel && entity->solid==SOLID_BSP)
        {
            const char material=Surface(trace,start,end);if(material=='Z')break;
            MaterialSound(trace,material);
            bool streak=false;float scale=.4f;color24 color{40,40,40};
            switch(material)
            {
            case 'M':power=int(double(power)*.15);streak=true;break;
            case 'C':power=int(double(power)*.25);streak=true;color={65,65,65};break;
            case 'G':case 'V':power=int(double(power)*.5);streak=true;break;
            case 'T':power=int(double(power)*.65);streak=true;break;
            case 'P':power=int(double(power)*.4);streak=true;break;
            case 'W':scale=.5f;color={75,42,15};break;
            case 'I':power=0;streak=true;WallSmoke(trace.endpos,trace.plane.normal,scale,color);break;
            default:break;
            }
            WallImpact(trace,*entity,material,streak,scale,color);
        }
        if(!remaining)break;
        for(int i=0;i<3;++i)start[i]=trace.endpos[i]+direction[i]*power;
        distance=(distance-travelled)*.5f;
    }
    ev->EV_PopPMStates();
}
void MuzzleSmoke(float* origin,const float* forward,const float* velocity,bool dual)
{
    auto* fx=engine.pEfxAPI;auto* ev=engine.pEventAPI;
    if(!fx || !fx->R_DefaultSprite || !ev->EV_FindModelIndex || !GunSmokeEnabled())return;
    constexpr const char* pistol[]{"sprites/pistol_smoke2.spr","sprites/pistol_smoke1.spr"};
    constexpr const char* rifle[]{"sprites/rifle_smoke1.spr","sprites/rifle_smoke2.spr","sprites/rifle_smoke3.spr"};
    const char* path=dual?rifle[Random(0,2)]:pistol[Random(0,1)];
    const int index=ev->EV_FindModelIndex(path);if(index<=0)return;
    auto* temp=fx->R_DefaultSprite(origin,index,35);if(!temp)return;
    const float base=dual?.35f:.25f;
    temp->flags|=FTENT_CLIENTCUSTOM|FTENT_PERSIST|FTENT_COLLIDEWORLD;
    temp->entity.curstate.scale=base+Random(0.f,base*.5f);
    temp->entity.curstate.rendermode=kRenderTransAdd;
    temp->entity.curstate.renderamt=150;temp->entity.curstate.renderfx=kRenderFxFadeSlow;
    const byte shade=dual?20:10;temp->entity.curstate.rendercolor={shade,shade,shade};
    temp->callback=UpdateSmoke;temp->hitcallback=HitSmoke;
    const float speed=float(3+Random(0,5));
    for(int i=0;i<3;++i)
        temp->entity.baseline.origin[i]=float(double(forward[i]*speed)+double(velocity[i])*(i==2?.5:.9));
    ++muzzleSmokes;
}
void EjectShell(const event_args_t& args,bool local,bool dual,bool left,const float* forward,
    const float* right,const float* up,float yaw,int modelIndex)
{
    auto* ev=engine.pEventAPI;auto* fx=engine.pEfxAPI;
    const bool rightHand=engine.pfnGetCvarFloat && engine.pfnGetCvarFloat("cl_righthand")!=0;
    const float side=local?(dual?10.f:rightHand?11.f:-11.f):4.f;
    const float r=Random(50.f,90.f)*(side>0?-1.f:1.f);
    const float u=Random(75.f,175.f),f=Random(25.f,150.f);
    float height[]{0,0,args.ducking==1?12.f:28.f};
    if(local && ev->EV_LocalPlayerViewheight)ev->EV_LocalPlayerViewheight(height);
    float origin[3],velocity[3],angles[]{0,yaw,0};
    for(int i=0;i<3;++i)
    {
        const float halfVelocity=args.velocity[i]*.5f;
        velocity[i]=halfVelocity+right[i]*r+up[i]*u+forward[i]*f;
        origin[i]=args.origin[i]+height[i]+halfVelocity/5.f+
            up[i]*(local?(dual?-8.f:-10.f):-12.f)+
            forward[i]*(local?(dual?20.f:34.f):20.f)+right[i]*side;
    }
    if(local)
    {
        velocity[2]-=dual?75.f:50.f;
        if(const auto* model=engine.GetViewModel?engine.GetViewModel():nullptr)
            std::copy_n(model->attachment[dual&&!left?2:0],3,origin);
    }
    if(fx && fx->R_TempModel && modelIndex>0)
        if(auto* temp=fx->R_TempModel(origin,velocity,angles,2.f,modelIndex,1))
        {
            for(float& value:temp->entity.baseline.angles)value*=dual?9:13;
            temp->entity.baseline.body=0;++shells;
        }
}

void PlayFire(event_args_t* args,bool dual,bool left=false)
{
    if(!args || args->entindex<1 || args->entindex>32 || !engine.pEventAPI || !engine.pfnAngleVectors)
    {++rejected;return;}
    for(int i=0;i<3;++i)
        if(!std::isfinite(args->origin[i]) || !std::isfinite(args->angles[i]) || !std::isfinite(args->velocity[i]))
        {++rejected;return;}
    if(!std::isfinite(args->fparam1) || !std::isfinite(args->fparam2)){++rejected;return;}
    auto* ev=engine.pEventAPI;
    const bool local=ev->EV_IsLocal && ev->EV_IsLocal(args->entindex-1);
    const bool localView=local && WeaponPresentationAllowed(dual?skull3::DualId:skull3::Id);
    float angles[3],forward[3],right[3],up[3];std::copy_n(args->angles,3,angles);
    // Original first converts the integer to float, then divides in double.
    angles[0]=float(double(angles[0])+double(float(args->iparam1))/100.);
    angles[1]=float(double(angles[1])+double(float(args->iparam2))/100.);
    engine.pfnAngleVectors(angles,forward,right,up);
    const int shell=ev->EV_FindModelIndex?ev->EV_FindModelIndex(dual?"models/rshell.mdl":"models/pshell.mdl"):0;
    if(localView)
    {
        auto* model=engine.GetViewModel?engine.GetViewModel():nullptr;
        if(model)model->curstate.effects|=EF_MUZZLEFLASH;
        // Original right event tests >1 although Fire supplies only 0/1.
        const int animation=!dual?3+Random(0,2):left?
            (args->bparam1?10:8+Random(0,1)):(args->bparam1>1?11+Random(0,1):13);
        if(engine.pfnWeaponAnim)engine.pfnWeaponAnim(animation,2);
        if(model)MuzzleSmoke(model->attachment[0],forward,args->velocity,dual);
        shot=true;
    }
    EjectShell(*args,localView,dual,left,forward,right,up,angles[1],shell);
    if(ev->EV_PlaySound)ev->EV_PlaySound(args->entindex,args->origin,CHAN_WEAPON,
        "weapons/skull3-1.wav",1,dual?.4f:.64f,0,94+Random(0,15));
    Bullet(*args,local,forward,right,up);
    if(local)++localEffects;else ++remoteEffects;
    ++effects;
}
void FireEvent(event_args_t* args){PlayFire(args,false);}
void FireLeftEvent(event_args_t* args){PlayFire(args,true,true);}
void FireRightEvent(event_args_t* args){PlayFire(args,true,false);}
}

void InitializeSkull3()
{
    if(engine.pfnHookEvent)
    {
        engine.pfnHookEvent(skull3::SingleEvent,FireEvent);
        engine.pfnHookEvent(skull3::LeftEvent,FireLeftEvent);
        engine.pfnHookEvent(skull3::RightEvent,FireRightEvent);
    }
    InitializeGunSmoke();ResetSkull3();
}
void ResetSkull3()
{
    predictedClip=predictedReserve=-1;presentedId=presentedModel=0;authoritativeAnimation=-1;
    presented=shot=false;
}
void ReceiveSkull3State(const WeaponCurrent& current)
{
    predictedClip=predictedReserve=-1;
    if(!skull3::IsWeapon(current.id) || !current.state)ResetSkull3();
}
int Skull3PredictedAmmo(){return predictedClip;}
int Skull3PredictedReserve(){return predictedReserve;}
bool Skull3CrosshairShot(){const bool value=shot;shot=false;return value;}
bool PredictSkull3(local_state_t* from,local_state_t* to,usercmd_t* command,int runFunctions,double time,unsigned seed)
{
    const int id=from->client.m_iId;
    const auto* definition=FindWeaponDefinition(id);
    const bool dual=id==skull3::DualId;
    if(!skull3::IsWeapon(id) || from->weapondata[ActiveWeaponSlot].m_iId!=id ||
        !definition || definition->name!=(dual?skull3::DualClassname:skull3::Classname))
    {if(runFunctions)ResetSkull3();return false;}
    ++predictionCalls;
    const auto source=from->client;auto weapon=from->weapondata[ActiveWeaponSlot];
    // Only the active entity occupies the host transport slot. Its peer's
    // magazine is owned by the server and is never pooled into this copy.
    int shots=skull3::UnpackShots(weapon.fuser3);
    from->client.m_iId=0;downstream.HUD_PostRunCmd(from,to,command,runFunctions,time,seed);
    from->client.m_iId=id;to->client.m_iId=id;
    to->client.viewmodel=source.viewmodel;to->client.weaponanim=source.weaponanim;
    to->client.m_flNextAttack=source.m_flNextAttack;to->client.iuser3=source.iuser3;to->client.fov=source.fov;
    std::copy_n(source.vuser4,3,to->client.vuser4);to->playerstate.oldbuttons=command->buttons;
    int animation=-1;bool fired=false,left=false;float spread=0;
    const bool alive=source.health>0 && !source.deadflag;
    const bool predict=!engine.pfnGetCvarFloat || engine.pfnGetCvarFloat("cl_lw")!=0;
    const auto& mode=skull3::Parameters(dual);
    const auto reload=[&]{
        if(weapon.m_iClip>=skull3::MaxClip(dual) || to->client.vuser4[1]<=0)return;
        weapon.m_fInReload=1;to->client.m_flNextAttack=mode.reloadTime;
        weapon.m_flTimeWeaponIdle=mode.reloadTime+.5f;animation=dual?14:1;
        weapon.fuser1=0;shots=0;
        // 0278C7B7 sets FOV10 before calling Secondary, whose only resulting
        // effects are FOV90 and its .3s secondary clock. There is no zoom sound.
        if(!dual && source.fov!=90){to->client.fov=90;weapon.m_flNextSecondaryAttack=.3f;}
    };
    if(predict && alive && source.m_flNextAttack<=0)
    {
        if(weapon.m_fInReload)
        {
            const int amount=std::max(0,std::min(skull3::MaxClip(dual)-weapon.m_iClip,int(to->client.vuser4[1])));
            weapon.m_iClip+=amount;to->client.vuser4[1]-=amount;weapon.m_fInReload=0;
        }
        const bool canShoot=(source.iuser3&1) && !(source.iuser3&2);
        // Original v2C4: A returns1, D returns0. ItemPostFrame 025D1B7B
        // masks unsupported secondary input before dispatch and idle checks.
        const int buttons=dual?command->buttons&~IN_ATTACK2:command->buttons;
        if((buttons&IN_ATTACK2) && weapon.m_flNextSecondaryAttack<=0)
        {
            if(!dual){to->client.fov=source.fov==90?64:90;weapon.m_flNextSecondaryAttack=.3f;}
        }
        else if((buttons&IN_ATTACK) && weapon.m_flNextPrimaryAttack<=0 && canShoot)
        {
            // Primary computes spread from the prior accuracy, then Fire
            // updates shots/accuracy before its empty-magazine check.
            spread=skull3::Spread(dual,weapon.fuser1,(source.flags&FL_ONGROUND)!=0);
            shots=std::min(shots,1000000)+1;weapon.m_iWeaponState|=skull3::DelayedFire;
            weapon.fuser1=skull3::AccuracyAfterTrigger(dual,shots);
            if(weapon.m_iClip>0)
            {
                --weapon.m_iClip;fired=true;
                // Bit1 initially set by an even-clip Deploy selects RIGHT.
                left=dual && !(weapon.m_iWeaponState&skull3::Left);
                if(dual)weapon.m_iWeaponState^=skull3::Left;
                weapon.m_flNextPrimaryAttack=weapon.m_flNextSecondaryAttack=mode.interval;
                weapon.m_flTimeWeaponIdle=skull3::FireIdle;
            }
            // Original predicted v288 is 025D1A20: returns0, no sound.
            else weapon.m_flNextPrimaryAttack=.2f;
        }
        else if((buttons&IN_RELOAD) && !weapon.m_fInReload && weapon.m_flNextPrimaryAttack<0)reload();
        else if(!(buttons&(IN_ATTACK|IN_ATTACK2)))
        {
            // Client 025D1FE1 differs from MP 115436EE: it adds4 below16,
            // clears delayFire and has no timed shots decay. Both weapon
            // metadata flags=0x201B, so v200/v204 do not clear the count.
            if(weapon.m_iWeaponState&skull3::DelayedFire)
            {
                weapon.m_iWeaponState&=~skull3::DelayedFire;
                if(shots<16)shots+=4;
            }
            if(!weapon.m_iClip && to->client.vuser4[1]>0 && weapon.m_flNextPrimaryAttack<0)reload();
            else if(weapon.m_flTimeWeaponIdle<=0 && (!dual || weapon.m_iClip>0))
            {animation=dual?(weapon.m_iClip==1?7:6):0;weapon.m_flTimeWeaponIdle=dual?60.f:20.f;}
        }
    }
    if(animation>=0)to->client.weaponanim=animation;
    if(runFunctions && alive)
    {
        const int sequence=to->client.weaponanim;
        const bool reconcile=predict?animation>=0:source.weaponanim!=authoritativeAnimation;
        if(!fired && sequence>=0 && sequence<=17 && engine.pfnWeaponAnim &&
            (reconcile || !presented || presentedId!=id || presentedModel!=source.viewmodel))
        {
            // SendWeaponAnim uses the weapon's body, unlike Fire's body2.
            // The host viewmodel carries the current character's body state.
            const auto* model=engine.GetViewModel?engine.GetViewModel():nullptr;
            engine.pfnWeaponAnim(sequence,model?model->curstate.body:0);
        }
        presented=true;presentedId=id;presentedModel=source.viewmodel;authoritativeAnimation=source.weaponanim;
        const auto* local=engine.GetLocalPlayer?engine.GetLocalPlayer():nullptr;
        if(fired && local)
        {
            event_args_t args{};args.entindex=local->index;args.ducking=(source.flags&FL_DUCKING)!=0;
            std::copy_n(to->client.origin,3,args.origin);std::copy_n(command->viewangles,3,args.angles);
            std::copy_n(to->client.velocity,3,args.velocity);
            args.iparam1=int(source.punchangle[0]*100);args.iparam2=int(source.punchangle[1]*100);
            args.fparam1=(SharedSpread(seed)+SharedSpread(seed+1))*spread;
            args.fparam2=(SharedSpread(seed+2)+SharedSpread(seed+3))*spread;
            args.bparam1=dual && weapon.m_iClip<=1;
            PlayFire(&args,dual,left);
        }
    }
    else if(runFunctions)presented=false;
    const float dt=command->msec/1000.f;
    weapon.m_flNextPrimaryAttack=std::max(-.001f,weapon.m_flNextPrimaryAttack-dt);
    weapon.m_flNextSecondaryAttack=std::max(-.001f,weapon.m_flNextSecondaryAttack-dt);
    weapon.m_flTimeWeaponIdle=std::max(-.001f,weapon.m_flTimeWeaponIdle-dt);
    weapon.fuser2=std::max(-.001f,weapon.fuser2-dt);
    to->client.m_flNextAttack=std::max(-.001f,to->client.m_flNextAttack-dt);
    weapon.fuser3=skull3::PackShots(shots);to->weapondata[ActiveWeaponSlot]=weapon;
    if(runFunctions && predict){predictedClip=weapon.m_iClip;predictedReserve=int(to->client.vuser4[1]);}
    return true;
}
void Skull3Diagnostics(std::ostream& out)
{
    out<<",\"skull3\":{\"predictedClip\":"<<predictedClip<<",\"predictedReserve\":"<<predictedReserve
        <<",\"predictionCalls\":"<<predictionCalls<<",\"effects\":"<<effects
        <<",\"localEffects\":"<<localEffects<<",\"remoteEffects\":"<<remoteEffects
        <<",\"muzzleSmokes\":"<<muzzleSmokes<<",\"shells\":"<<shells
        <<",\"impacts\":"<<impacts<<",\"rejected\":"<<rejected<<'}';
}
}

namespace csol::client {
namespace {
constexpr int SingleIds[]{skull3::Id},DualIds[]{skull3::DualId};
constinit const WeaponModule SingleModule{
    .ids=SingleIds,.order=66,
    .initialize=InitializeSkull3,.reset=ResetSkull3,
    .videoInit=ResetSkull3,.receive=ReceiveSkull3State,
    .predict=PredictSkull3,.diagnostics=Skull3Diagnostics,
    .ammoKind=hud_style::AmmoKind::Magazine,
    .ammoSprite="sprites/csol/640hud2.spr",.ammoRect={49,73,231,255},
    .crosshair={6,3,0,1},.crosshairShot=Skull3CrosshairShot,
    .predictedClip=Skull3PredictedAmmo,.predictedReserve=Skull3PredictedReserve};
// The peer has separate presentation metadata but shares one event/prediction
// lifecycle. Registering those callbacks twice would replay each frame twice.
constinit const WeaponModule DualModule{
    .ids=DualIds,.order=67,
    .ammoKind=hud_style::AmmoKind::Magazine,
    .ammoSprite="sprites/csol/640hud2.spr",.ammoRect={49,73,231,255},
    .crosshair={6,3,195,3},.crosshairShot=Skull3CrosshairShot,
    .predictedClip=Skull3PredictedAmmo,.predictedReserve=Skull3PredictedReserve};
const WeaponModuleRegistration SingleRegistration{SingleModule},DualRegistration{DualModule};
}
}
