// SPDX-License-Identifier: GPL-3.0-or-later
#include "weapons.hpp"
#include "weapon_inventory.hpp"
#include "crosshair.hpp"
#include "pickup_history.hpp"
#include "beamgun.hpp"
#include "cannon.hpp"
#include "../cannon.hpp"
#include <usercmd.h>
#include <in_buttons.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <ostream>

namespace csol::client
{
namespace
{
constexpr int KnifeId = ThrowingKnifeId;
constexpr int BlueR = 114, BlueG = 197, BlueB = 255;
bool registered{}, drawingNative{};
int active{}, clip{}, ammoType=-1, reserve{}, hidden{};
int predictedClip=-1, predictedTotal=-1;
int hudDraws{}, predictionCalls{}, postRunCalls{}, sourceWeaponId{};
SCREENINFO screen{};
struct Sprite { int handle{}; wrect_t rect{}; } digits[10], ammoIcon;
int nativeDigitSprites[10]{};
Sprite nativeDigits[10], currency[3]; // dollar, minus, plus from the active hud.txt resolution
bool nativeHudLoaded{}, adjustMoney{}, moneyAnchor{};
int moneyX{}, moneyY{}, moneyHeight{}, shiftedMoneySprites{};
struct { int numberX{}, numberY{}, iconX{}, iconY{}; } hudLayout;
int currentSprite{}, loadedAmmoId{};
int spriteR{}, spriteG{}, spriteB{};
struct WideAmmoDraw
{
    bool enabled{}, drawn{}, hasClip{};
    int value{}, x{}, clipX{}, y{}, width{}, height{}, shift{};
} wideAmmo;
int wideAmmoWeapon{}, wideAmmoIconWidth{}, wideAmmoDraws{};
crosshair::State crosshairState;
int playerFlags=0x200, fieldOfView=90;
float playerSpeed{};
bool nightVision{};
int crosshairDraws{};
pfnUserMsgHook oldFov{},oldNightVision{};
pfnUserMsgHook oldWeaponList{}, oldCurWeapon{}, oldAmmo{}, oldHide{}, oldReset{};
decltype(engine.pfnSPR_Set) originalSet{};
decltype(engine.pfnSPR_DrawAdditive) originalDraw{};
decltype(engine.pfnFillRGBA) originalFill{};

bool Custom() { return (registered && active==KnifeId) || (active==cannon::Id && FindWeaponDefinition(active)); }
int SetFov(const char* name,int size,void* data)
{
    if(size==1 && data){fieldOfView=*static_cast<unsigned char*>(data);if(!fieldOfView)fieldOfView=90;}
    return oldFov?oldFov(name,size,data):1;
}
int NightVision(const char* name,int size,void* data)
{
    if(size==1 && data)nightVision=*static_cast<unsigned char*>(data)!=0;
    return oldNightVision?oldNightVision(name,size,data):1;
}
void DrawCrosshair(int intermission)
{
    if(!Custom() || intermission || (hidden&69) || view.health<=0 || view.spectator ||
        fieldOfView<90 || engine.pfnGetCvarFloat("hud_draw")==0)return;
    // CS draws this dynamic crosshair itself. The engine's `crosshair`
    // cvar controls its separate sprite path, not CHudAmmo::DrawCrosshair.
    crosshair::Style style;
    style.width=screen.iWidth;style.height=screen.iHeight;
    style.virtualWidth=crosshair::VirtualWidth(engine.pfnGetCvarString("cl_crosshair_size"),style.width);
    style.type=int(engine.pfnGetCvarFloat("cl_crosshair_type"));
    if(const auto* color=engine.pfnGetCvarString("cl_crosshair_color"))
        std::sscanf(color,"%d %d %d",&style.r,&style.g,&style.b);
    style.r=std::clamp(style.r,0,255);style.g=std::clamp(style.g,0,255);style.b=std::clamp(style.b,0,255);
    style.translucent=engine.pfnGetCvarFloat("cl_crosshair_translucent")!=0;
    if(nightVision){style.r=250;style.g=style.b=50;style.translucent=false;}
    const bool dynamic=engine.pfnGetCvarFloat("cl_dynamiccrosshair")!=0;
    const auto parameters=active==cannon::Id?crosshair::Parameters{6,3,0,3}:crosshair::ThrowingKnife;
    if(active==cannon::Id && CannonCrosshairShot())crosshairState.shot=true;
    crosshair::Advance(crosshairState,parameters,playerFlags,playerSpeed,dynamic);
    crosshair::Draw(crosshairState,crosshair::BaseGap(parameters,playerFlags,playerSpeed,dynamic),
        style,style.translucent?originalFill:engine.pfnFillRGBABlend);
    ++crosshairDraws;
}
void ApplyNativeWeaponVisibility()
{
    if (!oldHide) return;
    // Stock CurWeapon ID zero clears only the crosshair in this CS build;
    // its active HUD pointer remains. HIDEHUD_WEAPONS suppresses that HUD
    // without hiding health/armor or changing the server's requested mask.
    unsigned char mask = static_cast<unsigned char>(hidden | (ExtendedWeaponId(active) ? 1 : 0));
    oldHide("HideWeapon", sizeof(mask), &mask);
}
int AmmoTotal()
{
    if(active==cannon::Id)return std::max(0,engine.pfnGetCvarFloat("cl_lw")!=0 && CannonPredictedAmmo()>=0?CannonPredictedAmmo():clip);
    if(predictedTotal>=0 && engine.pfnGetCvarFloat("cl_lw")!=0)return predictedTotal;
    return std::max(0,clip)+std::max(0,reserve);
}
bool AmmoArea(int x,int y) { return drawingNative && Custom() && x>=screen.iWidth-200 && y>=screen.iHeight-48; }
void SetSprite(int sprite,int r,int g,int b)
{
    currentSprite=sprite;spriteR=r;spriteG=g;spriteB=b;originalSet(sprite,r,g,b);
}
bool Matches(const Sprite& sprite, const wrect_t* rect)
{
    return rect && sprite.handle && currentSprite==sprite.handle &&
        sprite.rect.left==rect->left && sprite.rect.right==rect->right &&
        sprite.rect.top==rect->top && sprite.rect.bottom==rect->bottom;
}
int MoneySpriteY(int x, int y, const wrect_t* rect)
{
    if(!drawingNative || !adjustMoney || moneyHeight<=0)return y;
    const int width=nativeDigits[0].rect.right-nativeDigits[0].rect.left;
    if(width<=0)return y;
    const int fieldX=screen.iWidth-5*width-(screen.iWidth>=640?16:12);
    // Stock account Draw starts with its dollar icon, followed by this row's
    // digits and an optional income/expense row. Never classify by atlas alone.
    if(Matches(currency[0],rect) && x==fieldX-(rect->right-rect->left) &&
        (y==screen.iHeight-3*moneyHeight || y==screen.iHeight-5*moneyHeight))
    {moneyAnchor=true;moneyX=fieldX;moneyY=y;}
    if(!moneyAnchor)return y;
    const int deltaY=moneyY-moneyHeight-moneyHeight/2;
    bool account=Matches(currency[0],rect) && x==moneyX-(rect->right-rect->left) && y==moneyY;
    for(const auto& digit:nativeDigits)
        if(Matches(digit,rect) && x>=moneyX && x<moneyX+5*width && (x-moneyX)%width==0 && (y==moneyY || y==deltaY))account=true;
    for(int i=1;i<3;++i)
        if(Matches(currency[i],rect) && x==moneyX-(rect->right-rect->left) && y==deltaY)account=true;
    if(account){++shiftedMoneySprites;return y-moneyHeight;}
    return y;
}
void DrawSprite(int frame,int x,int y,const wrect_t* rect)
{
    // Only substitute the exact ordinary-ammo glyph cells emitted by CS.
    // Keep its icon, clip, fade color, crosshair and all unrelated HUD draws.
    if(drawingNative && wideAmmo.enabled && y==wideAmmo.y)
    {
        bool number=false;for(const auto& glyph:nativeDigits)if(Matches(glyph,rect)){number=true;break;}
        if(number && wideAmmo.hasClip && x>=wideAmmo.clipX && x<wideAmmo.clipX+3*wideAmmo.width &&
            (x-wideAmmo.clipX)%wideAmmo.width==0)
        {originalDraw(frame,x-wideAmmo.shift,y,rect);return;}
        if(number && x>=wideAmmo.x && x<wideAmmo.x+3*wideAmmo.width && (x-wideAmmo.x)%wideAmmo.width==0)
        {
            if(!wideAmmo.drawn)
            {
                char value[16];std::snprintf(value,sizeof(value),"%d",wideAmmo.value);
                int cursor=wideAmmo.x-wideAmmo.shift;
                for(const char* p=value;*p;++p)
                {
                    const auto& glyph=nativeDigits[*p-'0'];originalSet(glyph.handle,spriteR,spriteG,spriteB);
                    originalDraw(frame,cursor,y,&glyph.rect);cursor+=wideAmmo.width;
                }
                originalSet(currentSprite,spriteR,spriteG,spriteB);
                wideAmmo.drawn=true;++wideAmmoDraws;
            }
            return;
        }
    }
    bool digit=false;for(const int sprite:nativeDigitSprites)if(sprite==currentSprite)digit=true;
    if(AmmoArea(x,y) && (digit || currentSprite==ammoIcon.handle))return;
    originalDraw(frame,x,MoneySpriteY(x,y,rect),rect);
}
void Fill(int x,int y,int width,int height,int r,int g,int b,int a)
{
    if(drawingNative && wideAmmo.enabled && wideAmmo.hasClip && y==wideAmmo.y &&
        x==wideAmmo.clipX+3*wideAmmo.width+wideAmmo.width/2 &&
        width==wideAmmo.width/10 && height==wideAmmo.height)
    {originalFill(x-wideAmmo.shift,y,width,height,r,g,b,a);return;}
    if(AmmoArea(x,y) && width<=4 && height<=32)return;
    originalFill(x,y,width,height,r,g,b,a);
}
int WeaponList(const char* name,int size,void* data)
{
    // Sign-on messages can precede HUD_Frame on a fast local connection.
    // Retire the previous session before accepting this server's registration.
    SynchronizeSession();
    ObserveLegacyWeaponDefinition(size,data);
    if(data && size>0)
    {
        const auto* bytes=static_cast<const unsigned char*>(data);
        const auto* end=static_cast<const unsigned char*>(std::memchr(data,0,size));
        if(!end || end-bytes+9!=size || end[7]>=LegacyWeaponLimit)return 1;
    }
    return oldWeaponList?oldWeaponList(name,size,data):1;
}
int CurWeapon(const char* name,int size,void* data)
{
    if(data && size==3)
    {
        const auto* b=static_cast<const unsigned char*>(data);
        if(b[1]>=LegacyWeaponLimit && b[1]!=255)return 1;
        ObserveSelectionClip(b[1],static_cast<signed char>(b[2]));
        if(b[1]==0 && ExtendedWeaponActive())return 1;
        if(b[0]) {active=b[1];clip=static_cast<signed char>(b[2]);predictedClip=predictedTotal=-1;}
        if(b[1]==255){active=0;predictedClip=predictedTotal=-1;}
    }
    return oldCurWeapon?oldCurWeapon(name,size,data):1;
}
int Ammo(const char* name,int size,void* data)
{
    if(data && size==2){const auto* b=static_cast<unsigned char*>(data);ObserveSelectionAmmo(b[0],b[1]);}
    if(data && size==2 && static_cast<unsigned char*>(data)[0]==ammoType)
    {
        reserve=static_cast<unsigned char*>(data)[1];
        predictedTotal=-1;
    }
    return oldAmmo?oldAmmo(name,size,data):1;
}
int Hide(const char* name,int size,void* data)
{
    if(data && size==1)
    {
        hidden=*static_cast<unsigned char*>(data);
        WeaponSelectionVisibility(hidden,0);
        ApplyNativeWeaponVisibility();
        return 1;
    }
    return oldHide?oldHide(name,size,data):1;
}
int Reset(const char* name,int size,void* data)
{
    ResetWeapons(false);
    return oldReset?oldReset(name,size,data):1;
}
void LoadNativeHudSprites()
{
    nativeHudLoaded=true;
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
            nativeDigitSprites[digit]=engine.pfnSPR_Load(path);
            nativeDigits[digit]={nativeDigitSprites[digit],list[i].rc};
        }
        constexpr const char* names[]{"dollar","minus","plus"};
        for(int c=0;c<3;++c)if(!std::strcmp(list[i].szName,names[c]))
        {
            char path[160];std::snprintf(path,sizeof(path),"sprites/%s.spr",list[i].szSprite);
            currency[c]={engine.pfnSPR_Load(path),list[i].rc};
        }
    }
    moneyHeight=nativeDigits[0].handle?nativeDigits[0].rect.bottom-nativeDigits[0].rect.top:0;
}
void LoadSprites()
{
    if(!nativeHudLoaded)LoadNativeHudSprites();
    // CSO hud.txt: number_0..9 use 20x25 cells in 640hud7. Stock CS
    // uses the same rectangles but different pixels; keep its atlas intact.
    constexpr int digitLeft[]{0,23,47,70,95,119,144,169,192,216};
    const int atlas=engine.pfnSPR_Load("sprites/csol/640hud7.spr");
    for(int digit=0;digit<10;++digit)
        digits[digit]={atlas,{digitLeft[digit],digitLeft[digit]+20,0,25}};
    if(active==cannon::Id)ammoIcon={engine.pfnSPR_Load("sprites/csol/640hud2.spr"),{73,97,231,255}};
    else ammoIcon={engine.pfnSPR_Load("sprites/csol/640hud12.spr"),{49,73,231,255}};
    loadedAmmoId=active;
}
void PrepareWideAmmo(int intermission)
{
    wideAmmo={};
    const auto& current=CurrentExtendedWeapon();
    const auto* definition=FindWeaponDefinition(active);
    // CSOWpnCur is already capability-negotiated and carries a signed 16-bit
    // reserve for ordinary weapons too. Stock AmmoX stays byte-compatible.
    if(intermission || (hidden&5) || view.health<=0 || view.spectator ||
        !current.state || current.id!=active || active<=0 || active>=LegacyWeaponLimit ||
        current.reserve<=255 || !definition || definition->ammo1<0 ||
        engine.pfnGetCvarFloat("hud_draw")==0)return;
    if(!nativeHudLoaded)LoadNativeHudSprites();
    const int width=nativeDigits[0].rect.right-nativeDigits[0].rect.left;
    const int height=nativeDigits[0].rect.bottom-nativeDigits[0].rect.top;
    if(width<=0 || height<=0)return;
    for(const auto& glyph:nativeDigits)
        if(!glyph.handle || glyph.rect.right-glyph.rect.left!=width || glyph.rect.bottom-glyph.rect.top!=height)return;
    if(wideAmmoWeapon!=active)
    {
        wideAmmoWeapon=active;wideAmmoIconWidth=0;
        int count{};std::string path="sprites/"+definition->name+".txt";
        const auto* list=engine.pfnSPR_GetList(path.data(),&count);
        const int resolution=screen.iWidth<640?320:640;
        for(int i=0;list && i<count;++i)
            if(list[i].iRes==resolution && std::memchr(list[i].szName,0,sizeof(list[i].szName)) &&
                !std::strcmp(list[i].szName,"ammo"))wideAmmoIconWidth=list[i].rc.right-list[i].rc.left;
    }
    if(wideAmmoIconWidth<=0)return;
    char value[16];std::snprintf(value,sizeof(value),"%d",current.reserve);
    wideAmmo.enabled=true;wideAmmo.hasClip=current.clip>=0;wideAmmo.value=current.reserve;
    wideAmmo.width=width;wideAmmo.height=height;wideAmmo.y=screen.iHeight-height-height/2;
    wideAmmo.clipX=screen.iWidth-8*width-wideAmmoIconWidth;
    wideAmmo.x=screen.iWidth-4*width-wideAmmoIconWidth+(wideAmmo.hasClip?width/10:0);
    wideAmmo.shift=std::max(0,int(std::strlen(value))-3)*width;
}
}

