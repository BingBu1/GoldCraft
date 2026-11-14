// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../runtime.hpp"
#include "../../command_chain.hpp"
#include "../../user_message_chain.hpp"
#include "../../resource_refresh.hpp"
#include "../../overlay_layers.hpp"
#include "../../buy_menu.hpp"
#include "../../characters.hpp"
#include "../../hud_style.hpp"
#include "scenario_map_vote.hpp"
#include "human_kick.hpp"
#include "../../../scenario/core/scenario_upgrades.hpp"
#include "../../../scenario/custom/action_hud_layout.hpp"
#include <ostream>
#include <VGUI/ISurface.h>
#include <IVGUI2Extension.h>

namespace csol::client
{
namespace
{
human_kick_detail::InputState state;
xcommand_t previousImpulse{};
pfnUserMsgHook previousSnapshot{}, previousInit{}, previousReset{}, previousHide{};
vgui::ISurface* surface{};
IVGUI2Extension* extension{};
vgui::HFont font{};
HANDLE privateFont{};
cvar_t* hudDraw{};
DWORD ownerThread{};
bool prepared{}, requested{}, gameUI{}, console{};
int hidden{}, hintWidth{}, hintHeight{}, keyWidth{}, keyHeight{};
std::uint64_t accepted{}, malformed{}, submitted{};
constexpr wchar_t Hint[] = L"可以踢击";

bool Owner() { return ownerThread && ownerThread == GetCurrentThreadId(); }
bool Gameplay()
{
    return !gameUI && !console && !BuyMenuVisible() && !CharacterMenuVisible() &&
        !ScenarioMapVoteVisible() && view.health > 0 && !view.spectator;
}
void Impulse()
{
    const int argc = engine.Cmd_Argc ? engine.Cmd_Argc() : 0;
    const char* argument = argc == 2 && engine.Cmd_Argv ? engine.Cmd_Argv(1) : nullptr;
    if (Owner() && engine.pfnServerCmd && state.OwnImpulse(argc, argument, true) && Gameplay())
    {
        // User-selected F normally invokes impulse100. Keep every native bind;
        // this dedicated server command carries no client damage/target claims.
        engine.pfnServerCmd("csol_kick\n");
        ++submitted;
        return;
    }
    if (previousImpulse) previousImpulse();
}
void Prepare()
{
    if (prepared || !surface || !ResourceReadsAllowed()) return;
    prepared = true;
    if (!privateFont)
    {
        try
        {
            const auto bytes = ReadResource(upgrades::FontPath);
            DWORD count{};
            privateFont = RegisterResourceFont(privateFont, upgrades::FontPath, "DFYuanW9-GB", bytes, &count);
        }
        catch (const std::exception& error)
        { engine.Con_DPrintf("CSOLSupport: human kick font: %s\n", error.what()); }
    }
    if (privateFont && !font)
    {
        font = surface->CreateFont();
        if (font && !surface->AddGlyphSetToFont(font, "DFYuanW9-GB", 18, 0, 0, 0,
            vgui::FONTFLAG_ANTIALIAS, 0, 0xffff)) font = 0;
    }
    if (font)
    {
        surface->GetTextSize(font, Hint, hintWidth, hintHeight);
        surface->GetTextSize(font, L"F", keyWidth, keyHeight);
    }
}
void Paint()
{
    if (!requested) return;
    requested = false;
    if (!surface || !font || !state.Ready() || !Gameplay() || (hidden & 4) ||
        (hudDraw && hudDraw->value == 0)) return;
    int width{}, height{};
    surface->GetScreenSize(width, height);
    const auto root = surface->GetEmbeddedPanel();
    if (!root || width <= 0 || height <= 0) return;
    // User-authorized F hint, not an invented original CSO HUD layout.
    constexpr int keySize = 26, gap = 10;
    const auto layout=scenario::custom::StaminaLayout(width,height,HudStyleScale());
    if(!layout.width||keySize+gap+hintWidth>width-2*layout.padding)return;
    const int x = (width-keySize-gap-hintWidth)/2;
    const int y = layout.kickY;
    surface->PushMakeCurrent(root, false);
    surface->DrawSetColor(12, 16, 20, 196);
    surface->DrawFilledRect(x, y, x + keySize, y + keySize);
    surface->DrawSetColor(210, 187, 129, 255);
    surface->DrawOutlinedRect(x, y, x + keySize, y + keySize);
    surface->DrawSetTextFont(font);
    surface->DrawSetTextColor(235, 222, 188, 255);
    surface->DrawSetTextPos(x + (keySize - keyWidth) / 2, y + (keySize - keyHeight) / 2);
    surface->DrawPrintText(L"F", 1);
    surface->DrawSetTextPos(x + keySize + gap, y + (keySize - hintHeight) / 2);
    surface->DrawPrintText(Hint, int(std::size(Hint) - 1));
    surface->DrawFlushText();
    surface->PopMakeCurrent(root);
}
class Overlay final : public IVGUI2Extension_BaseUICallbacks
{
public:
    int GetAltitude() const override { return 22; }
    void Initialize(CreateInterfaceFn*, int) override {}
    void Start(cl_enginefuncs_s*, int) override {}
    void Shutdown() override { ResetHumanKick(); surface = nullptr; font = 0; prepared = false; extension = nullptr; }
    void Key_Event(int&, int&, const char*&, VGUI2Extension_CallbackContext*) override {}
    void CallEngineSurfaceAppProc(void*&, void*&, VGUI2Extension_CallbackContext*) override {}
    void CallEngineSurfaceWndProc(void*&, unsigned int&, unsigned int&, long&, VGUI2Extension_CallbackContext*) override {}
    void Paint(int&, int&, int&, int&, VGUI2Extension_CallbackContext* context) override
    { if (context->IsPost) QueueOverlayPaint(OverlayLayer::HumanKick, csol::client::Paint); }
    void HideGameUI(VGUI2Extension_CallbackContext* context) override { if (context->IsPost) gameUI = false; }
    void ActivateGameUI(VGUI2Extension_CallbackContext* context) override { if (!context->IsPost) gameUI = true; }
    void HideConsole(VGUI2Extension_CallbackContext* context) override { if (context->IsPost) console = false; }
    void ShowConsole(VGUI2Extension_CallbackContext* context) override { if (!context->IsPost) console = true; }
} overlay;
int Receive(const char* name, int size, void* data)
{
    if (!Owner()) return previousSnapshot ? previousSnapshot(name, size, data) : 1;
    SynchronizeSession();
    const auto previous = state.Value();
    if (!data || size < 0 || !state.Receive({static_cast<const std::byte*>(data), std::size_t(size)}))
    { ++malformed; return 1; }
    ++accepted;
    HumanKickAnimationSnapshot(previous, state.Value());
    return previousSnapshot ? previousSnapshot(name, size, data) : 1;
}
int Init(const char* name, int size, void* data)
{ ResetHumanKick(); return previousInit ? previousInit(name, size, data) : 1; }
int Reset(const char* name, int size, void* data)
{ ResetHumanKick(); return previousReset ? previousReset(name, size, data) : 1; }
int Hide(const char* name, int size, void* data)
{
    if (Owner() && data && size == 1) hidden = *static_cast<const unsigned char*>(data);
    return previousHide ? previousHide(name, size, data) : 1;
}
}
void InitializeHumanKick()
{
    if (ownerThread && !Owner()) return;
    ownerThread = GetCurrentThreadId();
    InitializeHumanKickAnimation();
    InstallChainedCommand("impulse", Impulse, previousImpulse);
    InstallChainedUserMessage(scenario::human_kick::MessageName, Receive, previousSnapshot);
    InstallChainedUserMessage("InitHUD", Init, previousInit);
    InstallChainedUserMessage("ResetHUD", Reset, previousReset);
    InstallChainedUserMessage("HideWeapon", Hide, previousHide);
    hudDraw = engine.pfnGetCvarPointer ? engine.pfnGetCvarPointer("hud_draw") : nullptr;
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
void ResetHumanKick()
{
    if (!Owner()) return;
    state.Reset(); requested = false; hidden = 0;
    ResetHumanKickAnimation();
}
void HumanKickFrame()
{
    if (Owner() && state.Enabled()) Prepare();
}
void HumanKickVideoInit() { prepared = false; }
void InvalidateHumanKickResources() { HumanKickVideoInit(); requested = false; ResetHumanKickAnimation(); }
void DrawHumanKick(float, int intermission)
{ requested = Owner() && !intermission && state.Ready(); }
void ShutdownHumanKick()
{
    if (!Owner()) return;
    ResetHumanKick();
    if (extension && surface) extension->UnregisterBaseUICallbacks(&overlay);
    if (privateFont) RemoveFontMemResourceEx(privateFont);
    privateFont = nullptr; font = 0; surface = nullptr; extension = nullptr;
    prepared = false; gameUI = console = false; hudDraw = nullptr;
}
void HumanKickDiagnostics(std::ostream& output)
{
    const auto& value = state.Value();
    output << "humanKick life=" << value.life << " flags=" << unsigned(value.flags)
        << " remaining=" << value.remaining << " prepared=" << prepared << " font=" << bool(font)
        << " accepted=" << accepted << " malformed=" << malformed << " submitted=" << submitted << '\n';
}
bool HumanKickActive() noexcept
{
    return Owner() && state.Enabled() && (state.Value().flags & scenario::human_kick::Active) != 0;
}
}
