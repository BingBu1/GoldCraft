// SPDX-License-Identifier: GPL-3.0-or-later
#include "../weapon_hud_paths.hpp"
#include "weapons.hpp"
#include "weapon_module.hpp"
#include "user_message_chain.hpp"
#include "weapon_inventory.hpp"
#include "hud_style.hpp"
#include "character_skill_effects.hpp"
#include "crosshair.hpp"
#include "pickup_history.hpp"
#include <usercmd.h>
#include <in_buttons.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <charconv>
#include <ostream>

namespace csol::client
{
namespace
{
constexpr int KnifeId = ThrowingKnifeId;
constexpr int BlueR = 114, BlueG = 197, BlueB = 255;
bool registered{}, drawingNative{}, nativeDeadHud{};
int active{}, clip{}, ammoType=-1, reserve{}, hidden{};
int predictedClip=-1, predictedTotal=-1;
int hudDraws{}, predictionCalls{}, postRunCalls{}, sourceWeaponId{};
SCREENINFO screen{};
struct Sprite { int handle{}; wrect_t rect{}; } digits[10], ammoIcon;
int nativeDigitSprites[10]{};
Sprite nativeDigits[10], currency[3]; // dollar, minus, plus from the active hud.txt resolution
std::array<Sprite,5> nativeVitals;
std::array<Sprite,2> nativeAmmo;
struct NativeVitalRun
{
    std::array<char,10> text{};
    int x{}, y{}, position{}, count{};
};
std::array<NativeVitalRun,2> nativeVitalRuns{};
struct NativeAmmoRun { int x{}, y{}; unsigned consumed{}; bool enabled{}; };
struct NativeAmmoIcon { int x{}, y{}; bool enabled{}; };
std::array<NativeAmmoRun,3> nativeAmmoRuns{};
std::array<NativeAmmoIcon,2> nativeAmmoIcons{};
struct { int x{}, y{}, width{}, height{}; bool enabled{}; } nativeAmmoDivider;
int nativeAmmoWeapon{};
bool nativeHudLoaded{}, adjustMoney{}, moneyAnchor{};
int moneyX{}, moneyY{}, moneyHeight{}, shiftedMoneySprites{};
struct { int numberX{}, numberY{}, iconX{}, iconY{}; } hudLayout;
int currentSprite{}, loadedAmmoId{};
int spriteR{}, spriteG{}, spriteB{};
struct WideAmmoDraw
{
    bool enabled{}, drawn{}, hasClip{}, infinite{};
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

bool Custom()
{
    const auto* module=FindWeaponModule(active);
    return (registered && active==KnifeId) ||
        (module && module->ammoKind==hud_style::AmmoKind::Total && FindWeaponDefinition(active));
}
bool MagazineActive()
{
    const auto* module=FindWeaponModule(active);
    return module && module->ammoKind==hud_style::AmmoKind::Magazine && FindWeaponDefinition(active);
}
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
    const auto* module=FindWeaponModule(active);
    // CSO chooses the crosshair from the weapon record, including knife 29.
    // AmmoKind::None must not exclude a registered melee presentation.
    if((!Custom() && !(module && FindWeaponDefinition(active))) || intermission || (hidden&69) ||
        view.health<=0 || view.spectator || fieldOfView<90 || engine.pfnGetCvarFloat("hud_draw")==0)return;
    if(module && !module->dynamicCrosshair)return;
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
    const auto parameters=module?module->crosshair:crosshair::ThrowingKnife;
    if(module && module->crosshairShot && module->crosshairShot())crosshairState.shot=true;
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
    // A dead player waiting in Scenario is not an observer, so stock CS would
    // keep drawing 0 health/armor and its weapon HUD. CSO hides the bottom
    // bars while dead; add WEAPONS|HEALTH only for the local stock client.
    unsigned char mask = static_cast<unsigned char>(hidden | (ExtendedWeaponId(active) ? 1 : 0) |
        (nativeDeadHud ? 1 | 8 : 0));
    oldHide("HideWeapon", sizeof(mask), &mask);
}
int AmmoTotal()
{
    if(const auto* module=FindWeaponModule(active);module && module->ammoKind==hud_style::AmmoKind::Total)
        return std::max(0,engine.pfnGetCvarFloat("cl_lw")!=0 && module->predictedClip && module->predictedClip()>=0?
            module->predictedClip():clip);
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
bool NativeMoneySprite(int x, int y, const wrect_t* rect)
{
    if(!drawingNative || !adjustMoney || moneyHeight<=0)return false;
    const int width=nativeDigits[0].rect.right-nativeDigits[0].rect.left;
    if(width<=0)return false;
    const int fieldX=screen.iWidth-5*width-(screen.iWidth>=640?16:12);
    // Stock account Draw starts with its dollar icon, followed by this row's
    // digits and an optional income/expense row. Never classify by atlas alone.
    if(Matches(currency[0],rect) && x==fieldX-(rect->right-rect->left) &&
        (y==screen.iHeight-3*moneyHeight || y==screen.iHeight-5*moneyHeight))
    {moneyAnchor=true;moneyX=fieldX;moneyY=y;}
    if(!moneyAnchor)return false;
    const int deltaY=moneyY-moneyHeight-moneyHeight/2;
    bool account=Matches(currency[0],rect) && x==moneyX-(rect->right-rect->left) && y==moneyY;
    for(const auto& digit:nativeDigits)
        if(Matches(digit,rect) && x>=moneyX && x<moneyX+5*width && (x-moneyX)%width==0 && (y==moneyY || y==deltaY))account=true;
    for(int i=1;i<3;++i)
        if(Matches(currency[i],rect) && x==moneyX-(rect->right-rect->left) && y==deltaY)account=true;
    return account;
}
int MoneySpriteY(int x,int y,const wrect_t* rect)
{
    // Only the legacy secondary-ammo row needs this one-line displacement.
    // The modern account panel has its own visibility and a fixed left anchor.
    if(NativeMoneySprite(x,y,rect)) {++shiftedMoneySprites;return y-moneyHeight;}
    return y;
}
bool Inside(const Sprite& sprite,const wrect_t* rect)
{
    return rect && sprite.handle && currentSprite==sprite.handle &&
        rect->left>=sprite.rect.left && rect->right<=sprite.rect.right &&
        rect->top>=sprite.rect.top && rect->bottom<=sprite.rect.bottom &&
        rect->right>rect->left && rect->bottom>rect->top;
}
void PrepareStyledNativeAmmo(bool hasClip,bool hasPrimary,bool hasSecondary)
{
    nativeAmmoRuns={};nativeAmmoIcons={};nativeAmmoDivider={};
    if(!HudStyleWeaponVisible())return;
    const int width=nativeDigits[0].rect.right-nativeDigits[0].rect.left;
    const int height=nativeDigits[0].rect.bottom-nativeDigits[0].rect.top;
    if(width<=0 || height<=0)return;
    const int baseline=screen.iHeight-height-height/2;
    if(hasPrimary && nativeAmmo[0].handle)
    {
        const int iconWidth=nativeAmmo[0].rect.right-nativeAmmo[0].rect.left;
        const int iconHeight=nativeAmmo[0].rect.bottom-nativeAmmo[0].rect.top;
        int reserveX=screen.iWidth-4*width-iconWidth;
        if(hasClip)
        {
            const int clipX=screen.iWidth-8*width-iconWidth;
            const int dividerX=clipX+3*width+width/2;
            nativeAmmoRuns[0]={clipX,baseline,0,true};
            nativeAmmoDivider={dividerX,baseline,width/10,height,true};
            reserveX=dividerX+width/10+width/2;
        }
        nativeAmmoRuns[1]={reserveX,baseline,0,true};
        nativeAmmoIcons[0]={reserveX+3*width,baseline-iconHeight/8,true};
    }
    if(hasSecondary && nativeAmmo[1].handle)
    {
        const int iconWidth=nativeAmmo[1].rect.right-nativeAmmo[1].rect.left;
        const int iconHeight=nativeAmmo[1].rect.bottom-nativeAmmo[1].rect.top;
        const int x=screen.iWidth-4*width-iconWidth,y=baseline-height-height/4;
        nativeAmmoRuns[2]={x,y,0,true};
        nativeAmmoIcons[1]={x+3*width,y-iconHeight/8,true};
    }
}
bool HideStyledSprite(int x,int y,const wrect_t* rect)
{
    if(!drawingNative)return false;
    const int width=nativeDigits[0].rect.right-nativeDigits[0].rect.left;
    const int height=nativeDigits[0].rect.bottom-nativeDigits[0].rect.top;
    if(width<=0 || height<=0)return false;
    const auto glyph=std::find_if(std::begin(nativeDigits),std::end(nativeDigits),
        [&](const Sprite& sprite){return Matches(sprite,rect);});
    const bool digit=glyph!=std::end(nativeDigits);
    if(HudStyleVitalsVisible())
    {
        const int baseline=screen.iHeight-height-height/2;
        if(y>=screen.iHeight-2*height && x<screen.iWidth/2)
            for(unsigned i=0;i<nativeVitals.size();++i)if(Inside(nativeVitals[i],rect))
            {
                const int iconWidth=nativeVitals[i].rect.right-nativeVitals[i].rect.left;
                if(x!=(i==0?iconWidth/2:screen.iWidth/5))continue;
                const auto values=HudStyleVitalsState();
                auto& run=nativeVitalRuns[i==0?0:1];
                const int value=std::max(0,i==0?values.health:values.armor);
                run.count=int(std::to_chars(run.text.data(),run.text.data()+run.text.size(),value).ptr-run.text.data());
                run.position=0;run.y=baseline;
                run.x=(i==0?iconWidth+width/2:x+iconWidth)+std::max(0,3-run.count)*width;
                return true;
            }
        if(digit)for(auto& run:nativeVitalRuns)
            if(run.position<run.count && x==run.x && y==run.y &&
                glyph-std::begin(nativeDigits)==run.text[run.position]-'0')
            {run.x+=width;++run.position;return true;}
    }
    if(HudStyleWeaponVisible())
    {
        for(unsigned i=0;i<nativeAmmoIcons.size();++i)
        {
            auto& icon=nativeAmmoIcons[i];
            if(icon.enabled && x==icon.x && y==icon.y && Matches(nativeAmmo[i],rect))
            {icon.enabled=false;return true;}
        }
        // These are the native client's three-cell fields, not the CSO
        // overlay's coordinates. Consume each cell once per native draw.
        if(digit)for(auto& run:nativeAmmoRuns)
        {
            const int offset=x-run.x;
            if(!run.enabled || y!=run.y || offset<0 || offset>=3*width || offset%width)continue;
            const unsigned bit=1u<<(offset/width);
            if(!(run.consumed&bit)){run.consumed|=bit;return true;}
        }
    }
    return false;
}
void DrawSprite(int frame,int x,int y,const wrect_t* rect)
{
    if(HideStyledSprite(x,y,rect))return;
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
                if(wideAmmo.infinite)
                {
                    const int restoreSprite=currentSprite,r=spriteR,g=spriteG,b=spriteB;
                    if(DrawCharacterSkillInfiniteAmmo(active,wideAmmo.x+3*wideAmmo.width,y,r,g,b))
                    {
                        engine.pfnSPR_Set(restoreSprite,r,g,b);
                        wideAmmo.drawn=true;++wideAmmoDraws;return;
                    }
                }
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
    if(HudStyleMoneyVisible() && NativeMoneySprite(x,y,rect))return;
    originalDraw(frame,x,MoneySpriteY(x,y,rect),rect);
}
void Fill(int x,int y,int width,int height,int r,int g,int b,int a)
{
    if(drawingNative && HudStyleWeaponVisible())
    {
        if(nativeAmmoDivider.enabled && x==nativeAmmoDivider.x && y==nativeAmmoDivider.y &&
            width==nativeAmmoDivider.width && height==nativeAmmoDivider.height)
        {nativeAmmoDivider.enabled=false;return;}
    }
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
        if(b[0] || b[1]==255)
            for(const auto* module:WeaponModules())if(module->nativeWeaponChanged)
                module->nativeWeaponChanged(b[1]==255?0:b[1]);
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
    char hudPath[]="sprites/hud.txt";
    auto* list=engine.pfnSPR_GetList(hudPath,&count);
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
        constexpr const char* vitals[]{"cross","suit_full","suit_empty","suithelmet_full","suithelmet_empty"};
        for(unsigned v=0;v<nativeVitals.size();++v)if(!std::strcmp(list[i].szName,vitals[v]))
        {
            char path[160];std::snprintf(path,sizeof(path),"sprites/%s.spr",list[i].szSprite);
            nativeVitals[v]={engine.pfnSPR_Load(path),list[i].rc};
        }
    }
    moneyHeight=nativeDigits[0].handle?nativeDigits[0].rect.bottom-nativeDigits[0].rect.top:0;
}
void LoadNativeAmmo()
{
    if(nativeAmmoWeapon==active)return;
    nativeAmmo={};nativeAmmoWeapon=active;
    const auto* definition=FindWeaponDefinition(active);if(!definition)return;
    int count{};auto path=WeaponHudList(definition->name);
    const auto* list=engine.pfnSPR_GetList(path.data(),&count);
    for(int i=0;list && i<count;++i)
    {
        if(list[i].iRes!=(screen.iWidth<640?320:640) ||
            !std::memchr(list[i].szName,0,sizeof(list[i].szName)) ||
            !std::memchr(list[i].szSprite,0,sizeof(list[i].szSprite)))continue;
        const int kind=!std::strcmp(list[i].szName,"ammo")?0:!std::strcmp(list[i].szName,"ammo2")?1:-1;
        if(kind<0)continue;
        const std::string name=list[i].szSprite;
        if(name.empty() || name.find("..")!=name.npos || name.front()=='/' || name.find_first_of(":\\")!=name.npos)continue;
        const auto sprite="sprites/"+name+".spr";
        nativeAmmo[kind]={engine.pfnSPR_Load(sprite.c_str()),list[i].rc};
    }
}
void LoadSprites()
{
    if(!nativeHudLoaded)LoadNativeHudSprites();
    // CSO hud.txt: number_0..9 use 20x25 cells in 640hud7. Stock CS
    // uses the same rectangles but different pixels; keep its atlas intact.
    constexpr int digitLeft[]{0,23,47,70,95,119,144,169,192,216};
    const int atlas=engine.pfnSPR_Load("sprites/csol/640hud7_v2.spr");
    for(int digit=0;digit<10;++digit)
        digits[digit]={atlas,{digitLeft[digit],digitLeft[digit]+20,0,25}};
    if(const auto* module=FindWeaponModule(active);module && module->ammoSprite)
        ammoIcon={std::strcmp(module->ammoSprite,"sprites/csol/640hud7_v2.spr")==0?atlas:
            engine.pfnSPR_Load(module->ammoSprite),module->ammoRect};
    else ammoIcon={engine.pfnSPR_Load("sprites/csol/640hud12.spr"),{49,73,231,255}};
    loadedAmmoId=active;
}
void DrawMagazineAmmo(int intermission)
{
    if(!MagazineActive() || intermission || (hidden&5) || view.health<=0 || view.spectator ||
        !digits[0].handle || !ammoIcon.handle || engine.pfnGetCvarFloat("hud_draw")==0)return;
    const auto current=CurrentWeaponHudState();
    const int currentClip=std::max(0,current.clip),currentReserve=std::max(0,current.reserve);
    const auto* module=FindWeaponModule(active);
    const int red=module->ammoColor[0],green=module->ammoColor[1],blue=module->ammoColor[2];
    char clipText[16],reserveText[16];
    std::snprintf(clipText,sizeof(clipText),"%d",currentClip);
    std::snprintf(reserveText,sizeof(reserveText),"%d",currentReserve);
    const bool infinite=CharacterSkillInfiniteAmmoFor(active);
    const int clipCells=std::max(3,int(std::strlen(clipText))),reserveCells=std::max(3,int(std::strlen(reserveText)));
    const int width=digits[0].rect.right-digits[0].rect.left,height=digits[0].rect.bottom-digits[0].rect.top;
    if(width<=0 || height<=0)return;
    const int y=screen.iHeight-height-height/2,iconWidth=ammoIcon.rect.right-ammoIcon.rect.left;
    const int reserveSpan=infinite?72:reserveCells*width;
    const int clipX=screen.iWidth-(clipCells+2)*width-reserveSpan-iconWidth;
    const int dividerX=clipX+clipCells*width+width/2,dividerWidth=std::max(1,width/10);
    const int reserveX=dividerX+dividerWidth+width/2;
    const auto number=[&](const char* text,int x,int cells)
    {
        x+=(cells-int(std::strlen(text)))*width;
        for(const char* digit=text;*digit;++digit)
        {
            const auto& sprite=digits[*digit-'0'];originalSet(sprite.handle,red,green,blue);
            originalDraw(0,x,y,&sprite.rect);x+=width;
        }
    };
    number(clipText,clipX,clipCells);
    if(!infinite||!DrawCharacterSkillInfiniteAmmo(active,reserveX+reserveSpan,y,red,green,blue))
        number(reserveText,reserveX,reserveCells);
    originalFill(dividerX,y,dividerWidth,height,red,green,blue,255);
    originalSet(ammoIcon.handle,red,green,blue);
    originalDraw(0,reserveX+reserveSpan,y-(ammoIcon.rect.bottom-ammoIcon.rect.top)/8,&ammoIcon.rect);
    ++hudDraws;
}
void PrepareWideAmmo(int intermission)
{
    wideAmmo={};
    const auto& current=CurrentExtendedWeapon();
    const auto* definition=FindWeaponDefinition(active);
    const bool infinite=CharacterSkillInfiniteAmmoFor(active);
    // CSOWpnCur is already capability-negotiated and carries a signed 16-bit
    // reserve for ordinary weapons too. Stock AmmoX stays byte-compatible.
    if(intermission || (hidden&5) || view.health<=0 || view.spectator ||
        !current.state || current.id!=active || active<=0 || active>=LegacyWeaponLimit ||
        (current.reserve<=255&&!infinite) || !definition || definition->ammo1<0 ||
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
        int count{};std::string path=WeaponHudList(definition->name);
        const auto* list=engine.pfnSPR_GetList(path.data(),&count);
        const int resolution=screen.iWidth<640?320:640;
        for(int i=0;list && i<count;++i)
            if(list[i].iRes==resolution && std::memchr(list[i].szName,0,sizeof(list[i].szName)) &&
                !std::strcmp(list[i].szName,"ammo"))wideAmmoIconWidth=list[i].rc.right-list[i].rc.left;
    }
    if(wideAmmoIconWidth<=0)return;
    char value[16];std::snprintf(value,sizeof(value),"%d",current.reserve);
    wideAmmo.enabled=true;wideAmmo.hasClip=current.clip>=0;wideAmmo.value=current.reserve;wideAmmo.infinite=infinite;
    wideAmmo.width=width;wideAmmo.height=height;wideAmmo.y=screen.iHeight-height-height/2;
    wideAmmo.clipX=screen.iWidth-8*width-wideAmmoIconWidth;
    wideAmmo.x=screen.iWidth-4*width-wideAmmoIconWidth+(wideAmmo.hasClip?width/10:0);
    wideAmmo.shift=infinite?std::max(0,72-3*width):std::max(0,int(std::strlen(value))-3)*width;
}
}

void InitializeWeapons()
{
    InitializePickupHistory();
    InitializeWeaponModules();
    oldWeaponList=api->HookUserMsg("WeaponList",WeaponList);
    oldCurWeapon=api->HookUserMsg("CurWeapon",CurWeapon);
    oldAmmo=api->HookUserMsg("AmmoX",Ammo);
    InstallChainedUserMessage("HideWeapon",Hide,oldHide);
    InstallChainedUserMessage("ResetHUD",Reset,oldReset);
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
    ResetWeaponModules();
    active=clip=reserve=hidden=0;predictedClip=predictedTotal=-1;nativeDeadHud=false;
    wideAmmo={};wideAmmoWeapon=wideAmmoIconWidth=0;
    nativeAmmo={};nativeAmmoWeapon=0;nativeVitalRuns={};
    adjustMoney=moneyAnchor=false;
    crosshairState={};fieldOfView=90;nightVision=false;playerFlags=0x200;playerSpeed=0;
    if(forgetRegistration){registered=false;ammoType=-1;}
    // Release the native HUD suppression owned by the departing extended gun.
    // Clearing only our fields leaves the stock slot commands hidden as well.
    ApplyNativeWeaponVisibility();
}
void ShutdownWeapons() { ShutdownWeaponModules(); }
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
    WeaponModulesVideoInit();
    for(auto& sprite:digits)sprite={};
    std::fill(std::begin(nativeDigitSprites),std::end(nativeDigitSprites),0);
    for(auto& sprite:nativeDigits)sprite={};
    for(auto& sprite:currency)sprite={};
    nativeVitals={};nativeAmmo={};nativeAmmoWeapon=0;
    nativeHudLoaded=adjustMoney=moneyAnchor=false;moneyHeight=0;
    ammoIcon={};loadedAmmoId=0;screen={};hudLayout={};
    wideAmmo={};wideAmmoWeapon=wideAmmoIconWidth=0;
}
int WeaponFieldOfView() { return fieldOfView; }
int WeaponRedraw(float time,int intermission)
{
    // Observers are already hidden by stock iuser1 checks; only the dead
    // non-observer local player changes the mask forwarded to stock CS.
    if(const bool dead=view.health<=0 && !view.spectator;dead!=nativeDeadHud)
    {
        nativeDeadHud=dead;
        ApplyNativeWeaponVisibility();
    }
    BeginHudStyle(hidden,intermission);
    if(HudStyleVitalsVisible() || HudStyleWeaponVisible() || HudStyleMoneyVisible())
    {
        if(!nativeHudLoaded)LoadNativeHudSprites();
        LoadNativeAmmo();
        const bool digitsReady=std::all_of(std::begin(nativeDigits),std::end(nativeDigits),
            [](const Sprite& sprite){return sprite.handle!=0;});
        const auto* definition=FindWeaponDefinition(active);
        ConstrainHudStyle(digitsReady && std::all_of(nativeVitals.begin(),nativeVitals.end(),
            [](const Sprite& sprite){return sprite.handle!=0;}),
            ExtendedWeaponId(active) || (digitsReady && definition &&
                (definition->ammo1<0 || nativeAmmo[0].handle) &&
                (definition->ammo2<0 || WeaponReserveAmmo(definition->ammo2)<=0 || nativeAmmo[1].handle)),
            digitsReady && std::all_of(std::begin(currency),std::end(currency),
                [](const Sprite& sprite){return sprite.handle!=0;}));
    }
    nativeVitalRuns={};
    const auto* styledDefinition=FindWeaponDefinition(active);
    const bool nativeWeapon=styledDefinition && !ExtendedWeaponId(active);
    PrepareStyledNativeAmmo(clip>=0,nativeWeapon && styledDefinition->ammo1>=0,
        nativeWeapon && styledDefinition->ammo2>=0 && WeaponReserveAmmo(styledDefinition->ammo2)>0);
    // Preserve the ordinary HUD and selection. Only replace the selected
    // weapon's bottom-right ammo sprites and divider while it is being drawn.
    if((Custom() || MagazineActive()) && (!ammoIcon.handle || loadedAmmoId!=active))LoadSprites();
    const auto* module=FindWeaponModule(active);
    adjustMoney=HudStyleMoneyVisible() || (!HudStyleWeaponVisible() && module &&
        module->secondaryVisible && module->secondaryVisible(hidden,intermission));
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
    for(const auto* item:WeaponModules())if(item->drawHud)
        item->drawHud(hidden,intermission,!HudStyleWeaponVisible());
    if(!HudStyleWeaponVisible())DrawMagazineAmmo(intermission);
    DrawHudStyle();
    if(HudStyleWeaponVisible() || !Custom() || intermission || (hidden&5) || !digits[0].handle || !ammoIcon.handle ||
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
    if(!DrawCharacterSkillInfiniteAmmo(active,iconX,y,BlueR,BlueG,BlueB))
        for(const char* digit=value;*digit;++digit)
        {
            const auto& item=digits[*digit-'0'];originalSet(item.handle,BlueR,BlueG,BlueB);
            originalDraw(0,x,y,&item.rect);x+=width;
        }
    originalSet(ammoIcon.handle,BlueR,BlueG,BlueB);originalDraw(0,iconX,iconY,&ammoIcon.rect);
    ++hudDraws;
    return result;
}

int ActiveWeaponHudId(){return active;}
hud_style::WeaponState CurrentWeaponHudState()
{
    hud_style::WeaponState result;
    const auto* definition=FindWeaponDefinition(active);
    if(!definition)return result;
    result.id=active;result.slot=definition->slot;result.clip=clip;
    result.reserve=WeaponReserveAmmo(definition->ammo1);
    result.secondary=WeaponReserveAmmo(definition->ammo2);
    const auto& current=CurrentExtendedWeapon();
    if(current.state && current.id==active)
    {result.reserve=current.reserve;result.secondary=current.secondary;}
    if(const auto* module=FindWeaponModule(active))
    {
        if(module->hudState)module->hudState(result);
        else if(engine.pfnGetCvarFloat("cl_lw")!=0 && module->predictedClip && module->predictedClip()>=0 &&
            module->ammoKind==hud_style::AmmoKind::Magazine)
        {
            result.clip=module->predictedClip();
            if(module->predictedReserve)result.reserve=std::max(0,module->predictedReserve());
            if(module->predictedSecondary)result.secondary=std::max(0,module->predictedSecondary());
        }
    }
    result.infinite=CharacterSkillInfiniteAmmoFor(active);
    if(Custom())
    {result.kind=hud_style::AmmoKind::Total;result.total=AmmoTotal();result.specialAmmo=true;}
    else if(definition->ammo1>=0)
    {
        result.kind=result.clip>=0?hud_style::AmmoKind::Magazine:hud_style::AmmoKind::Total;
        result.total=std::max(0,result.reserve);
    }
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
    for(const auto* module:WeaponModules())if(module->diagnostics)module->diagnostics(out);
}

void WeaponPostRunCmd(local_state_t* from,local_state_t* to,usercmd_t* cmd,int runFunctions,double time,unsigned int seed)
{
    ++postRunCalls;
    sourceWeaponId=from->client.m_iId;
    playerFlags=from->client.flags;
    playerSpeed=std::hypot(from->client.velocity[0],from->client.velocity[1]);
    // Reliable weapon selection can arrive before the prediction snapshot
    // catches up. Simulate old commands silently in either direction: stock
    // CS also reconciles animations when returning from an extended weapon.
    if(!WeaponPresentationAllowed(sourceWeaponId))runFunctions=0;
    for(const auto* module:WeaponModules())
        if(module->predict && module->predict(from,to,cmd,runFunctions,time,seed))return;
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
        const auto sync=runFunctions?PendingWeaponAnimationSync(sourceWeaponId):0;
        downstream.HUD_PostRunCmd(from,to,cmd,runFunctions,time,seed);
        if(sync)
        {
            // Stock's cached (animation, weapon) can already match this knife
            // while the engine still holds K3's Draw (knife's StabHit). Let
            // stock compute the real next animation, then synchronize once
            // on return. Replayed commands must not consume this handoff.
            if(from->client.health<=0 || from->client.deadflag || to->client.m_iId!=sourceWeaponId ||
                (engine.pfnGetCvarFloat && engine.pfnGetCvarFloat("cl_lw")==0))
                ConsumeWeaponAnimationSync(sync,sourceWeaponId);
            else if(to->client.viewmodel>0 && to->client.weaponanim>=0 && to->client.weaponanim<=255 &&
                engine.GetViewModel && engine.pfnWeaponAnim)
            {
                if(auto* model=engine.GetViewModel();model && ConsumeWeaponAnimationSync(sync,sourceWeaponId))
                    engine.pfnWeaponAnim(to->client.weaponanim,model->curstate.body);
            }
        }
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
    for(const auto* module:WeaponModules())if(module->receive)module->receive(current);
    active=current.state?current.id:0;clip=current.clip;reserve=current.reserve;
    ApplyNativeWeaponVisibility();
    predictedClip=predictedTotal=-1;
}
}
