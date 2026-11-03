// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "hud_style.hpp"
#include "user_message_chain.hpp"
#include "weapon_inventory.hpp"
#include "characters.hpp"
#include "scenario_upgrades.hpp"
#include "../image_codec.hpp"
#include <VGUI/ISurface.h>
#include <VGUI/IScheme.h>
#include <IVGUI2Extension.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cwchar>
#include <ostream>

namespace csol::client
{
namespace
{
namespace hud = hud_style;
vgui::ISurface* surface{};
vgui::ISchemeManager* schemes{};
IVGUI2Extension* extension{};
HANDLE privateFont{};
vgui::HScheme hudScheme{};
cvar_t* styleOption{};
cvar_t* scaleOption{};
hud::Catalog catalog;
hud::Vitals vitals;
hud::Money money;
double moneyChangedAt{};
bool moneyFadeActive{};
hud::Layout layout;
hud::WeaponState weapon;
struct Texture { int id{}; unsigned width{}, height{}; };
std::array<Texture,unsigned(hud::Art::Count)> art{};
std::array<Texture,10> digits{};
std::array<Texture,10> moneyDigits{};
Texture portrait, weaponImage, ammoImage, secondaryImage;
std::string portraitPath, weaponName;
std::wstring weaponTitle;
vgui::HFont titleFont{};
int* nativeHealth{};
int* nativeArmor{};
pfnUserMsgHook previousHealth{}, previousArmor{}, previousMoney{}, previousReset{}, previousInit{};
bool pendingVitals{}, loaded{}, reported{}, gameUI{}, console{}, requested{}, additiveSurface{};
bool vitalsVisible{}, weaponVisible{}, moneyVisible{}, moneyLoaded{}, moneyReported{};
int screenWidth{}, screenHeight{}, portraitId{};
ULONGLONG retryAfter{};
ULONGLONG selectionRetryAfter{};
ULONGLONG portraitRetryAfter{};
ULONGLONG moneyRetryAfter{};
unsigned draws{}, rejected{};

void Delete(Texture& texture)
{
    if (surface && texture.id) surface->DeleteTextureByID(texture.id);
    texture = {};
}
Texture Upload(const Image& image)
{
    if (!image.Width || !image.Height || image.Width > 2048 || image.Height > 2048)
        throw std::runtime_error("HUD image dimensions exceed limit");
    const int id = surface->CreateNewTextureID(true);
    if (!id) throw std::runtime_error("cannot create HUD texture");
    surface->DrawSetTextureRGBA(id,image.RGBA.data(),image.Width,image.Height,1,true);
    return {id,image.Width,image.Height};
}
Texture Load(const char* path)
{ return Upload(DecodeTGA(ReadResource(path))); }
void ReleaseBase()
{
    for (unsigned i=0;i<unsigned(hud::Art::MoneyBackground);++i) Delete(art[i]);
    for (auto& texture : digits) Delete(texture);
    Delete(portrait); Delete(weaponImage); Delete(ammoImage); Delete(secondaryImage);
    portraitId=0; portraitPath.clear(); weaponName.clear(); weaponTitle.clear();
    selectionRetryAfter=portraitRetryAfter=0;
    loaded=false; requested=vitalsVisible=weaponVisible=false;
}
void ReleaseMoney()
{
    for (unsigned i=unsigned(hud::Art::MoneyBackground);i<art.size();++i) Delete(art[i]);
    for (auto& texture:moneyDigits) Delete(texture);
    moneyLoaded=moneyVisible=false;
}
void Release(){ReleaseBase();ReleaseMoney();}
std::wstring Wide(const std::string& text)
{
    const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0);
    if (!count) throw std::runtime_error("invalid HUD localization");
    std::wstring result(count,L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),result.data(),count);
    return result;
}
vgui::HFont EngineFont()
{
    auto* scheme=schemes && hudScheme?schemes->GetIScheme(hudScheme):nullptr;
    return scheme?scheme->GetFont("EngineFont",true):0;
}
void Prepare()
{
    if (!surface || !schemes || loaded || GetTickCount64()<retryAfter) return;
    retryAfter=GetTickCount64()+5000;
    try
    {
        const auto bytes=ReadResource(hud::CatalogPath);
        auto candidate=hud::ParseCatalog({reinterpret_cast<const char*>(bytes.data()),bytes.size()});
        for (unsigned i=0;i<unsigned(hud::Art::MoneyBackground);++i)
        {
            if (art[i].id) continue;
            const auto& resource=hud::Resources[i];
            const auto image=DecodeTGA(ReadResource(resource.path));
            if (image.Width!=resource.width || image.Height!=resource.height)
                throw std::runtime_error("original HUD image dimensions changed");
            if (i==unsigned(hud::Art::WhiteDigits))
            {
                // HL25's additive surface entry accepts a complete texture.
                // Slice the atlas once during preparation, never in Paint.
                Image cell{17,22,std::vector<std::uint8_t>(17*22*4)};
                for (unsigned digit=0;digit<digits.size();++digit)
                {
                    for (unsigned y=0;y<22;++y)
                        std::copy_n(image.RGBA.data()+(y*170+digit*17)*4,17*4,cell.RGBA.data()+y*17*4);
                    digits[digit]=Upload(cell);
                }
            }
            else art[i]=Upload(image);
        }
        if (!privateFont)
        {
            const auto font=ReadResource(hud::FontPath); DWORD count{};
            privateFont=AddFontMemResourceEx(const_cast<std::byte*>(font.data()),DWORD(font.size()),nullptr,&count);
            if (!privateFont) throw std::runtime_error("cannot load CSOL HUD font");
        }
        // Use CSO's complete EngineFont scheme entry. The VGUI scheme owns
        // resolution ranges, weight and shadow; do not guess or shrink fonts.
        if (ReadResource(hud::SchemePath).empty()) throw std::runtime_error("missing CSOL HUD scheme");
        hudScheme=schemes->LoadSchemeFromFile(hud::SchemePath,"CSOLHudV1");
        auto* scheme=hudScheme?schemes->GetIScheme(hudScheme):nullptr;
        const auto* revision=scheme?scheme->GetResourceString("CSOLHudRevision"):nullptr;
        if (!revision || std::string_view(revision)!="1" || !EngineFont())
            throw std::runtime_error("cannot load original CSOL EngineFont");
        catalog=std::move(candidate); loaded=true;
    }
    catch (const std::exception& error)
    {
        // Do not suppress a usable native HUD while downloads are incomplete.
        ReleaseBase();
        if (!reported) engine.Con_DPrintf("CSOLSupport: HUD style: %s\n",error.what());
        reported=true;
    }
}
void PrepareMoney()
{
    if (!surface || moneyLoaded || GetTickCount64()<moneyRetryAfter) return;
    moneyRetryAfter=GetTickCount64()+5000;
    try
    {
        for (unsigned i=unsigned(hud::Art::MoneyBackground);i<art.size();++i)
        {
            const auto& resource=hud::Resources[i];
            const auto image=DecodeTGA(ReadResource(resource.path));
            if (image.Width!=resource.width || image.Height!=resource.height)
                throw std::runtime_error("original money image dimensions changed");
            if (i==unsigned(hud::Art::MoneyDigits))
            {
                Image cell{13,18,std::vector<std::uint8_t>(13*18*4)};
                for (unsigned digit=0;digit<moneyDigits.size();++digit)
                {
                    for (unsigned y=0;y<18;++y)
                        std::copy_n(image.RGBA.data()+(y*130+digit*13)*4,13*4,cell.RGBA.data()+y*13*4);
                    moneyDigits[digit]=Upload(cell);
                }
            }
            else art[i]=Upload(image);
        }
        moneyLoaded=true;
    }
    catch (const std::exception& error)
    {
        ReleaseMoney();
        if (!moneyReported) engine.Con_DPrintf("CSOLSupport: HUD money: %s\n",error.what());
        moneyReported=true;
    }
}
void PrepareSelection()
{
    const auto* definition=FindWeaponDefinition(CurrentWeaponHudState().id);
    static const std::string empty;
    const auto& name=definition?definition->name:empty;
    if (weaponName!=name)
    {
        Delete(weaponImage); Delete(ammoImage); Delete(secondaryImage);
        weaponName=name; weaponTitle.clear();
        selectionRetryAfter=0;
    }
    if (const auto found=catalog.weapons.find(name); found!=catalog.weapons.end() &&
        !weaponImage.id && GetTickCount64()>=selectionRetryAfter)
    {
        selectionRetryAfter=GetTickCount64()+5000;
        // Commit the complete row together. A missing ammo icon must not
        // discard the native ammo display while showing only a silhouette.
        Texture image, ammo, secondary;
        try
        {
            const auto& item=found->second;
            auto title=std::to_wstring(definition->slot+1)+L" "+Wide(item.title);
            image=Load(item.weapon.c_str());
            if (!item.ammo.empty()) ammo=Load(item.ammo.c_str());
            if (!item.ammo2.empty()) secondary=Load(item.ammo2.c_str());
            weaponTitle=std::move(title); weaponImage=image; ammoImage=ammo; secondaryImage=secondary;
        }
        catch (const std::exception& error)
        {
            Delete(image); Delete(ammo); Delete(secondary);
            if (!reported) engine.Con_DPrintf("CSOLSupport: HUD weapon: %s\n",error.what());
            reported=true;
        }
    }
    const auto* character=ViewCharacter();
    const int id=character?character->id:0;
    const auto found=catalog.portraits.find(id);
    const auto& path=found==catalog.portraits.end()?empty:found->second;
    if (portraitId!=id || portraitPath!=path)
    { Delete(portrait); portraitId=id; portraitPath=path; portraitRetryAfter=0; }
    if (!portrait.id && !path.empty() && GetTickCount64()>=portraitRetryAfter)
    {
        portraitRetryAfter=GetTickCount64()+5000;
        try { portrait=Load(path.c_str()); }
        catch (const std::exception& error)
        {
            if (!reported) engine.Con_DPrintf("CSOLSupport: HUD portrait: %s\n",error.what());
            reported=true;
        }
    }
}
void Quad(const Texture& texture,float x,float y,float width,float height,
    float u0=0,float v0=0,float u1=1,float v1=1)
{
    if (!texture.id || width<=0 || height<=0) return;
    surface->DrawSetTexture(texture.id);
    vgui::VGuiVertex vertices[]{{int(x),int(y),u0,v0},{int(x+width),int(y),u1,v0},
        {int(x+width),int(y+height),u1,v1},{int(x),int(y+height),u0,v1}};
    surface->DrawTexturedPolygon(vertices,4);
}
void Art(hud::Art image,const hud::Rect& rect)
{ Quad(art[unsigned(image)],rect.x,rect.y,rect.width,rect.height); }
void Sprite(const Texture& texture,float x,float y,float width,float height)
{
    if (!additiveSurface || !texture.id || width<=0 || height<=0) return;
    surface->DrawSetTexture(texture.id);
    // The exporter preserves source RGB as RGBA.rgb * RGBA.a. These SPR
    // glyphs use additive blending; panel/portrait TGAs keep normal alpha.
    static_cast<vgui::ISurface_HL25*>(surface)->DrawTexturedRectAdd(
        int(x),int(y),int(x+width),int(y+height));
}
void Text(const wchar_t* text,float x,float y,vgui::HFont font)
{
    if (!font) return;
    surface->DrawSetTextFont(font); surface->DrawSetTextColor(255,255,255,255);
    surface->DrawSetTextPos(int(x),int(y)); surface->DrawPrintText(text,int(std::wcslen(text)));
    surface->DrawFlushText();
}
void Number(int value,float x,float y,bool blue=false,bool right=false,unsigned maxDigits=10)
{
    char text[16]; const auto end=std::to_chars(text,text+sizeof(text),std::max(0,value)).ptr;
    const auto first=end-std::min(unsigned(end-text),maxDigits);
    const int count=int(end-first);
    const float scale=layout.scale;
    if (right) x-=count*17*scale;
    if (value<=0) surface->DrawSetColor(75,75,75,255);
    else surface->DrawSetColor(blue?114:255,blue?197:255,255,255);
    for (auto p=first;p!=end;++p,x+=17*scale)
        Sprite(digits[unsigned(*p-'0')],x,y,17*scale,22*scale);
    surface->DrawSetColor(255,255,255,255);
}
void MoneyNumber(int value,float x,float y)
{
    char text[16];const auto end=std::to_chars(text,text+sizeof(text),value).ptr;
    const float s=layout.scale;
    // Original DrawHudNumber uses "%5i": leading blanks reserve five cells.
    x+=std::max(0,5-int(end-text))*13*s;
    for (auto p=text;p!=end;++p,x+=13*s)
        Sprite(moneyDigits[unsigned(*p-'0')],x,y,13*s,18*s);
}
void PaintMoney()
{
    const float s=layout.scale;
    const auto panel=hud::MoneyBackground(s,ScenarioHudLevel()>=0);
    Art(hud::Art::MoneyBackground,panel);
    const double now=engine.GetClientTime?engine.GetClientTime():moneyChangedAt;
    const double age=std::isfinite(now) && now>=moneyChangedAt?now-moneyChangedAt:5;
    if (age>=5) moneyFadeActive=false;
    const float remaining=moneyFadeActive?std::clamp(float(1-age/5),0.f,1.f):0;
    const std::array<int,3> accent=money.delta>0?std::array<int,3>{180,225,92}:
        money.delta<0?std::array<int,3>{231,26,26}:std::array<int,3>{255,255,255};
    // Original Account draw first interpolates integer RGB, then applies
    // ScaleColors with a separately truncated 100+128*remaining brightness.
    const float brightness=moneyFadeActive?float(int(100+128*remaining))/255.f:1.f;
    std::array<int,3> color{};
    for (unsigned i=0;i<color.size();++i)
        color[i]=int(int(255*(1-remaining)+accent[i]*remaining)*brightness);
    surface->DrawSetColor(color[0],color[1],color[2],255);
    const float x=69*s,y=panel.y+5*s;
    Sprite(art[unsigned(hud::Art::MoneyDollar)],51*s,y,18*s,18*s);
    MoneyNumber(money.amount,x,y);
    if (money.trackChange && age<2.5)
    {
        const float fade=float(int(std::clamp(1-age/2.5,0.0,1.0)*256))/255.f;
        surface->DrawSetColor(int(color[0]*fade),int(color[1]*fade),int(color[2]*fade),255);
        Sprite(art[unsigned(money.delta>0?hud::Art::MoneyPlus:hud::Art::MoneyMinus)],56*s,y+27*s,13*s,18*s);
        MoneyNumber(money.delta>0?money.delta:-money.delta,x,y+27*s);
    }
    surface->DrawSetColor(255,255,255,255);
}
void Paint()
{
    if (!requested) return;
    requested=false;
    if (!surface || gameUI || console || (!loaded && !moneyLoaded) || engine.pfnGetCvarFloat("hud_draw")==0) return;
    const auto root=surface->GetEmbeddedPanel(); if (!root) return;
    surface->PushMakeCurrent(root,false);
    surface->DrawSetColor(255,255,255,255);
    const float s=layout.scale;
    if (moneyVisible) PaintMoney();
    if (vitalsVisible)
    {
        surface->DrawSetColor(255,255,255,225);
        Art(hud::Art::CharacterBottom,layout.character);
        Art(hud::Art::CharacterTop,layout.character);
        // These are the HUD's own cut-out portraits, not the menu's full card.
        const float portraitU=portrait.width==portrait.height?1.f-99.f/116.f:0;
        Quad(portrait,layout.portrait.x,layout.portrait.y,layout.portrait.width,layout.portrait.height,portraitU);
        surface->DrawSetColor(255,255,255,255);
        Sprite(art[unsigned(hud::Art::Health)],9*s,screenHeight-34*s,16*s,16*s);
        Number(vitals.health,28*s,screenHeight-39*s);
        Sprite(art[unsigned(hud::Art::Armor)],160*s,screenHeight-34*s,16*s,16*s);
        Number(vitals.armor,179*s,screenHeight-39*s);
    }
    if (weaponVisible)
    {
        surface->DrawSetColor(255,255,255,225);
        Art(hud::Art::WeaponBottom,layout.weapon); Art(hud::Art::WeaponTop,layout.weapon);
        surface->DrawSetColor(255,255,255,225);
        Art(hud::Art::WeaponCard,{layout.card.x,layout.card.y,170*s,60*s});
        surface->DrawSetColor(255,255,255,255);
        Text(weaponTitle.c_str(),layout.card.x+8*s,layout.card.y+4*s,titleFont);
        Sprite(weaponImage,layout.card.x,layout.card.y+15*s,weaponImage.width*s,weaponImage.height*s);
        const float y=screenHeight-39*s;
        const float reserveRight=screenWidth-(10+ammoImage.width)*s;
        const auto infinity=[&](float x,float top,bool blue)
        {
            surface->DrawSetColor(blue?114:255,blue?197:255,255,255);
            Sprite(art[unsigned(hud::Art::Infinite)],x,top,36*s,24*s);
            surface->DrawSetColor(255,255,255,255);
        };
        if (weapon.kind==hud::AmmoKind::Magazine)
        {
            Number(weapon.clip,screenWidth-110*s,y,false,true);
            surface->DrawSetColor(75,75,75,255);
            surface->DrawFilledRect(int(screenWidth-106*s),int(y),int(screenWidth-104*s),int(y+22*s));
            surface->DrawSetColor(255,255,255,255);
            if (weapon.infinite)
                infinity(screenWidth-(72+ammoImage.width)*s,y+2*s,false);
            else Number(weapon.reserve,reserveRight,y,false,true);
        }
        else if (weapon.kind==hud::AmmoKind::Total)
        {
            // CSO's no-clip and special branches reserve three forward cells
            // starting at W-4*digitWidth-iconWidth. It does not use the
            // magazine reserve's W-10-iconWidth right edge.
            if (weapon.infinite)
                infinity(screenWidth-(72+ammoImage.width)*s,y+2*s,weapon.specialAmmo);
            else Number(weapon.total,screenWidth-(17+ammoImage.width)*s,y,weapon.specialAmmo,true,3);
        }
        if (weapon.kind!=hud::AmmoKind::None)
        {
            surface->DrawSetColor(weapon.specialAmmo?114:255,weapon.specialAmmo?197:255,255,255);
            Sprite(ammoImage,screenWidth-(8+ammoImage.width)*s,screenHeight-41*s,ammoImage.width*s,ammoImage.height*s);
            surface->DrawSetColor(255,255,255,255);
        }
        if (weapon.secondary>0 && secondaryImage.id)
        {
            if (weapon.secondary==255) infinity(screenWidth-(206+36)*s,y,true);
            else Number(weapon.secondary,screenWidth-208*s,y,true,true);
            surface->DrawSetColor(114,197,255,255);
            Sprite(secondaryImage,screenWidth-206*s,screenHeight-41*s,secondaryImage.width*s,secondaryImage.height*s);
            surface->DrawSetColor(255,255,255,255);
        }
    }
    surface->PopMakeCurrent(root); ++draws;
}
class Overlay final:public IVGUI2Extension_BaseUICallbacks
{
public:
    int GetAltitude()const override{return 21;}
    void Initialize(CreateInterfaceFn*,int)override{}
    void Start(cl_enginefuncs_s*,int)override{}
    void Shutdown()override{ResetHudStyle();Release();surface=nullptr;hudScheme=0;}
    void Key_Event(int&,int&,const char*&,VGUI2Extension_CallbackContext*)override{}
    void CallEngineSurfaceAppProc(void*&,void*&,VGUI2Extension_CallbackContext*)override{}
    void CallEngineSurfaceWndProc(void*&,unsigned int&,unsigned int&,long&,VGUI2Extension_CallbackContext*)override{}
    void Paint(int&,int&,int&,int&,VGUI2Extension_CallbackContext* context)override{if(context->IsPost)csol::client::Paint();}
    void HideGameUI(VGUI2Extension_CallbackContext* context)override{if(context->IsPost)gameUI=false;}
    void ActivateGameUI(VGUI2Extension_CallbackContext* context)override{if(!context->IsPost)gameUI=true;}
    void HideConsole(VGUI2Extension_CallbackContext* context)override{if(context->IsPost)console=false;}
    void ShowConsole(VGUI2Extension_CallbackContext* context)override{if(!context->IsPost)console=true;}
} overlay;
int Health(const char* name,int size,void* data)
{
    SynchronizeSession();
    if (data && size>=0 && !vitals.Health({static_cast<const std::byte*>(data),std::size_t(size)})) ++rejected;
    return previousHealth?previousHealth(name,size,data):1;
}
int Armor(const char* name,int size,void* data)
{
    SynchronizeSession();
    if (data && size>=0 && !vitals.Armor({static_cast<const std::byte*>(data),std::size_t(size)})) ++rejected;
    return previousArmor?previousArmor(name,size,data):1;
}
int MoneyMessage(const char* name,int size,void* data)
{
    SynchronizeSession();
    if (!data || size<0 || !money.ApplyMoney({static_cast<const std::byte*>(data),std::size_t(size)})) ++rejected;
    else if (money.changed)
    {
        const double now=engine.GetClientTime?engine.GetClientTime():0;
        moneyChangedAt=std::isfinite(now)?now:0;
        moneyFadeActive=true;
    }
    else if (money.amount<0) moneyFadeActive=false;
    return previousMoney?previousMoney(name,size,data):1;
}
int Reset(const char* name,int size,void* data)
{ ResetHudStyle(); return previousReset?previousReset(name,size,data):1; }
int Init(const char* name,int size,void* data)
{ ResetHudStyle(); return previousInit?previousInit(name,size,data):1; }
}
void InitializeHudStyle()
{
    ResetHudStyle(); pendingVitals=true;
    additiveSurface=api->GetEngineType()==ENGINE_GOLDSRC_HL25;
    nativeHealth=nativeArmor=nullptr;
    styleOption=engine.pfnGetCvarPointer("csol_hud_style");
    if (!styleOption) styleOption=engine.pfnRegisterVariable("csol_hud_style","1",FCVAR_ARCHIVE);
    scaleOption=engine.pfnGetCvarPointer("csol_hud_scale");
    if (!scaleOption) scaleOption=engine.pfnRegisterVariable("csol_hud_scale","1",FCVAR_ARCHIVE);
    InstallChainedUserMessage("ResetHUD",Reset,previousReset);
    InstallChainedUserMessage("InitHUD",Init,previousInit);
    InstallChainedUserMessage("Money",MoneyMessage,previousMoney);
    if (!extension)
    {
        const auto factory=api->GetEngineFactory();
        surface=factory?static_cast<vgui::ISurface*>(factory(VGUI_SURFACE_INTERFACE_VERSION,nullptr)):nullptr;
        // Like VGUI2Extension's BaseUI, obtain schemes from vgui2.dll.
        // The engine factory supplies the surface, but not the scheme manager.
        const auto vguiModule=reinterpret_cast<HINTERFACEMODULE>(GetModuleHandleA("vgui2.dll"));
        const auto vguiFactory=vguiModule?Sys_GetFactory(vguiModule):nullptr;
        schemes=vguiFactory?static_cast<vgui::ISchemeManager*>(vguiFactory(VGUI_SCHEME_INTERFACE_VERSION,nullptr)):nullptr;
        const auto module=api->GetPluginModuleHandleByBaseFileName("VGUI2Extension");
        const auto pluginFactory=module?Sys_GetFactory(module):nullptr;
        extension=pluginFactory?static_cast<IVGUI2Extension*>(pluginFactory(VGUI2_EXTENSION_INTERFACE_VERSION,nullptr)):nullptr;
        if (extension && surface) extension->RegisterBaseUICallbacks(&overlay);
    }
}
void HudStyleFrame()
{
    requested=vitalsVisible=weaponVisible=moneyVisible=false;
    if (pendingVitals)
    {
        pendingVitals=false;
        // GoldCraft checks the original handlers after HUD_Init returns.
        // Install observation only now, keeping its LONG read adapters intact.
        InstallChainedUserMessage("Health",Health,previousHealth);
        InstallChainedUserMessage("Battery",Armor,previousArmor);
        void* value{};
        if (api->ResolveGameSymbol(api->GetClientBase(),"GoldCraftVitals_Health",MH_GAMESYMBOL_KIND_GLOBAL,&value)==MH_GAMESYMBOL_OK)
            nativeHealth=static_cast<int*>(value);
        value=nullptr;
        if (api->ResolveGameSymbol(api->GetClientBase(),"GoldCraftVitals_Armor",MH_GAMESYMBOL_KIND_GLOBAL,&value)==MH_GAMESYMBOL_OK)
            nativeArmor=static_cast<int*>(value);
    }
    // Seed sign-on values that arrived before deferred observers were installed.
    if (nativeHealth) {vitals.health=std::max(0,*nativeHealth);vitals.healthKnown=true;}
    if (nativeArmor) {vitals.armor=std::max(0,*nativeArmor);vitals.armorKnown=true;}
    if (!surface || !extension || !additiveSurface || !styleOption || styleOption->value==0) return;
    const auto level=engine.pfnGetLevelName(); if (!level || !*level) return;
    Prepare(); PrepareMoney(); if (!loaded && !moneyLoaded) return;
    surface->GetScreenSize(screenWidth,screenHeight);
    layout=hud::MakeLayout(screenWidth,screenHeight,scaleOption?scaleOption->value:1);
    if (!loaded) return;
    PrepareSelection();
    titleFont=EngineFont();
}
void BeginHudStyle(int hidden,int intermission)
{
    vitalsVisible=weaponVisible=moneyVisible=false;
    if (!surface || !extension || !additiveSurface || !styleOption || styleOption->value==0 ||
        layout.scale<=0 || !surface->GetEmbeddedPanel() ||
        gameUI || console || intermission || view.spectator || view.health<=0 || (hidden&4) ||
        engine.pfnGetCvarFloat("hud_draw")==0) return;
    moneyVisible=moneyLoaded && money.known && money.amount>=0 && !(hidden&32);
    if (!loaded || !titleFont) return;
    vitalsVisible=!(hidden&8) && vitals.healthKnown && vitals.armorKnown;
    weapon=CurrentWeaponHudState();
    const auto* definition=FindWeaponDefinition(weapon.id);
    weaponVisible=!(hidden&1) && weapon.id>0 && definition && definition->name==weaponName &&
        weaponImage.id && titleFont && !weaponTitle.empty() &&
        (weapon.kind==hud::AmmoKind::None || ammoImage.id);
}
bool HudStyleVitalsVisible(){return vitalsVisible;}
hud_style::Vitals HudStyleVitalsState(){return vitals;}
void ConstrainHudStyle(bool nativeVitalsReady,bool nativeWeaponReady,bool nativeMoneyReady)
{vitalsVisible=vitalsVisible&&nativeVitalsReady;weaponVisible=weaponVisible&&nativeWeaponReady;moneyVisible=moneyVisible&&nativeMoneyReady;}
bool HudStyleWeaponVisible(){return weaponVisible;}
bool HudStyleMoneyVisible(){return moneyVisible;}
hud_style::Money HudStyleMoneyState(){return money;}
float HudStyleScale(){return layout.scale;}
void DrawHudStyle(){requested=vitalsVisible || weaponVisible || moneyVisible;}
void ResetHudStyle(){vitals={};weapon={};money={};moneyChangedAt=0;moneyFadeActive=false;requested=vitalsVisible=weaponVisible=moneyVisible=false;}
void HudStyleVideoInit(){Release();retryAfter=moneyRetryAfter=0;reported=moneyReported=false;}
void ShutdownHudStyle()
{
    ResetHudStyle();Release();catalog={};
    if (extension && surface) extension->UnregisterBaseUICallbacks(&overlay);
    if (privateFont) RemoveFontMemResourceEx(privateFont);
    privateFont=nullptr;hudScheme=0;surface=nullptr;schemes=nullptr;extension=nullptr;
    nativeHealth=nativeArmor=nullptr;pendingVitals=false;gameUI=console=false;
    additiveSurface=false;
}
void HudStyleDiagnostics(std::ostream& out)
{
    out<<"hudStyle ready="<<loaded<<" vitals="<<vitalsVisible<<" weapon="<<weaponVisible
       <<" money="<<moneyVisible<<" moneyReady="<<moneyLoaded<<" balance="<<money.amount
       <<" health="<<vitals.health<<" armor="<<vitals.armor<<" id="<<weapon.id
       <<" scale="<<layout.scale<<" additive="<<additiveSurface<<" draws="<<draws<<" rejected="<<rejected<<'\n';
    out<<"hudStyle interfaces surface="<<(surface!=nullptr)<<" schemes="<<(schemes!=nullptr)
       <<" extension="<<(extension!=nullptr)<<" font="<<titleFont<<" portrait="<<portrait.id
       <<" weaponImage="<<weaponImage.id<<" ammoImage="<<ammoImage.id<<" secondaryImage="<<secondaryImage.id
       <<" clip="<<weapon.clip<<" reserve="<<weapon.reserve<<" secondary="<<weapon.secondary<<'\n';
}
}
