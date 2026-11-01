// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "user_message_chain.hpp"
#include "boss_health.hpp"
#include "../boss_health.hpp"
#include "../image_codec.hpp"
#include <ostream>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <cwchar>
#include <VGUI/ISurface.h>
#include <IVGUI2Extension.h>

namespace csol::client
{
namespace
{
boss_health::State state;
vgui::ISurface* surface{};
IVGUI2Extension* extension{};
std::array<int, boss_health::Resources.size()> textures{};
std::array<int, boss_health::AbilityResources.size()> abilityTextures{};
vgui::HFont abilityFont{};
HANDLE abilityFontMemory{};
pfnUserMsgHook previousMessage{}, previousInit{}, previousHide{}, previousReset{};
bool prepared{}, requested{}, gameUI{}, console{};
int hidden{};
std::uint64_t accepted{}, malformed{};
std::array<float, 2> impact{}; // Original second CRT draw=pitch, first=yaw.
std::uint64_t impacts{}, stuns{};

void Release()
{
    if (surface) for (const int texture : textures) if (texture) surface->DeleteTextureByID(texture);
    if (surface) for (const int texture : abilityTextures) if (texture) surface->DeleteTextureByID(texture);
    textures = {};
    abilityTextures = {};
    prepared = false;
}
void Prepare()
{
    if (prepared || !surface) return;
    prepared = true;
    for (std::size_t i = 0; i != textures.size(); ++i)
    {
        try
        {
            const auto image = DecodeTGA(ReadResource(boss_health::Resources[i]));
            if (image.Width != boss_health::Dimensions[i][0] || image.Height != boss_health::Dimensions[i][1])
                throw std::runtime_error("boss health image dimensions changed");
            const int texture = surface->CreateNewTextureID(true);
            if (texture) surface->DrawSetTextureRGBA(texture, image.RGBA.data(), image.Width, image.Height, 1, true);
            textures[i] = texture;
        }
        catch (const std::exception& error)
        { engine.Con_DPrintf("CSOLSupport: %s: %s\n", boss_health::Resources[i], error.what()); }
    }
    for (std::size_t i = 0; i < abilityTextures.size(); ++i)
    {
        try
        {
            const auto image = DecodeTGA(ReadResource(boss_health::AbilityResources[i]));
            if (image.Height != 24 || image.Width != (i == 1 ? 1 : 10))
                throw std::runtime_error("elite ability image dimensions changed");
            const int texture = surface->CreateNewTextureID(true);
            if (texture) surface->DrawSetTextureRGBA(texture, image.RGBA.data(), image.Width, image.Height, 1, true);
            abilityTextures[i] = texture;
        }
        catch (const std::exception& error)
        { engine.Con_DPrintf("CSOLSupport: %s: %s\n", boss_health::AbilityResources[i], error.what()); }
    }
    if (!abilityFontMemory)
    {
        try
        {
            const auto bytes = ReadResource("resource/font/df_gb_y9.ttf");
            DWORD count{};
            abilityFontMemory = AddFontMemResourceEx(const_cast<std::byte*>(bytes.data()), DWORD(bytes.size()), nullptr, &count);
        }
        catch (const std::exception& error)
        { engine.Con_DPrintf("CSOLSupport: elite font: %s\n", error.what()); }
    }
    if (abilityFontMemory && !abilityFont)
    {
        const auto font = surface->CreateFont();
        // Original ClientScheme ChatDefault: fixed14px, weight300, AA.
        if (font && surface->AddGlyphSetToFont(font, "DFYuanW9-GB", 14, 300, 0, 0,
            vgui::FONTFLAG_ANTIALIAS, 0, 0xffff)) abilityFont = font;
    }
}
void PaintAbilities(int width, int height, int panelX)
{
    const auto abilities = state.Abilities();
    if (!abilityFont || abilities.empty() || std::any_of(abilityTextures.begin(), abilityTextures.end(), [](int n){return !n;})) return;
    //029BAA90 creates the panel at the health background's x,113*h/768;
    // its width scales with screen width, while the labels retain25px height.
    const int panelWidth = int(float(width) * (1.0f/1024.0f) * 578.0f);
    const int panelHeight = int(float(height) / 768.0f * 52.0f);
    const int panelY = int(float(height) / 768.0f * 113.0f);
    int x = 9, y = 0;
    for (const int id : abilities)
    {
        const auto* name = boss_health::AbilityNames[id];
        int textWidth{}, textHeight{};
        surface->GetTextSize(abilityFont, name, textWidth, textHeight);
        const int labelWidth = textWidth + 20;
        if (x + labelWidth > panelWidth) { x = 9; y += 27; }
        if (y + 25 > panelHeight) break;
        const int left = panelX + x, top = panelY + y;
        surface->DrawSetColor(255,255,255,255);
        for (unsigned part = 0; part < abilityTextures.size(); ++part)
        {
            const int a = part == 0 ? left : part == 1 ? left + 10 : left + labelWidth - 10;
            const int b = part == 0 ? left + 10 : part == 1 ? left + labelWidth - 10 : left + labelWidth;
            surface->DrawSetTexture(abilityTextures[part]);
            surface->DrawTexturedRect(a,top,b,top+25);
        }
        surface->DrawSetTextFont(abilityFont); surface->DrawSetTextColor(255,255,255,255);
        surface->DrawSetTextPos(left+10,top+5);
        surface->DrawPrintText(name,static_cast<int>(std::wcslen(name)));
        x += labelWidth + 9;
    }
}
void Paint()
{
    if (!requested) return;
    requested = false;
    const auto* current = state.Current();
    if (!surface || !prepared || !current || gameUI || console || (hidden & 4) ||
        engine.pfnGetCvarFloat("hud_draw") == 0) return;
    const int left = state.Appearance() == 18 ? 0 : state.Appearance() == 125 ? 4 : 3;
    if (!textures[left] || !textures[1] || !textures[2]) return;
    int width{}, height{};
    surface->GetScreenSize(width, height);
    const auto root = surface->GetEmbeddedPanel();
    if (!root) return;
    const auto layout = boss_health::MakeLayout(width, height, current->ratio);
    surface->PushMakeCurrent(root, false);
    surface->DrawSetColor(255, 255, 255, 255);
    for (unsigned i = 0; i != layout.background.size(); ++i)
    {
        const auto& rectangle = layout.background[i];
        surface->DrawSetTexture(textures[i == 0 ? left : i]);
        surface->DrawTexturedRect(rectangle.x, rectangle.y,
            rectangle.x + rectangle.width, rectangle.y + rectangle.height);
    }
    const auto& bar = layout.gauge;
    const auto& color = layout.color;
    surface->DrawSetColor(color[0], color[1], color[2], color[3]);
    surface->DrawFilledRect(bar.x, bar.y, bar.x + bar.width, bar.y + bar.height);
    PaintAbilities(width, height, layout.background[0].x);
    surface->DrawFlushText();
    surface->PopMakeCurrent(root);
}
class BossOverlay final : public IVGUI2Extension_BaseUICallbacks
{
public:
    int GetAltitude() const override { return 23; }
    void Initialize(CreateInterfaceFn*, int) override {}
    void Start(cl_enginefuncs_s*, int) override {}
    void Shutdown() override { ResetBossHealth(); surface = nullptr; textures = {}; abilityTextures = {}; abilityFont = {}; prepared = false; }
    void Key_Event(int&, int&, const char*&, VGUI2Extension_CallbackContext*) override {}
    void CallEngineSurfaceAppProc(void*&, void*&, VGUI2Extension_CallbackContext*) override {}
    void CallEngineSurfaceWndProc(void*&, unsigned int&, unsigned int&, long&, VGUI2Extension_CallbackContext*) override {}
    void Paint(int&, int&, int&, int&, VGUI2Extension_CallbackContext* context) override
    { if (context->IsPost) csol::client::Paint(); }
    void HideGameUI(VGUI2Extension_CallbackContext* context) override { if (context->IsPost) gameUI = false; }
    void ActivateGameUI(VGUI2Extension_CallbackContext* context) override { if (!context->IsPost) gameUI = true; }
    void HideConsole(VGUI2Extension_CallbackContext* context) override { if (context->IsPost) console = false; }
    void ShowConsole(VGUI2Extension_CallbackContext* context) override { if (!context->IsPost) console = true; }
} overlay;
int Receive(const char* name, int size, void* data)
{
    SynchronizeSession();
    if (data && size >= 0)
    {
        const auto* bytes = static_cast<const unsigned char*>(data);
        boss_health::AbilityMessage abilities;
        const auto abilityParsed = boss_health::DecodeAbilities({static_cast<const std::byte*>(data), std::size_t(size)}, abilities);
        if (abilityParsed == boss_health::ParseResult::Malformed) { ++malformed; return 1; }
        if (abilityParsed == boss_health::ParseResult::Accepted) { state.ApplyAbilities(abilities); ++accepted; return 1; }
        if (size >= 2 && bytes[0] == 27 && bytes[1] == 26)
        {
            if (size != 4) { ++malformed; return 1; }
            const unsigned raw = unsigned(bytes[2]) | (unsigned(bytes[3]) << 8);
            const int appearance = raw < 0x8000 ? int(raw) : int(raw) - 0x10000;
            // Original02A5BAEC ->029CAB40 removes by appearance. Negative
            // types clear the whole table; no entity index is transmitted.
            if (appearance < 0) state.Reset();
            else state.Apply({appearance, 0, 0, 0, false});
            ++accepted;
            return 1;
        }
        if (size == 1 && bytes[0] == 3)
        {
            const int yaw = std::rand() % 10 + 10, pitch = std::rand() % 10 + 10;
            impact = {float(pitch & 1 ? -pitch : pitch), float(yaw & 1 ? -yaw : yaw)};
            ++impacts;
            return 1;
        }
        if (size == 2 && bytes[0] == 15 && bytes[1] == 3)
        {
            if (engine.pfnPlaySoundByNameAtPitch)
            { engine.pfnPlaySoundByNameAtPitch("zombi/TD_Stun.wav", 1, 100); ++stuns; }
            return 1;
        }
        boss_health::Message message;
        const auto parsed = boss_health::Decode({static_cast<const std::byte*>(data), std::size_t(size)}, message);
        if (parsed == boss_health::ParseResult::Malformed) { ++malformed; return 1; }
        if (parsed == boss_health::ParseResult::Accepted) { state.Apply(message); ++accepted; }
    }
    return previousMessage ? previousMessage(name, size, data) : 1;
}
int Init(const char* name, int size, void* data)
{ ResetBossHealth(); return previousInit ? previousInit(name, size, data) : 1; }
int ResetLife(const char* name, int size, void* data)
{
    impact = {};
    return previousReset ? previousReset(name, size, data) : 1;
}
int Hide(const char* name, int size, void* data)
{
    if (data && size == 1) hidden = *static_cast<unsigned char*>(data);
    return previousHide ? previousHide(name, size, data) : 1;
}
void Install(const char* name, pfnUserMsgHook hook, pfnUserMsgHook& previous)
{
    InstallChainedUserMessage(name, hook, previous);
}
}
void InitializeBossHealth()
{
    Install("MPToCL", Receive, previousMessage);
    Install("InitHUD", Init, previousInit);
    Install("ResetHUD", ResetLife, previousReset);
    Install("HideWeapon", Hide, previousHide);
    if (!extension)
    {
        const auto factory = api->GetEngineFactory();
        surface = factory ? static_cast<vgui::ISurface*>(factory(VGUI_SURFACE_INTERFACE_VERSION, nullptr)) : nullptr;
        const auto module = api->GetPluginModuleHandleByBaseFileName("VGUI2Extension");
        const auto pluginFactory = module ? Sys_GetFactory(module) : nullptr;
        extension = pluginFactory ? static_cast<IVGUI2Extension*>(pluginFactory(VGUI2_EXTENSION_INTERFACE_VERSION, nullptr)) : nullptr;
        if (extension && surface) extension->RegisterBaseUICallbacks(&overlay);
    }
}
void ResetBossHealth() { state.Reset(); hidden = 0; requested = false; impact = {}; }
void BossHealthFrame() { if (state.Current()) Prepare(); }
void BossImpactCreateMove(int active)
{
    if (!active || (impact[0] == 0 && impact[1] == 0) || !engine.GetViewAngles || !engine.SetViewAngles) return;
    float angles[3]{};
    engine.GetViewAngles(angles);
    const auto pending = impact;
    impact = {};
    if (!std::isfinite(angles[0]) || !std::isfinite(angles[1])) return;
    angles[0] += pending[0];
    angles[1] += pending[1];
    // The following stock CreateMove clamps pitch and normalizes angles in
    // its normal input stage. This is an aim change, not a one-frame overlay.
    engine.SetViewAngles(angles);
}
void BossHealthVideoInit() { Release(); }
void DrawBossHealth(int intermission) { requested = !intermission && state.Current(); }
void ShutdownBossHealth()
{
    ResetBossHealth(); Release();
    if (extension && surface) extension->UnregisterBaseUICallbacks(&overlay);
    extension = nullptr; surface = nullptr; gameUI = console = false;
    abilityFont = {};
    if (abilityFontMemory) { RemoveFontMemResourceEx(abilityFontMemory); abilityFontMemory = nullptr; }
}
void BossHealthDiagnostics(std::ostream& out)
{
    const auto* current = state.Current();
    out << "bossHealth appearance=" << state.Appearance() << " entity=" << (current ? current->entity : 0)
        << " ratio=" << (current ? current->ratio : 0) << " prepared=" << prepared
        << " accepted=" << accepted << " rejected=" << malformed << " impacts=" << impacts
        << " stuns=" << stuns << '\n';
}
}
