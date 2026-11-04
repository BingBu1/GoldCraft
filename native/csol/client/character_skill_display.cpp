// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "overlay_layers.hpp"
#include "character_skill_display.hpp"
#include "user_message_chain.hpp"
#include "../character_skill_display.hpp"
#include "../image_codec.hpp"
#include <VGUI/ISurface.h>
#include <IVGUI2Extension.h>
#include <algorithm>

namespace csol::client
{
namespace
{
vgui::ISurface* surface{};
IVGUI2Extension* extension{};
struct Image { int texture{}; bool attempted{}; double retryAt{}; };
std::array<Image, 50> images;
Image overlayImage;
struct Active { skillfx::ScreenMessage message; float start{}, end{}; bool set{}; } status, colorOverlay;
// MPToCL/50 appends an independent timed layer. A reflection impact must
// remain visible together with the longer protection overlay.
std::array<Active, 128> transientOverlays;
pfnUserMsgHook previousMP{}, previousReset{}, previousInit{};
bool requested{}, gameUI{}, console{};
bool lifeObserved{};
float frameTime{};

void ClearActive()
{
    status = {}; colorOverlay = {}; transientOverlays = {};
    frameTime = 0; requested = false;
}

void Release()
{
    if (surface)
    {
        for (auto& image : images) if (image.texture) surface->DeleteTextureByID(image.texture);
        if (overlayImage.texture) surface->DeleteTextureByID(overlayImage.texture);
    }
    images = {}; overlayImage = {};
}
void Prepare(Image& output, const char* path)
{
    if (!surface || output.texture || !path || !engine.GetClientTime) return;
    const double now = engine.GetClientTime();
    if (!std::isfinite(now) || (output.attempted && now < output.retryAt && now >= output.retryAt - 5)) return;
    const bool report = !output.attempted;
    output.attempted = true;
    output.retryAt = now + 5;
    try
    {
        const auto image = DecodeTGA(ReadResource(path));
        output.texture = surface->CreateNewTextureID(true);
        if (output.texture) surface->DrawSetTextureRGBA(output.texture, image.RGBA.data(),
            image.Width, image.Height, 1, true);
    }
    catch (const std::exception& error)
    { if (report) engine.Con_DPrintf("CSOLSupport: skill screen resource %s: %s\n", path, error.what()); }
}
void Quad(int texture, int left, int top, int right, int bottom)
{
    if (!texture) return;
    // Preserve inverted destination coordinates: CSO uses the same source
    // image with a mirrored rectangle on the right/bottom edges.
    surface->DrawSetTexture(texture);
    vgui::VGuiVertex vertices[]{{left,top,0,0},{right,top,1,0},{right,bottom,1,1},{left,bottom,0,1}};
    surface->DrawTexturedPolygon(vertices,4);
}
bool Visible(const Active& item)
{ return item.set && frameTime >= item.start && (item.message.transient ? frameTime <= item.end : frameTime < item.end); }
void FourCorners(int texture, int width, int height)
{
    const int x = width / 2, y = height / 2;
    Quad(texture,0,0,x,y); Quad(texture,width,0,x,y);
    Quad(texture,0,height,x,y); Quad(texture,width,height,x,y);
}
void PaintSkills()
{
    if (!requested) return; requested = false;
    if (!surface || gameUI || console || view.health <= 0 || view.spectator || engine.pfnGetCvarFloat("hud_draw") == 0) return;
    const auto panel = surface->GetEmbeddedPanel(); if (!panel) return;
    int width{}, height{}; surface->GetScreenSize(width,height);
    if (width <= 0 || height <= 0) return;
    surface->PushMakeCurrent(panel,false);
    if (Visible(colorOverlay))
    {
        const auto& color = colorOverlay.message.color;
        // The original stores RGBA but derives opacity from its pulse;
        // the fourth packet byte is not the output opacity here.
        surface->DrawSetColor(color[0],color[1],color[2],skillfx::OverlayAlpha(frameTime));
        FourCorners(overlayImage.texture,width,height);
    }
    for (const auto& item : transientOverlays) if (Visible(item))
    {
        const auto& color = item.message.color;
        //02A9C447..02A9C531:46 uses the time-based pulse;47 uses
        // the received alpha unchanged. Both call the four-corner painter.
        const int alpha = item.message.status == 46 ? skillfx::OverlayAlpha(frameTime) : color[3];
        surface->DrawSetColor(color[0],color[1],color[2],alpha);
        FourCorners(images[item.message.status].texture,width,height);
    }
    if (Visible(status))
    {
        const auto edge = skillfx::SkillEdges(frameTime - status.start, height);
        surface->DrawSetColor(255,255,255,edge.alpha);
        const int texture = images[status.message.status].texture;
        Quad(texture,0,edge.offset,edge.width,edge.bottom);
        Quad(texture,width,edge.offset,width-edge.width,edge.bottom);
    }
    surface->PopMakeCurrent(panel);
}
class Overlay final : public IVGUI2Extension_BaseUICallbacks
{
public:
    int GetAltitude() const override { return 22; }
    void Initialize(CreateInterfaceFn*, int) override {}
    void Start(cl_enginefuncs_s*, int) override {}
    void Shutdown() override { ResetCharacterSkillDisplay(); surface = nullptr; images = {}; overlayImage = {}; }
    void Key_Event(int&, int&, const char*&, VGUI2Extension_CallbackContext*) override {}
    void CallEngineSurfaceAppProc(void*&, void*&, VGUI2Extension_CallbackContext*) override {}
    void CallEngineSurfaceWndProc(void*&, unsigned int&, unsigned int&, long&, VGUI2Extension_CallbackContext*) override {}
    void Paint(int&, int&, int&, int&, VGUI2Extension_CallbackContext* context) override { if (context->IsPost) QueueOverlayPaint(OverlayLayer::CharacterSkills, PaintSkills); }
    void HideGameUI(VGUI2Extension_CallbackContext* context) override { if (context->IsPost) gameUI = false; }
    void ActivateGameUI(VGUI2Extension_CallbackContext* context) override { if (!context->IsPost) gameUI = true; }
    void HideConsole(VGUI2Extension_CallbackContext* context) override { if (context->IsPost) console = false; }
    void ShowConsole(VGUI2Extension_CallbackContext* context) override { if (!context->IsPost) console = true; }
} overlay;
int Receive(const char* name, int size, void* data)
{
    SynchronizeSession();
    if (!data || size <= 0) return previousMP ? previousMP(name,size,data) : 1;
    skillfx::ScreenMessage message;
    const auto result = skillfx::DecodeScreen({static_cast<const std::uint8_t*>(data), std::size_t(size)}, message);
    if (result == skillfx::ScreenParse::Unrelated) return previousMP ? previousMP(name,size,data) : 1;
    if (result == skillfx::ScreenParse::Malformed) return 1;
    if (message.clearStatus) { status = {}; return 1; }
    // ResetHUD starts a new life before CalcRefdef necessarily publishes its
    // health. Preserve that life's first server effects across the old dead
    // view, but never retain effects received after an observed death.
    if (lifeObserved && (view.health <= 0 || view.spectator)) return 1;
    if (!engine.GetClientTime) return 1;
    const float now = static_cast<float>(engine.GetClientTime());
    if (!std::isfinite(now)) return 1;
    if (message.transient)
    {
        if (message.duration <= 0) return 1;
        const auto slot = std::find_if(transientOverlays.begin(),transientOverlays.end(),
            [&](const Active& value) { return !value.set || now > value.end || now < value.start; });
        if (slot != transientOverlays.end()) *slot = {message,now,now+message.duration,true};
        return 1;
    }
    auto& item = message.overlay ? colorOverlay : status;
    // Original MPToCL/51 stores end = now + duration (02812566..0281257B).
    // A zero/negative duration cancels; it is not an infinite buff.
    if (message.duration <= 0) item = {};
    else item = {message,now,now+message.duration,true};
    return 1;
}
int Reset(const char* name, int size, void* data)
{ ResetCharacterSkillDisplay(); return previousReset ? previousReset(name,size,data) : 1; }
int Init(const char* name, int size, void* data)
{ ResetCharacterSkillDisplay(); return previousInit ? previousInit(name,size,data) : 1; }
void Install(const char* name, pfnUserMsgHook hook, pfnUserMsgHook& previous)
{
    InstallChainedUserMessage(name,hook,previous);
}
}
void InitializeCharacterSkillDisplay()
{
    Install("MPToCL",Receive,previousMP); Install("ResetHUD",Reset,previousReset); Install("InitHUD",Init,previousInit);
    if (!extension)
    {
        const auto factory = api->GetEngineFactory();
        surface = factory ? static_cast<vgui::ISurface*>(factory(VGUI_SURFACE_INTERFACE_VERSION,nullptr)) : nullptr;
        const auto module = api->GetPluginModuleHandleByBaseFileName("VGUI2Extension");
        const auto pluginFactory = module ? Sys_GetFactory(module) : nullptr;
        extension = pluginFactory ? static_cast<IVGUI2Extension*>(pluginFactory(VGUI2_EXTENSION_INTERFACE_VERSION,nullptr)) : nullptr;
        if (extension && surface) extension->RegisterBaseUICallbacks(&overlay);
    }
}
void ResetCharacterSkillDisplay()
{ ClearActive(); lifeObserved = false; }
void CharacterSkillDisplayFrame()
{
    if (status.set) Prepare(images[status.message.status],skillfx::StatusImage(status.message.status));
    if (colorOverlay.set) Prepare(overlayImage,"resource/zombi/directitem.tga");
    for (const auto& item : transientOverlays) if (item.set)
        Prepare(images[item.message.status],skillfx::TransientImage(item.message.status));
}
void CharacterSkillDisplayVideoInit() { Release(); }
void DrawCharacterSkillDisplay(float time, int intermission)
{
    if (!std::isfinite(time)) return;
    frameTime = time;
    if (view.health <= 0 || view.spectator)
    {
        if (lifeObserved) ClearActive();
        else requested = false;
        return;
    }
    lifeObserved = true;
    if (status.set && time >= status.end) status = {};
    if (colorOverlay.set && time >= colorOverlay.end) colorOverlay = {};
    for (auto& item : transientOverlays) if (item.set && time > item.end) item = {};
    requested = !intermission && (Visible(status) || Visible(colorOverlay) ||
        std::any_of(transientOverlays.begin(),transientOverlays.end(),Visible));
}
void ShutdownCharacterSkillDisplay()
{
    ResetCharacterSkillDisplay(); Release();
    if (extension && surface) extension->UnregisterBaseUICallbacks(&overlay);
    extension = nullptr; surface = nullptr; gameUI = console = false;
}
}
