// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "overlay_layers.hpp"
#include <IVGUI2Extension.h>

namespace csol::client
{
namespace
{
IVGUI2Extension* extension{};

class Compositor final : public IVGUI2Extension_BaseUICallbacks
{
public:
    // Below every CSOL overlay (20..35) so it runs after their post-paints,
    // above VGUI2Extension's own altitude 0 callbacks.
    int GetAltitude() const override { return 1; }
    void Initialize(CreateInterfaceFn*, int) override {}
    void Start(cl_enginefuncs_s*, int) override {}
    void Shutdown() override { overlay_detail::queued = {}; overlay_detail::active = false; }
    void Key_Event(int&, int&, const char*&, VGUI2Extension_CallbackContext*) override {}
    void CallEngineSurfaceAppProc(void*&, void*&, VGUI2Extension_CallbackContext*) override {}
    void CallEngineSurfaceWndProc(void*&, unsigned int&, unsigned int&, long&, VGUI2Extension_CallbackContext*) override {}
    void Paint(int&, int&, int&, int&, VGUI2Extension_CallbackContext* context) override
    { if (context->IsPost) PaintQueuedOverlays(); }
    void HideGameUI(VGUI2Extension_CallbackContext*) override {}
    void ActivateGameUI(VGUI2Extension_CallbackContext*) override {}
    void HideConsole(VGUI2Extension_CallbackContext*) override {}
    void ShowConsole(VGUI2Extension_CallbackContext*) override {}
} compositor;
}

void InitializeOverlayCompositor()
{
    if (overlay_detail::active) return;
    overlay_detail::queued = {};
    if (!extension)
    {
        const auto module = api->GetPluginModuleHandleByBaseFileName("VGUI2Extension");
        const auto factory = module ? Sys_GetFactory(module) : nullptr;
        extension = factory ? static_cast<IVGUI2Extension*>(factory(VGUI2_EXTENSION_INTERFACE_VERSION, nullptr)) : nullptr;
        if (!extension) return;
        extension->RegisterBaseUICallbacks(&compositor);
    }
    overlay_detail::active = true;
}

void ShutdownOverlayCompositor()
{
    // BaseUI::Shutdown disables painting while its callback is still registered.
    // Retire that registration even when painting has already been disabled.
    if (extension) extension->UnregisterBaseUICallbacks(&compositor);
    overlay_detail::queued = {};
    overlay_detail::active = false;
    extension = nullptr;
}
}