void InitializeWeapons()
{
    InitializePickupHistory();
    InitializeBeamgun();
    InitializeCannon();
    oldWeaponList=api->HookUserMsg("WeaponList",WeaponList);
    oldCurWeapon=api->HookUserMsg("CurWeapon",CurWeapon);
    oldAmmo=api->HookUserMsg("AmmoX",Ammo);
    oldHide=api->HookUserMsg("HideWeapon",Hide);
    oldReset=api->HookUserMsg("ResetHUD",Reset);
    oldFov=api->HookUserMsg("SetFOV",SetFov);
    oldNightVision=api->HookUserMsg("NVGToggle",NightVision);
    Hook(reinterpret_cast<void*>(engine.pfnSPR_Set),reinterpret_cast<void*>(SetSprite),reinterpret_cast<void**>(&originalSet));
    Hook(reinterpret_cast<void*>(engine.pfnSPR_DrawAdditive),reinterpret_cast<void*>(DrawSprite),reinterpret_cast<void**>(&originalDraw));
    Hook(reinterpret_cast<void*>(engine.pfnFillRGBA),reinterpret_cast<void*>(Fill),reinterpret_cast<void**>(&originalFill));
}
void ResetWeapons(bool forgetRegistration)
{
    ResetWeaponInventory(forgetRegistration);
    ResetPickupHistory();
    ResetBeamgun();
    ResetCannon();
    ResetCannonEffects();
    active=clip=reserve=hidden=0;predictedClip=predictedTotal=-1;
    wideAmmo={};wideAmmoWeapon=wideAmmoIconWidth=0;
    adjustMoney=moneyAnchor=false;
    crosshairState={};fieldOfView=90;nightVision=false;playerFlags=0x200;playerSpeed=0;
    if(forgetRegistration){registered=false;ammoType=-1;}
    // Release the native HUD suppression owned by the departing extended gun.
    // Clearing only our fields leaves the stock slot commands hidden as well.
    ApplyNativeWeaponVisibility();
}
void ClearNativeWeaponSelection()
{
    // HideWeapon clears stock gpActiveSel. Restore the effective mask at
    // once so normal ammo/crosshair rendering continues on ordinary weapons.
    if(oldHide){unsigned char mask=static_cast<unsigned char>(hidden|1);oldHide("HideWeapon",1,&mask);ApplyNativeWeaponVisibility();}
}
void WeaponVideoInit()
{
    WeaponInventoryVideoInit();
    PickupHistoryVideoInit();
    BeamgunVideoInit();
    ResetCannonEffects();
    for(auto& sprite:digits)sprite={};
    std::fill(std::begin(nativeDigitSprites),std::end(nativeDigitSprites),0);
    for(auto& sprite:nativeDigits)sprite={};
    for(auto& sprite:currency)sprite={};
    nativeHudLoaded=adjustMoney=moneyAnchor=false;moneyHeight=0;
    ammoIcon={};loadedAmmoId=0;screen={};hudLayout={};
    wideAmmo={};wideAmmoWeapon=wideAmmoIconWidth=0;
}
int WeaponRedraw(float time,int intermission)
{
    // Preserve the ordinary HUD and selection. Only replace the selected
    // weapon's bottom-right ammo sprites and divider while it is being drawn.
    if(Custom() && (!ammoIcon.handle || loadedAmmoId!=active))LoadSprites();
    adjustMoney=BeamgunSecondaryAmmoVisible(hidden,intermission);
    if(adjustMoney && !nativeHudLoaded)LoadNativeHudSprites();
    PrepareWideAmmo(intermission);
    moneyAnchor=false;
    drawingNative=true;
    const int result=downstream.HUD_Redraw(time,intermission);
    drawingNative=false;
    wideAmmo.enabled=false;
    adjustMoney=moneyAnchor=false;
    DrawWeaponSelection(hidden,intermission);
    DrawWeaponGuard(hidden,intermission);
    DrawPickupHistory(hidden,intermission);
    DrawCrosshair(intermission);
    DrawBeamgunHUD(hidden,intermission);
    if(!Custom() || intermission || (hidden&5) || !digits[0].handle || !ammoIcon.handle ||
        (view.health<=0) || engine.pfnGetCvarFloat("hud_draw")==0)return result;
    const int width=digits[0].rect.right-digits[0].rect.left;
    const int height=digits[0].rect.bottom-digits[0].rect.top;
    const int y=screen.iHeight-height-height/2;
    const int iconWidth=ammoIcon.rect.right-ammoIcon.rect.left;
    const int iconHeight=ammoIcon.rect.bottom-ammoIcon.rect.top;
    const int fieldX=screen.iWidth-4*width-iconWidth;
    const int iconX=fieldX+3*width;
    const int iconY=y-iconHeight/8;
    // CSO CHudAmmo::Draw's special-ammo path uses RGB(114,197,255)
    // Show every remaining throw, including the one ready in the internal clip.
    // Reload only transfers that throw; it must not change this total.
    char value[16];std::snprintf(value,sizeof(value),"%d",AmmoTotal());
    // Original DrawHudNumber with DHN_3DIGITS advances over empty leading
    // cells and returns the icon's x. There is no extra cell between them.
    int x=fieldX+std::max(0,3-int(std::strlen(value)))*width;
    hudLayout={x,y,iconX,iconY};
    for(const char* digit=value;*digit;++digit)
    {
        const auto& item=digits[*digit-'0'];originalSet(item.handle,BlueR,BlueG,BlueB);
        originalDraw(0,x,y,&item.rect);x+=width;
    }
    originalSet(ammoIcon.handle,BlueR,BlueG,BlueB);originalDraw(0,iconX,iconY,&ammoIcon.rect);
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
        <<",\"crosshairDraws\":"<<crosshairDraws<<",\"crosshairGap\":"<<crosshairState.gap
        <<",\"digitSprite\":"<<digits[0].handle<<",\"ammoSprite\":"<<ammoIcon.handle
        <<",\"screenWidth\":"<<screen.iWidth<<",\"screenHeight\":"<<screen.iHeight
        <<",\"numberX\":"<<hudLayout.numberX<<",\"numberY\":"<<hudLayout.numberY
        <<",\"iconX\":"<<hudLayout.iconX<<",\"iconY\":"<<hudLayout.iconY
        <<",\"moneyShiftDraws\":"<<shiftedMoneySprites<<",\"wideAmmoDraws\":"<<wideAmmoDraws<<'}';
    PickupHistoryDiagnostics(out);
    WeaponSelectionDiagnostics(out);
    SessionDiagnostics(out);
    BeamgunDiagnostics(out);
    CannonDiagnostics(out);
}

