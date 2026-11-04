// SPDX-License-Identifier: GPL-3.0-or-later
#include "balrog11.hpp"
#include "weapon_module.hpp"
#include "shared_random.hpp"
#include "weapon_inventory.hpp"
#include "../balrog11.hpp"
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
int predictedClip=-1, predictedReserve=-1, predictedCharges=-1, presentedModel{}, authoritativeAnimation=-1;
bool presented{}, shot{}, emptySoundReady=true;
unsigned predictionCalls{}, effects{}, localEffects{}, remoteEffects{}, shells{}, impacts{}, rejected{};


int Random(int low,int high){return engine.pfnRandomLong?engine.pfnRandomLong(low,high):low;}
float Random(float low,float high){return engine.pfnRandomFloat?engine.pfnRandomFloat(low,high):low;}

void ViewHeight(bool local,bool ducking,float* height)
{
    height[0]=height[1]=0;height[2]=ducking?12.f:28.f;
    if(local && engine.pEventAPI->EV_LocalPlayerViewheight)engine.pEventAPI->EV_LocalPlayerViewheight(height);
}
void EjectShell(const event_args_t& args,bool local,const float* forward,const float* right,const float* up)
{
    auto* ev=engine.pEventAPI;auto* fx=engine.pEfxAPI;
    if(!fx || !fx->R_TempModel || !ev->EV_FindModelIndex)return;
    const int model=ev->EV_FindModelIndex(args.bparam1?"models/shell_bcs.mdl":"models/shotgunshell.mdl");
    if(model<=0)return;
    float height[3];ViewHeight(local,args.ducking!=0,height);
    const float side=local?(engine.pfnGetCvarFloat && engine.pfnGetCvarFloat("cl_righthand")==0?-11.f:11.f):4.f;
    const float vertical=local?-9.f:-12.f;
    const float r=Random(50.f,90.f)*(side>0?-1.f:1.f), u=Random(75.f,175.f), f=Random(25.f,150.f);
    float origin[3],velocity[3],angles[]{0,args.angles[1],0};
    for(int i=0;i<3;++i)
    {
        const float halfVelocity=args.velocity[i]*.5f;
        velocity[i]=halfVelocity+(local?-1.f:1.f)*right[i]*r+up[i]*u+forward[i]*f;
        origin[i]=args.origin[i]+height[i]+halfVelocity/5.f+up[i]*vertical+forward[i]*(local?22.f:20.f)+right[i]*side;
        if(local)velocity[i]*=1.25f;
    }
    if(local)
    {
        velocity[2]-=50.f;
        if(const auto* model=engine.GetViewModel?engine.GetViewModel():nullptr)
            std::copy_n(model->attachment[1],3,origin);
    }
    if(fx->R_TempModel(origin,velocity,angles,2.f,model,2))++shells;
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
void Pellets(const event_args_t& args, bool local, const float* forward, const float* right, const float* up)
{
    auto* ev = engine.pEventAPI;
    if (!ev->EV_SetUpPlayerPrediction || !ev->EV_PushPMStates || !ev->EV_PopPMStates ||
        !ev->EV_SetSolidPlayers || !ev->EV_SetTraceHull || !ev->EV_PlayerTrace || !ev->EV_GetPhysent) return;
    float height[3], start[3];
    ViewHeight(local, args.ducking != 0, height);
    for (int i = 0; i < 3; ++i) start[i] = args.origin[i] + height[i];
    ev->EV_SetUpPlayerPrediction(0, 1);
    ev->EV_PushPMStates();
    ev->EV_SetSolidPlayers(args.entindex - 1);
    ev->EV_SetTraceHull(2);
    // The source CSO event sends6 in bparam2, but GoldSrc's installed event
    // delta only transmits one bit. These properties are fixed for Balrog11.
    for (int pellet = 0; pellet < balrog11::Pellets; ++pellet)
    {
        float x, y;
        do { x = Random(-.5f, .5f) + Random(-.5f, .5f); y = Random(-.5f, .5f) + Random(-.5f, .5f); }
        while (x * x + y * y > 1);
        float end[3];
        // Original event2579950 draws pellet traces to8192, independently
        // from the MP damage range property3148.
        for (int i = 0; i < 3; ++i)
            end[i] = start[i] + 8192 * (forward[i] + balrog11::Spread * (x * right[i] + y * up[i]));
        pmtrace_t trace{};
        ev->EV_PlayerTrace(start, end, PM_NORMAL, -1, &trace);
        if (trace.fraction < 1 && !trace.allsolid && !trace.startsolid)
            Impact(trace, ev->EV_GetPhysent(trace.ent));
    }
    ev->EV_PopPMStates();
}
void PlayFire(event_args_t* args, int animation = -1)
{
    if (!args || args->entindex < 1 || args->entindex > 32 || !engine.pEventAPI || !engine.pfnAngleVectors)
    { ++rejected; return; }
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(args->origin[i]) || !std::isfinite(args->angles[i]) || !std::isfinite(args->velocity[i]))
        { ++rejected; return; }
    if (!std::isfinite(args->fparam1) || !std::isfinite(args->fparam2)) { ++rejected; return; }
    auto* ev = engine.pEventAPI;
    const bool local = ev->EV_IsLocal && ev->EV_IsLocal(args->entindex - 1);
    const bool localView = local && WeaponPresentationAllowed(balrog11::Id);
    float angles[3], forward[3], right[3], up[3];
    std::copy_n(args->angles, 3, angles);
    angles[0] += args->iparam1 / 100.f;
    angles[1] += args->iparam2 / 100.f;
    engine.pfnAngleVectors(angles, forward, right, up);
    if (localView)
    {
        auto* model = engine.GetViewModel ? engine.GetViewModel() : nullptr;
        if (model) model->curstate.effects |= EF_MUZZLEFLASH;
        if (engine.pfnWeaponAnim)
            engine.pfnWeaponAnim(animation >= 0 ? animation : args->bparam1 ? balrog11::ShootFlame :
                balrog11::Shoot1 + Random(0, 1), model ? model->curstate.body : 0);
        shot = true;
    }
    EjectShell(*args, localView, forward, right, up);
    // The original primary sound is emitted by the MP. Only the flame sound
    // belongs to this event, avoiding a doubled primary sound on the owner.
    if (args->bparam1)
    {
        if (ev->EV_PlaySound) ev->EV_PlaySound(args->entindex, args->origin, CHAN_WEAPON,
            "weapons/balrog11-2.wav", args->fparam1, .52f, 0, 94 + Random(0, 15));
    }
    else Pellets(*args, localView, forward, right, up);
    if (local) ++localEffects; else ++remoteEffects;
    ++effects;
}
void FireEvent(event_args_t* args) { PlayFire(args); }
}

