// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "damage_display.hpp"
#include "hd_spray.hpp"
#include "../image_codec.hpp"
#include <IVGUI2Extension.h>
#include <vgui_controls/Controls.h>
#include <vgui_controls/PropertyPage.h>
#include <vgui_controls/ComboBox.h>
#include <vgui_controls/CheckButton.h>
#include <vgui_controls/Label.h>
#include <VGUI/IEngineVGui.h>
#include <IKeyValuesSystem.h>
#include <triangleapi.h>
#include <algorithm>
#include <cmath>
#include <cstring>

// Private copies in CSOLSupport, matching the SDK controls' Surface2 ABI.
vgui::ISurface2* g_pVGuiSurface2{};
vgui::ISchemeManager2* g_pVGuiSchemeManager2{};
vgui::IInput2* g_pVGuiInput2{};
cl_enginefunc_t gEngfuncs{}; // Matched SDK Cvar controls use this private table.
extern IKeyValuesSystem* g_pKeyValuesSystem;
extern vgui::IEngineVGui* g_pEngineVGui;

namespace csol::client
{
namespace
{
IVGUI2Extension* extension{};
bool ready{};
unsigned graphicsGeneration{};
struct Texture { int id{}; damage::TextureMetrics size; };
std::unordered_map<std::string, Texture> textures;
std::array<vgui::HFont,3> textFonts{};
HANDLE fontMemory{};
bool fontAttempted{};
vgui::DHANDLE<vgui::Panel> overlay;
std::vector<vgui::DHANDLE<vgui::Panel>> pages;

// Literal translations of the original configuration's Korean comments;
// stable numeric IDs are kept even when a resource is missing.
constexpr const wchar_t* names[]{L"原版文字", L"默认图像", L"毛笔字", L"万圣节", L"国服十二周年",
    L"圣诞节", L"恭贺新年", L"樱花", L"国服十三周年", L"赛季通行证 12", L"赛季通行证 13",
    L"赛季通行证 14", L"寒冰", L"霓虹 · 国服十四周年", L"赛季通行证 15", L"赛季通行证 16",
    L"赛季通行证 17", L"派对 · 国服十五周年", L"赛季通行证 18", L"竹子", L"赛季通行证 19",
    L"Nexon 三十周年", L"赛季通行证 20", L"棒球迷", L"赛季通行证 21", L"火焰", L"蛇年新春",
    L"赛季通行证 22", L"赛季通行证 23", L"荷叶饭", L"Good Week", L"生存者", L"黄金", L"月晕",
    L"奥术", L"死亡玫瑰", L"甜蜜杀戮者", L"赛季通行证 24", L"十七周年", L"赛季通行证 25",
    L"马年新春", L"钢铁", L"Luminous Brionac", L"赛季通行证 26", L"国服 Unite", L"Frost Viper EX",
    L"赛季通行证 27"};

void PrepareTexture(const std::string& resource)
{
    if (!ready || textures.contains(resource)) return;
    Texture texture;
    try
    {
        const auto image = DecodeTGA(ReadResource(resource.c_str()));
        if (image.Width > 4096 || image.Height > 4096) throw std::runtime_error("damage image too large");
        auto power = [](unsigned value) { unsigned n = 1; while (n < value) n *= 2; return n; };
        texture.size = {image.Width, image.Height, power(image.Width), power(image.Height)};
        // Preserve the original engine's padded allocation and widget UVs.
        std::vector<unsigned char> pixels(std::size_t(texture.size.paddedWidth) * texture.size.paddedHeight * 4);
        for (unsigned y = 0; y < image.Height; ++y)
            std::memcpy(pixels.data() + std::size_t(y) * texture.size.paddedWidth * 4,
                image.RGBA.data() + std::size_t(y) * image.Width * 4, image.Width * 4);
        texture.id = vgui::surface()->CreateNewTextureID(true);
        vgui::surface()->DrawSetTextureRGBA(texture.id, pixels.data(), texture.size.paddedWidth,
            texture.size.paddedHeight, 1, true);
    }
    catch (const std::exception& e) { engine.Con_DPrintf("CSOLSupport damage %s: %s\n", resource.c_str(), e.what()); }
    textures.emplace(resource, texture); // Missing assets are reported once, never loaded in Paint.
}
const Texture* FindTexture(const std::string& path)
{
    const auto it = textures.find(path);
    return it == textures.end() || !it->second.id ? nullptr : &it->second;
}
void PaintQuad(const Texture& texture, const damage::Quad& q)
{
    if (q.width <= 0 || q.height <= 0 || q.alpha == 0) return;
    vgui::surface()->DrawSetColor(255,255,255,q.alpha);
    vgui::surface()->DrawSetTexture(texture.id);
    vgui::surface()->DrawTexturedSubRect(int(q.x), int(q.y), int(q.x + q.width), int(q.y + q.height), q.u0,q.v0,q.u1,q.v1);
}
void PrepareText()
{
    if (fontAttempted || !ready) return;
    fontAttempted = true;
    try
    {
        auto bytes = ReadResource("resource/font/df_gb_y9.ttf");
        DWORD count{};
        fontMemory = AddFontMemResourceEx(bytes.data(), DWORD(bytes.size()), nullptr, &count);
        if (!fontMemory || !count) throw std::runtime_error("original damage font unavailable");
        const int heights[]{20,16,26};
        for (int i=0;i<3;++i)
        {
            textFonts[i] = vgui::surface()->CreateFont();
            if (!vgui::surface()->AddGlyphSetToFont(textFonts[i], "DFYuanW9-GB", heights[i], i==2?800:0,
                0,0,i==2?0:vgui::ISurface2::FONTFLAG_ANTIALIAS))
                throw std::runtime_error("original damage font creation failed");
        }
    }
    catch (const std::exception& e) { textFonts={}; engine.Con_DPrintf("CSOLSupport damage: %s\n",e.what()); }
}
void PaintText(const damage::Number& n, float x, float y)
{
    int style = (n.message.flags & damage::Critical) ? 2 : n.message.style;
    // Original cso_chn token CSO_ZBZ_OverHeadDmg_InfoType_Text_0.
    // Style5 has no text constructor; image skins own its MISS image.
    if (style == 5 || (style == 3 && n.message.value != 0)) return;
    const int font = style==1 ? 1 : (style==2 || style==3) ? 2 : 0;
    if (!textFonts[font]) return;
    wchar_t text[12]{};
    for (std::size_t i=0;i<n.Text().size();++i) text[i] = wchar_t(n.text[i]);
    const wchar_t* display=style==3?L"未命中":text;
    const int length=style==3?3:int(n.textLength);
    int width{},height{};
    vgui::surface()->GetTextSize(textFonts[font],display,width,height);
    const Color colors[]{Color(195,195,195,255), Color(128,0,255,254),Color(204,20,20,254),
        Color(250,240,0,254),Color(204,20,20,254),Color(195,195,195,255),Color(180,255,0,255)};
    vgui::surface()->DrawSetTextFont(textFonts[font]);
    vgui::surface()->DrawSetTextColor(colors[std::clamp(style,0,6)]);
    vgui::surface()->DrawSetTextPos(int(x-width*.5f),int(y));
    vgui::surface()->DrawPrintText(display,length);
}
void PaintNumber(const damage::Number& n, int skinId, damage::Anchor camera, float x, float y)
{
    if (skinId == 0) { PaintText(n,x,y); return; }
    const auto* skin = DamageSkin(skinId);
    const auto* part = skin ? skin->Find(damage::Style(n.message.style)) : nullptr;
    const auto* texture = part ? FindTexture(part->resource) : nullptr;
    if (!texture) return;
    if (n.message.style == unsigned(damage::Style::Miss))
    {
        if (const auto q = damage::LayoutMiss(n,*part,texture->size,camera,x,y)) PaintQuad(*texture,*q);
        return;
    }
    std::array<damage::Quad,12> quads;
    const auto count = damage::LayoutDigits(n,*part,texture->size,camera,x,y,quads);
    for (std::size_t i=0;i<count;++i) PaintQuad(*texture,quads[i]);
    for (const unsigned flag : {damage::Back,damage::Critical,damage::HealIcon})
    {
        if (!(n.message.flags & flag)) continue;
        const auto it = std::find_if(skin->icons.begin(),skin->icons.end(),[=](const auto& p){return p.type==flag;});
        if (it == skin->icons.end()) continue;
        const auto* icon = FindTexture(it->resource);
        if (!icon) continue;
        if (auto q=damage::LayoutIcon(n,*it,*part,camera,{quads.data(),count}))
        {
            q->u1=float(icon->size.width)/icon->size.paddedWidth;
            q->v1=float(icon->size.height)/icon->size.paddedHeight;
            PaintQuad(*icon,*q);
        }
    }
}
class DamageOverlay final : public vgui::Panel
{
public:
    DamageOverlay() : Panel(nullptr,"CSOLDamageNumbers")
    {
        SetParent(g_pEngineVGui->GetPanel(vgui::PANEL_CLIENTDLL));
        SetMouseInputEnabled(false); SetKeyBoardInputEnabled(false);
        SetPaintBackgroundEnabled(false); SetPaintBorderEnabled(false);
        SetVisible(true);
    }
    void OnThink() override
    {
        int width{},height{};vgui::surface()->GetScreenSize(width,height);SetBounds(0,0,width,height);
    }
    void Paint() override
    {
        if (!DamageEnabled() || engine.pfnGetCvarFloat("hud_draw")==0 || !engine.pTriAPI) return;
        int width{},height{};GetSize(width,height);
        const damage::Anchor camera{view.vieworg[0],view.vieworg[1],view.vieworg[2]};
        for (const auto& n : DamageEntries())
        {
            float world[]{n.anchor.x,n.anchor.y,n.anchor.z}, screen[3]{};
            if (DamageSelectedSkin()==0) world[2]+=n.age*20;
            if (engine.pTriAPI->WorldToScreen(world,screen)) continue;
            PaintNumber(n,DamageSelectedSkin(),camera,(screen[0]+1)*float(width/2),(1-screen[1])*float(height/2));
        }
    }
};
class DamagePreview final : public vgui::Panel
{
public:
    int skin{1}, style{}, flags{};
    explicit DamagePreview(vgui::Panel* parent):Panel(parent,"DamagePreview"){}
    void Paint() override
    {
        const auto* definition = DamageSkin(skin);
        if (definition)
            if (const auto* texture=FindTexture(definition->preview))
            {
                const float scale=(std::min)(130.0f/texture->size.width,55.0f/texture->size.height);
                PaintQuad(*texture,{14,12,texture->size.width*scale,texture->size.height*scale,0,0,
                    float(texture->size.width)/texture->size.paddedWidth,float(texture->size.height)/texture->size.paddedHeight,255});
            }
        damage::Number n;
        n.message={1,std::uint8_t(style),std::uint8_t(flags),12345};
        if(skin==0 && style==5)n.message={1,3,0,0};
        n.text={'1','2','3','4','5'};n.textLength=5;
        n.animation=definition?definition->animation:damage::Animation{1,0,0};
        n.age=n.effectiveAge=float(std::fmod(vgui::system()->GetCurrentTime(),n.animation.duration+0.6));
        if(n.age<=n.animation.duration)PaintNumber(n,skin,{},GetWide()*.5f,GetTall()-45.0f);
    }
};
class DamagePage final : public vgui::PropertyPage
{
    DECLARE_CLASS_SIMPLE(DamagePage,vgui::PropertyPage);
    vgui::CheckButton* show;
    vgui::ComboBox* skins;
    vgui::ComboBox* styles;
    vgui::Label* status;
    DamagePreview* preview;
    bool dirty{}, savedShow{};
    int savedSkin{}, preparedSkin{-1};
    unsigned preparedGeneration{};
    void PopulateSkins()
    {
        skins->DeleteAllItems();
        const auto add=[&](int id){wchar_t name[128];swprintf_s(name,L"%02d · %s",id,id<int(std::size(names))?names[id]:L"原版皮肤");
            auto* data=new KeyValues("skin");data->SetInt("id",id);skins->AddItem(name,data);data->deleteThis();};
        add(0);for(const auto& s:DamageSkins())add(s.id);
        preparedSkin=-1;
    }
public:
    explicit DamagePage(vgui::Panel* parent):PropertyPage(parent,"CSOLDamageOptions")
    {
        show=new vgui::CheckButton(this,"DamageEnabled","");show->SetText(L"显示伤害数字");show->AddActionSignalTarget(this);
        auto* label=new vgui::Label(this,"SkinTitle",L"原版数字皮肤");label->SetBounds(18,47,140,24);
        skins=new vgui::ComboBox(this,"DamageSkins",10,false);skins->AddActionSignalTarget(this);
        PopulateSkins();
        styles=new vgui::ComboBox(this,"DamageExamples",7,false);styles->AddActionSignalTarget(this);
        for(const auto* title:{L"普通伤害",L"爆头",L"暴击",L"背后攻击",L"未命中",L"治疗"})styles->AddItem(title,nullptr);
        styles->ActivateItemByRow(0);
        preview=new DamagePreview(this);
        status=new vgui::Label(this,"DamageStatus",L"选择皮肤查看预览；确定或应用后保存。");
        OnResetData();
    }
    void PerformLayout() override
    {
        BaseClass::PerformLayout();
        show->SetBounds(18,16,240,24);skins->SetBounds(18,75,GetWide()-36,26);
        styles->SetBounds(18,112,200,26);preview->SetBounds(18,149,GetWide()-36,(std::max)(100,GetTall()-205));
        status->SetBounds(18,GetTall()-49,GetWide()-36,43);
    }
    void OnThink() override
    {
        BaseClass::OnThink();
        // GameUI may construct Options before the client's HUD_Init reads the
        // catalog. Refresh that early page once the real resource list arrives.
        if (std::size_t(skins->GetItemCount()) != DamageSkins().size()+1)
        { PopulateSkins(); OnResetData(); }
        auto* data=skins->GetActiveItemUserData();preview->skin=data?data->GetInt("id",1):1;
        const int index=styles->GetActiveItem();
        constexpr int values[]{0,4,0,0,5,6}, flags[]{0,0,2,1,0,4};
        if(index>=0 && index<6){preview->style=values[index];preview->flags=flags[index];}
        if (!dirty && (show->IsSelected()!=savedShow || preview->skin!=savedSkin))
        {dirty=true;PostActionSignal(new KeyValues("ApplyButtonEnable"));}
        if (preparedSkin==preview->skin && preparedGeneration==graphicsGeneration) return;
        preparedSkin=preview->skin;preparedGeneration=graphicsGeneration;
        PrepareDamageGraphics(preview->skin);
        bool missing=preview->skin==0 && !textFonts[0];
        if(const auto* skin=DamageSkin(preview->skin))
        {
            missing=!FindTexture(skin->preview);
            for(const auto& p:skin->styles)missing|=!FindTexture(p.resource);
            for(const auto& p:skin->icons)missing|=!FindTexture(p.resource);
        }
        status->SetText(missing?L"此皮肤有资源缺失。\n预览和游戏中会跳过缺失部分。":
            L"确定或应用后保存。\n取消放弃尚未应用的修改。");
    }
    void OnResetData() override
    {
        savedShow=DamageEnabled();savedSkin=DamageSelectedSkin();show->SetSelected(savedShow);
        for(int row=0;row<skins->GetItemCount();++row)
        {const int item=skins->GetItemIDFromRow(row);if(skins->GetItemUserData(item)->GetInt("id")==DamageSelectedSkin())skins->ActivateItem(item);}
        dirty=false;
    }
    void OnApplyChanges() override
    {
        if(auto* data=skins->GetActiveItemUserData())ApplyDamageSettings(show->IsSelected(),data->GetInt("id",1));
        savedShow=DamageEnabled();savedSkin=DamageSelectedSkin();dirty=false;
    }
};
class OptionsCallbacks final : public IVGUI2Extension_GameUIOptionDialogCallbacks
{
public:
    int GetAltitude()const override{return 0;}
    void COptionsDialog_ctor(IGameUIOptionsDialogCtorCallbackContext* c)override
    {
        if(!ready)return;
        auto* page=new DamagePage(static_cast<vgui::Panel*>(c->GetDialog()));pages.emplace_back();pages.back()=page;
        c->AddPage(page,"#CSOL_DamageNumbers");
    }
    void COptionsSubVideo_ApplyVidSettings(void*&,bool&,VGUI2Extension_CallbackContext*)override{}
    void COptionsDialogSubPage_ctor(IGameUIOptionsDialogSubPageCtorCallbackContext* c)override{
        if(ready&&!std::strcmp(c->GetName(),"COptionsSubMultiplayer"))CreateSprayOptions(c->GetPage());
    }
    void COptionsSubPage_OnApplyChanges(void*& page,const char* name,VGUI2Extension_CallbackContext* c)override{
        if(c->IsPost&&!std::strcmp(name,"COptionsSubMultiplayer"))ApplySprayOptions(page);
    }
} optionCallbacks;
void ReleaseDamageUI()
{
    // BaseUI calls ClientVGUI_Shutdown before unhooking GameUI and unloading
    // vgui2.dll. Release DHANDLEs and textures while those interfaces live.
    if (!ready) return;
    ReleaseSprayOptions();
    for (auto& page : pages) if (page.Get()) delete page.Get();
    pages.clear();
    if (overlay.Get()) delete overlay.Get();
    overlay=nullptr;
    ResetDamageGraphics();
    if (fontMemory) { RemoveFontMemResourceEx(fontMemory); fontMemory=nullptr; }
    textFonts={};fontAttempted=false;ready=false;
}
class ClientCallbacks final : public IVGUI2Extension_ClientVGUICallbacks
{
public:
    int GetAltitude()const override{return 0;}
    void Initialize(CreateInterfaceFn*,int)override{}
    void Shutdown()override{ReleaseDamageUI();}
    void Start()override{if(ready && g_pEngineVGui && !overlay.Get())overlay=new DamageOverlay;}
    void SetParent(vgui::VPANEL parent)override{if(ready && overlay.Get())overlay->SetParent(parent);}
    void UseVGUI1(VGUI2Extension_CallbackContext*)override{}
    void HideScoreBoard(VGUI2Extension_CallbackContext*)override{}
    void HideAllVGUIMenu(VGUI2Extension_CallbackContext*)override{}
    void ActivateClientUI(VGUI2Extension_CallbackContext*)override{}
    void HideClientUI(VGUI2Extension_CallbackContext*)override{}
} clientCallbacks;
class GameCallbacks final : public IVGUI2Extension_GameUICallbacks
{
public:
    int GetAltitude()const override{return 0;}
    const char* GetControlModuleName()const override{return "CSOLSupport";}
    void Initialize(CreateInterfaceFn* factories,int count)override
    {
        const auto module=GetModuleHandleA("vgui2.dll");
        const auto factory=module?Sys_GetFactory(reinterpret_cast<HINTERFACEMODULE>(module)):nullptr;
        g_pKeyValuesSystem=factory?static_cast<IKeyValuesSystem*>(factory(KEYVALUESSYSTEM_INTERFACE_VERSION,nullptr)):nullptr;
        ready=count>=3 && g_pVGuiSurface2 && g_pVGuiInput2 && g_pVGuiSchemeManager2 &&
            vgui::VGui_InitInterfacesList("CSOLSupport",factories,count);
        if(ready){static wchar_t title[]=L"伤害数字";vgui::localize()->AddString("CSOL_DamageNumbers",title,"CSOLSupport");}
    }
    void PreStart(cl_enginefuncs_s*,int,void*)override{}
    void Start(cl_enginefuncs_s*,int,void*)override{}
    void Shutdown()override{ReleaseDamageUI();}
    void PostShutdown()override{}
    void ActivateGameUI(VGUI2Extension_CallbackContext*)override{}
    void ActivateDemoUI(VGUI2Extension_CallbackContext*)override{}
    void HasExclusiveInput(VGUI2Extension_CallbackContext*)override{}
    void RunFrame(VGUI2Extension_CallbackContext*)override{SprayOptionsFrame();}
    void ConnectToServer(const char*&,int&,int&,VGUI2Extension_CallbackContext*)override{ResetDamageDisplay();ResetSpray();}
    void DisconnectFromServer(VGUI2Extension_CallbackContext*)override{ResetDamageDisplay();ResetSpray();}
    void HideGameUI(VGUI2Extension_CallbackContext*)override{}
    void IsGameUIActive(VGUI2Extension_CallbackContext*)override{}
    void LoadingStarted(const char*&,const char*&,VGUI2Extension_CallbackContext*)override{}
    void LoadingFinished(const char*&,const char*&,VGUI2Extension_CallbackContext*)override{}
    void StartProgressBar(const char*&,int&,VGUI2Extension_CallbackContext*)override{}
    void ContinueProgressBar(int&,float&,VGUI2Extension_CallbackContext*)override{}
    void StopProgressBar(bool&,const char*&,const char*&,VGUI2Extension_CallbackContext*)override{}
    void SetProgressBarStatusText(const char*&,VGUI2Extension_CallbackContext*)override{}
    void SetSecondaryProgressBar(float&,VGUI2Extension_CallbackContext*)override{}
    void SetSecondaryProgressBarText(const char*&,VGUI2Extension_CallbackContext*)override{}
} gameCallbacks;
}
void InstallDamageOptions()
{
    gEngfuncs=engine;
    const auto module=GetModuleHandleA("VGUI2Extension.dll");
    const auto factory=module?Sys_GetFactory(reinterpret_cast<HINTERFACEMODULE>(module)):nullptr;
    if(!factory){engine.Con_Printf("CSOLSupport damage requires VGUI2Extension\n");return;}
    extension=static_cast<IVGUI2Extension*>(factory(VGUI2_EXTENSION_INTERFACE_VERSION,nullptr));
    g_pVGuiSurface2=static_cast<vgui::ISurface2*>(factory(VGUI_SURFACE2_INTERFACE_VERSION,nullptr));
    g_pVGuiSchemeManager2=static_cast<vgui::ISchemeManager2*>(factory(VGUI_SCHEME2_INTERFACE_VERSION,nullptr));
    g_pVGuiInput2=static_cast<vgui::IInput2*>(factory(VGUI_INPUT2_INTERFACE_VERSION,nullptr));
    if(!extension)return;
    extension->RegisterGameUICallbacks(&gameCallbacks);
    extension->RegisterGameUIOptionDialogCallbacks(&optionCallbacks);
    extension->RegisterClientVGUICallbacks(&clientCallbacks);
}
void PrepareDamageGraphics(int id)
{
    if(!ready)return;
    if(id==0){PrepareText();return;}
    if(const auto* skin=DamageSkin(id))
    {PrepareTexture(skin->preview);for(const auto& p:skin->styles)PrepareTexture(p.resource);for(const auto& p:skin->icons)PrepareTexture(p.resource);}
}
void ResetDamageGraphics()
{
    if(ready)for(const auto& [path,t]:textures)if(t.id)vgui::surface()->DeleteTextureByID(t.id);
    textures.clear();
    ++graphicsGeneration;
}
void ShutdownDamageOptions()
{
    if(extension){extension->UnregisterGameUICallbacks(&gameCallbacks);extension->UnregisterGameUIOptionDialogCallbacks(&optionCallbacks);
        extension->UnregisterClientVGUICallbacks(&clientCallbacks);extension=nullptr;}
    ReleaseDamageUI();
}
}