void WeaponPostRunCmd(local_state_t* from,local_state_t* to,usercmd_t* cmd,int runFunctions,double time,unsigned int seed)
{
    ++postRunCalls;
    sourceWeaponId=from->client.m_iId;
    playerFlags=from->client.flags;
    playerSpeed=std::hypot(from->client.velocity[0],from->client.velocity[1]);
    if(PredictBeamgun(from,to,cmd,runFunctions,time,seed))return;
    if(PredictCannon(from,to,cmd,runFunctions,time,seed))return;
    if(!registered || from->client.m_iId!=KnifeId || from->weapondata[ActiveWeaponSlot].m_iId!=KnifeId)
    {
        if(ExtendedWeaponId(from->client.m_iId))
        {
            // Stock CS's prediction table is only 32 entries. Unknown extended
            // weapons remain server-driven until their predictor is registered.
            const auto source=from->client;const auto data=from->weapondata[ActiveWeaponSlot];
            from->client.m_iId=0;
            downstream.HUD_PostRunCmd(from,to,cmd,runFunctions,time,seed);
            from->client.m_iId=source.m_iId;to->client.m_iId=source.m_iId;
            to->client.viewmodel=source.viewmodel;to->client.weaponanim=source.weaponanim;
            to->client.m_flNextAttack=source.m_flNextAttack;
            to->client.iuser3=source.iuser3;
            std::copy(std::begin(source.vuser4),std::end(source.vuser4),std::begin(to->client.vuser4));
            to->weapondata[ActiveWeaponSlot]=data;
            return;
        }
        downstream.HUD_PostRunCmd(from,to,cmd,runFunctions,time,seed);
        return;
    }
    const auto source=from->client;
    ++predictionCalls;
    auto weapon=from->weapondata[ActiveWeaponSlot];
    // CSO keeps the active weapon in slot zero; its ID selects the predictor.
    from->client.m_iId=0;
    downstream.HUD_PostRunCmd(from,to,cmd,runFunctions,time,seed);
    from->client.m_iId=KnifeId;
    to->client.m_iId=KnifeId;
    to->client.viewmodel=source.viewmodel;to->client.weaponanim=source.weaponanim;
    to->client.m_flNextAttack=source.m_flNextAttack;
    // Stock CS returns early for ID 0, before copying the server's permission
    // and freeze-period bits. Preserve them for the next predicted command.
    to->client.iuser3=source.iuser3;
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
        if(runFunctions){engine.pfnWeaponAnim(animation,0);if(animation==1)crosshairState.shot=true;}
    }
    weapon.m_flNextPrimaryAttack=std::max(-.001f,weapon.m_flNextPrimaryAttack-dt);
    weapon.m_flNextSecondaryAttack=std::max(-.001f,weapon.m_flNextSecondaryAttack-dt);
    weapon.m_flTimeWeaponIdle=std::max(-.001f,weapon.m_flTimeWeaponIdle-dt);
    to->client.m_flNextAttack=std::max(-.001f,to->client.m_flNextAttack-dt);
    to->weapondata[ActiveWeaponSlot]=weapon;
    if(runFunctions)
    {
        predictedClip=weapon.m_iClip;
        predictedTotal=std::max(0,weapon.m_iClip)+std::max(0,int(to->client.vuser4[1]));
    }
}
void ReceiveExtendedWeaponDefinition(const WeaponDefinition& definition)
{
    if(definition.id==KnifeId)
    {
        registered=definition.name=="weapon_tknifeex2";
        ammoType=registered?definition.ammo1:-1;
    }
}
void ReceiveExtendedWeaponState(const WeaponCurrent& current)
{
    ReceiveBeamgunState(current);
    ReceiveCannonState(current);
    active=current.state?current.id:0;clip=current.clip;reserve=current.reserve;
    ApplyNativeWeaponVisibility();
    predictedClip=predictedTotal=-1;
}
}
