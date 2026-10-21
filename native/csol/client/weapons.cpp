// SPDX-License-Identifier: GPL-3.0-or-later
#include "weapons.hpp"
#include <usercmd.h>
#include <in_buttons.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ostream>

namespace csol::client
{
namespace
{
constexpr int KnifeId = 2;
constexpr int BlueR = 114, BlueG = 197, BlueB = 255;
bool registered{}, drawingNative{};
int active{}, clip{}, ammoType=-1, reserve{}, hidden{};
int predictedClip=-1, predictedTotal=-1;
int hudDraws{}, predictionCalls{}, postRunCalls{}, sourceWeaponId{};
SCREENINFO screen{};
struct Sprite { int handle{}; wrect_t rect{}; } digits[10], ammoIcon;
int currentSprite{};
pfnUserMsgHook oldWeaponList{}, oldCurWeapon{}, oldAmmo{}, oldHide{}, oldReset{};
decltype(engine.pfnSPR_Set) originalSet{};
decltype(engine.pfnSPR_DrawAdditive) originalDraw{};
decltype(engine.pfnFillRGBA) originalFill{};

bool Custom() { return registered && active==KnifeId; }
int AmmoTotal()
{
    if(predictedTotal>=0 && engine.pfnGetCvarFloat("cl_lw")!=0)return predictedTotal;
    return std::max(0,clip)+std::max(0,reserve);
}
bool AmmoArea(int x,int y) { return drawingNative && Custom() && x>=screen.iWidth-200 && y>=screen.iHeight-48; }
void SetSprite(int sprite,int r,int g,int b)
{
    currentSprite=sprite; originalSet(sprite,r,g,b);
}
void DrawSprite(int frame,int x,int y,const wrect_t* rect)
{
    bool digit=false;for(const auto& item:digits)if(item.handle==currentSprite)digit=true;
    if(AmmoArea(x,y) && (digit || currentSprite==ammoIcon.handle))return;
    originalDraw(frame,x,y,rect);
}
void Fill(int x,int y,int width,int height,int r,int g,int b,int a)
{
    if(AmmoArea(x,y) && width<=4 && height<=32)return;
    originalFill(x,y,width,height,r,g,b,a);
}
int WeaponList(const char* name,int size,void* data)
{
    // Sign-on messages can precede HUD_Frame on a fast local connection.
    // Retire the previous session before accepting this server's registration.
    SynchronizeSession();
    if(data && size>0)
    {
        const auto* bytes=static_cast<const unsigned char*>(data);
        const auto* end=static_cast<const unsigned char*>(std::memchr(data,0,size));
        if(end && end+9<=bytes+size && end[7]==KnifeId)
        {
            registered=std::strcmp(static_cast<const char*>(data),"weapon_tknifeex2")==0;
            ammoType=registered?end[1]:-1;
        }
    }
    return oldWeaponList?oldWeaponList(name,size,data):1;
}
int CurWeapon(const char* name,int size,void* data)
{
    if(data && size==3)
    {
        const auto* b=static_cast<const unsigned char*>(data);
        if(b[0]) {active=b[1];clip=static_cast<signed char>(b[2]);predictedClip=predictedTotal=-1;}
        if(b[1]==255){active=0;predictedClip=predictedTotal=-1;}
    }
    return oldCurWeapon?oldCurWeapon(name,size,data):1;
}
int Ammo(const char* name,int size,void* data)
{
    if(data && size==2 && static_cast<unsigned char*>(data)[0]==ammoType)
    {
        reserve=static_cast<unsigned char*>(data)[1];
        predictedTotal=-1;
    }
    return oldAmmo?oldAmmo(name,size,data):1;
}
int Hide(const char* name,int size,void* data)
{
    if(data && size==1)hidden=*static_cast<unsigned char*>(data);
    return oldHide?oldHide(name,size,data):1;
}
int Reset(const char* name,int size,void* data)
{
    ResetWeapons(false);
    return oldReset?oldReset(name,size,data):1;
}
void LoadSprites()
{
    screen.iSize=sizeof(screen);engine.pfnGetScreenInfo(&screen);
    int count=0;
    auto* list=engine.pfnSPR_GetList("sprites/hud.txt",&count);
    const int resolution=screen.iWidth<640?320:640;
    for(int i=0;list && i<count;++i)
    {
        if(list[i].iRes!=resolution)continue;
        for(int digit=0;digit<10;++digit)
        {
            char name[24];std::snprintf(name,sizeof(name),"number_%d",digit);
            if(std::strcmp(list[i].szName,name))continue;
            char path[160];std::snprintf(path,sizeof(path),"sprites/%s.spr",list[i].szSprite);
            digits[digit]={engine.pfnSPR_Load(path),list[i].rc};
        }
    }
    ammoIcon={engine.pfnSPR_Load("sprites/csol/640hud12.spr"),{49,73,231,255}};
}
}

void InitializeWeapons()
{
    oldWeaponList=api->HookUserMsg("WeaponList",WeaponList);
    oldCurWeapon=api->HookUserMsg("CurWeapon",CurWeapon);
    oldAmmo=api->HookUserMsg("AmmoX",Ammo);
    oldHide=api->HookUserMsg("HideWeapon",Hide);
    oldReset=api->HookUserMsg("ResetHUD",Reset);
    Hook(reinterpret_cast<void*>(engine.pfnSPR_Set),reinterpret_cast<void*>(SetSprite),reinterpret_cast<void**>(&originalSet));
    Hook(reinterpret_cast<void*>(engine.pfnSPR_DrawAdditive),reinterpret_cast<void*>(DrawSprite),reinterpret_cast<void**>(&originalDraw));
    Hook(reinterpret_cast<void*>(engine.pfnFillRGBA),reinterpret_cast<void*>(Fill),reinterpret_cast<void**>(&originalFill));
}
void ResetWeapons(bool forgetRegistration)
{
    active=clip=reserve=hidden=0;predictedClip=predictedTotal=-1;
    if(forgetRegistration){registered=false;ammoType=-1;}
}
void WeaponVideoInit()
{
    for(auto& sprite:digits)sprite={};ammoIcon={};screen={};
}
int WeaponRedraw(float time,int intermission)
{
    // Preserve the ordinary HUD and selection. Only replace the selected
    // weapon's bottom-right ammo sprites and divider while it is being drawn.
    if(Custom() && !ammoIcon.handle)LoadSprites();
    drawingNative=true;
    const int result=downstream.HUD_Redraw(time,intermission);
    drawingNative=false;
    if(!Custom() || intermission || (hidden&5) || !digits[0].handle || !ammoIcon.handle ||
        (view.health<=0) || engine.pfnGetCvarFloat("hud_draw")==0)return result;
    const int width=digits[0].rect.right-digits[0].rect.left;
    const int height=digits[0].rect.bottom-digits[0].rect.top;
    const int y=screen.iHeight-height-height/2;
    const int iconX=screen.iWidth-24-width;
    // CSO CHudAmmo::Draw's special-ammo path uses RGB(114,197,255)
    // Show every remaining throw, including the one ready in the internal clip.
    // Reload only transfers that throw; it must not change this total.
    char value[16];std::snprintf(value,sizeof(value),"%d",AmmoTotal());
    int x=iconX-width-int(std::strlen(value))*width;
    for(const char* digit=value;*digit;++digit)
    {
        const auto& item=digits[*digit-'0'];originalSet(item.handle,BlueR,BlueG,BlueB);
        originalDraw(0,x,y,&item.rect);x+=width;
    }
    originalSet(ammoIcon.handle,BlueR,BlueG,BlueB);originalDraw(0,iconX,y,&ammoIcon.rect);
    ++hudDraws;
    return result;
}

void WeaponDiagnostics(std::ostream& out)
{
    out<<",\"weapon\":{\"registered\":"<<registered<<",\"active\":"<<active<<",\"clip\":"<<clip
        <<",\"predictedClip\":"<<predictedClip<<",\"reserve\":"<<reserve<<",\"hidden\":"<<hidden
        <<",\"health\":"<<view.health<<",\"draws\":"<<hudDraws<<",\"predictionCalls\":"<<predictionCalls
        <<",\"total\":"<<AmmoTotal()<<",\"postRunCalls\":"<<postRunCalls<<",\"sourceWeaponId\":"<<sourceWeaponId
        <<",\"cl_lw\":"<<engine.pfnGetCvarFloat("cl_lw")
        <<",\"digitSprite\":"<<digits[0].handle<<",\"ammoSprite\":"<<ammoIcon.handle<<'}';
}

void WeaponPostRunCmd(local_state_t* from,local_state_t* to,usercmd_t* cmd,int runFunctions,double time,unsigned int seed)
{
    ++postRunCalls;
    sourceWeaponId=from->client.m_iId;
    if(!registered || from->client.m_iId!=KnifeId)
    {
        downstream.HUD_PostRunCmd(from,to,cmd,runFunctions,time,seed);
        return;
    }
    const auto source=from->client;
    ++predictionCalls;
    auto weapon=from->weapondata[KnifeId];
    // ID 2 is unused in CS. Let downstream update its shared state without
    // dispatching this weapon through the old Half-Life Glock implementation.
    from->client.m_iId=0;
    downstream.HUD_PostRunCmd(from,to,cmd,runFunctions,time,seed);
    from->client.m_iId=KnifeId;
    to->client.m_iId=KnifeId;
    to->client.viewmodel=source.viewmodel;to->client.weaponanim=source.weaponanim;
    to->client.m_flNextAttack=source.m_flNextAttack;
    std::copy(std::begin(source.vuser4),std::end(source.vuser4),std::begin(to->client.vuser4));
    const float dt=cmd->msec/1000.0f;
    int animation=-1;
    if(source.health>0 && !source.deadflag && source.m_flNextAttack<=0)
    {
        if(weapon.m_fInReload)
        {
            if(weapon.m_iClip==0 && to->client.vuser4[1]>0)
            {weapon.m_iClip=1;to->client.vuser4[1]-=1;}
            weapon.m_fInReload=0;
        }
        const bool canShoot=(source.iuser3&1) && !(source.iuser3&2);
        if((cmd->buttons&IN_ATTACK2) && weapon.m_flNextSecondaryAttack<=0 && canShoot)
        {animation=3;weapon.m_flNextPrimaryAttack=weapon.m_flNextSecondaryAttack=1;weapon.m_flTimeWeaponIdle=1.1f;}
        else if((cmd->buttons&IN_ATTACK) && weapon.m_flNextPrimaryAttack<=0 && canShoot)
        {
            if(weapon.m_iClip>0)
            {--weapon.m_iClip;animation=1;weapon.m_flNextPrimaryAttack=weapon.m_flNextSecondaryAttack=.9f;weapon.m_flTimeWeaponIdle=1.6f;}
            else if(to->client.vuser4[1]>0)weapon.m_fInReload=1;
        }
        else if(weapon.m_iClip==0 && to->client.vuser4[1]>0 && weapon.m_flNextPrimaryAttack<=0)
            weapon.m_fInReload=1;
        else if(weapon.m_flTimeWeaponIdle<=0 && weapon.m_iClip>0)
        {animation=0;weapon.m_flTimeWeaponIdle=20;}
    }
    if(animation>=0)
    {
        to->client.weaponanim=animation;
        if(runFunctions)engine.pfnWeaponAnim(animation,0);
    }
    weapon.m_flNextPrimaryAttack=std::max(-.001f,weapon.m_flNextPrimaryAttack-dt);
    weapon.m_flNextSecondaryAttack=std::max(-.001f,weapon.m_flNextSecondaryAttack-dt);
    weapon.m_flTimeWeaponIdle=std::max(-.001f,weapon.m_flTimeWeaponIdle-dt);
    to->client.m_flNextAttack=std::max(-.001f,to->client.m_flNextAttack-dt);
    to->weapondata[KnifeId]=weapon;
    if(runFunctions)
    {
        predictedClip=weapon.m_iClip;
        predictedTotal=std::max(0,weapon.m_iClip)+std::max(0,int(to->client.vuser4[1]));
    }
}
}