void InitializeBalrog11()
{
    if (engine.pfnHookEvent) engine.pfnHookEvent(balrog11::Event, FireEvent);
    ResetBalrog11();
}
void ResetBalrog11()
{
    predictedClip = predictedReserve = predictedCharges = -1;
    presentedModel = 0;
    authoritativeAnimation = -1;
    presented = shot = false;
    emptySoundReady = true;
}
void ReceiveBalrog11State(const WeaponCurrent& current)
{
    predictedClip = predictedReserve = predictedCharges = -1;
    if (current.id != balrog11::Id || !current.state) ResetBalrog11();
}
int Balrog11PredictedAmmo() { return predictedClip; }
int Balrog11PredictedReserve() { return predictedReserve; }
int Balrog11PredictedCharges() { return predictedCharges; }
bool Balrog11CrosshairShot() { const bool value = shot; shot = false; return value; }

bool PredictBalrog11(local_state_t* from, local_state_t* to, usercmd_t* command,
    int runFunctions, double time, unsigned seed)
{
    const auto* definition = FindWeaponDefinition(balrog11::Id);
    if (from->client.m_iId != balrog11::Id || from->weapondata[ActiveWeaponSlot].m_iId != balrog11::Id ||
        !definition || definition->name != balrog11::Classname)
    { if (runFunctions) ResetBalrog11(); return false; }
    ++predictionCalls;
    const auto source = from->client;
    auto weapon = from->weapondata[ActiveWeaponSlot];
    from->client.m_iId = 0;
    downstream.HUD_PostRunCmd(from, to, command, runFunctions, time, seed);
    from->client.m_iId = to->client.m_iId = balrog11::Id;
    to->client.viewmodel = source.viewmodel;
    to->client.weaponanim = source.weaponanim;
    to->client.m_flNextAttack = source.m_flNextAttack;
    to->client.iuser3 = source.iuser3;
    std::copy_n(source.vuser4, 3, to->client.vuser4);
    to->playerstate.oldbuttons = command->buttons;
    auto charge = balrog11::UnpackCharge(weapon.m_iWeaponState);
    int animation = -1;
    bool fired = false, secondary = false, empty = false;
    float volume = 1;
    const bool alive = source.health > 0 && !source.deadflag;
    const bool predict = !engine.pfnGetCvarFloat || engine.pfnGetCvarFloat("cl_lw") != 0;

    const auto reload = [&]
    {
        if (to->client.vuser4[1] <= 0 || weapon.m_iClip >= balrog11::Clip || weapon.m_flNextPrimaryAttack > 0) return;
        if (!weapon.m_fInSpecialReload)
        {
            animation = balrog11::StartReload;
            weapon.m_fInSpecialReload = 1;
            to->client.m_flNextAttack = balrog11::ReloadFirst;
            weapon.m_flTimeWeaponIdle = weapon.m_flNextPrimaryAttack = weapon.m_flNextSecondaryAttack = balrog11::ReloadFirst;
        }
        else if (weapon.m_fInSpecialReload == 1)
        {
            if (weapon.m_flTimeWeaponIdle > 0) return;
            animation = balrog11::Insert;
            weapon.m_fInSpecialReload = 2;
            weapon.m_flNextReload = weapon.m_flTimeWeaponIdle = balrog11::ReloadInsert;
        }
        else
        {
            ++weapon.m_iClip;
            --to->client.vuser4[1];
            weapon.m_fInSpecialReload = 1;
        }
    };
    if (predict && alive && source.m_flNextAttack <= 0)
    {
        const bool canShoot = (source.iuser3 & 1) && !(source.iuser3 & 2);
        if ((command->buttons & IN_ATTACK2) && weapon.m_flNextSecondaryAttack <= 0 && canShoot)
        {
            if (charge.ConsumeSecondary())
            {
                fired = secondary = true;
                animation = balrog11::ShootFlame;
                weapon.m_flNextPrimaryAttack = weapon.m_flNextSecondaryAttack = balrog11::SecondaryInterval;
                weapon.m_flTimeWeaponIdle = weapon.m_iClip ? balrog11::FireIdle : balrog11::EmptyIdle;
                weapon.m_fInSpecialReload = 0;
            }
        }
        else if ((command->buttons & IN_ATTACK) && weapon.m_flNextPrimaryAttack <= 0 && canShoot)
        {
            if (source.waterlevel == 3) { empty = true; weapon.m_flNextPrimaryAttack = .15f; }
            else if (weapon.m_iClip <= 0)
            {
                reload();
                empty = weapon.m_iClip == 0;
                weapon.m_flNextPrimaryAttack = 1;
            }
            else
            {
                --weapon.m_iClip;
                fired = true;
                animation = balrog11::Shoot1 + (SharedSpread(seed + 4) >= 0 ? 1 : 0);
                volume = charge.PrimaryFired() ? .2f : 1.f;
                weapon.m_flNextPrimaryAttack = weapon.m_flNextSecondaryAttack = balrog11::Interval;
                weapon.m_flTimeWeaponIdle = weapon.m_iClip ? balrog11::FireIdle : balrog11::EmptyIdle;
                weapon.m_fInSpecialReload = 0;
            }
        }
        else if ((command->buttons & IN_RELOAD) && !weapon.m_fInSpecialReload) reload();
        else if (!(command->buttons & (IN_ATTACK | IN_ATTACK2)))
        {
            charge.Idle();
            if (runFunctions) emptySoundReady = true;
            if (weapon.m_flTimeWeaponIdle < 0)
            {
                if ((!weapon.m_iClip || weapon.m_fInSpecialReload) && weapon.m_iClip < balrog11::Clip && to->client.vuser4[1] > 0)
                    reload();
                else if (weapon.m_fInSpecialReload)
                {
                    animation = balrog11::AfterReload;
                    weapon.m_fInSpecialReload = 0;
                    weapon.m_flTimeWeaponIdle = balrog11::ReloadFinish;
                }
                else { animation = balrog11::Idle; weapon.m_flTimeWeaponIdle = balrog11::IdleTime; }
            }
        }
    }
    if (animation >= 0) to->client.weaponanim = animation;
    if (runFunctions && alive)
    {
        auto* model = engine.GetViewModel ? engine.GetViewModel() : nullptr;
        const int sequence = to->client.weaponanim;
        const bool reconcile = predict ? animation >= 0 : source.weaponanim != authoritativeAnimation;
        if (!fired && sequence >= balrog11::Idle && sequence <= balrog11::Draw && engine.pfnWeaponAnim &&
            (reconcile || !presented || presentedModel != source.viewmodel))
            engine.pfnWeaponAnim(sequence, model ? model->curstate.body : 0);
        presented = true;
        presentedModel = source.viewmodel;
        authoritativeAnimation = source.weaponanim;
        const auto* local = engine.GetLocalPlayer ? engine.GetLocalPlayer() : nullptr;
        if (fired && local)
        {
            event_args_t args{};
            args.entindex = local->index;
            args.ducking = (source.flags & FL_DUCKING) != 0;
            std::copy_n(to->client.origin, 3, args.origin);
            std::copy_n(command->viewangles, 3, args.angles);
            std::copy_n(to->client.velocity, 3, args.velocity);
            args.iparam1 = 7;
            // Original weapon+0x1A4 defaults to zero. Player punch pitch
            // must not become the event's yaw offset.
            args.iparam2 = 0;
            args.bparam1 = secondary;
            args.bparam2 = 0;
            args.fparam1 = volume;
            args.fparam2 = balrog11::Spread;
            PlayFire(&args, animation);
        }
        if (empty && emptySoundReady && local && engine.pEventAPI && engine.pEventAPI->EV_PlaySound)
        {
            engine.pEventAPI->EV_PlaySound(local->index, to->client.origin, CHAN_WEAPON,
                "weapons/dryfire_rifle.wav", .8f, .8f, 0, 100);
            emptySoundReady = false;
        }
    }
    else if (runFunctions) presented = false;
    const float dt = command->msec / 1000.f;
    weapon.m_flNextPrimaryAttack = std::max(-.001f, weapon.m_flNextPrimaryAttack - dt);
    weapon.m_flNextSecondaryAttack = std::max(-.001f, weapon.m_flNextSecondaryAttack - dt);
    weapon.m_flTimeWeaponIdle = std::max(-.001f, weapon.m_flTimeWeaponIdle - dt);
    weapon.m_flNextReload = std::max(-.001f, weapon.m_flNextReload - dt);
    to->client.m_flNextAttack = std::max(-.001f, to->client.m_flNextAttack - dt);
    weapon.m_iWeaponState = balrog11::PackCharge(charge);
    to->weapondata[ActiveWeaponSlot] = weapon;
    if (runFunctions && predict)
    {
        predictedClip = weapon.m_iClip;
        predictedReserve = int(to->client.vuser4[1]);
        predictedCharges = charge.charges;
    }
    return true;
}
void Balrog11Diagnostics(std::ostream& out)
{
    out << ",\"balrog11\":{\"predictedClip\":" << predictedClip << ",\"predictedReserve\":" << predictedReserve
        << ",\"predictedCharges\":" << predictedCharges << ",\"predictionCalls\":" << predictionCalls
        << ",\"effects\":" << effects << ",\"localEffects\":" << localEffects << ",\"remoteEffects\":" << remoteEffects
        << ",\"shells\":" << shells << ",\"impacts\":" << impacts << ",\"rejected\":" << rejected << '}';
}
}

namespace csol::client {
namespace {
constexpr int ModuleIds[]{balrog11::Id};
constinit const WeaponModule Module{
    .ids=ModuleIds,.order=80,
    .initialize=InitializeBalrog11,.reset=ResetBalrog11,
    .videoInit=ResetBalrog11,.receive=ReceiveBalrog11State,
    .predict=PredictBalrog11,.diagnostics=Balrog11Diagnostics,
    .ammoKind=hud_style::AmmoKind::Magazine,
    .ammoSprite="sprites/csol/640hud3_v2.spr",.ammoRect={222,246,112,136},
    .crosshair={9,4,0,0},.crosshairShot=Balrog11CrosshairShot,
    .predictedClip=Balrog11PredictedAmmo,
    .predictedReserve=Balrog11PredictedReserve,
    .predictedSecondary=Balrog11PredictedCharges};
const WeaponModuleRegistration Registration{Module};
}
}
