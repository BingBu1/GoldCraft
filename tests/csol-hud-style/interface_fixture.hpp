// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "native/csol/client/runtime.hpp"
#include <IVGUI2Extension.h>
#include <cstring>

namespace interface_fixture {
inline bool vguiPresent=true;
inline unsigned engineSchemeRequests{},vguiSchemeRequests{},moduleRequests{};
inline HMODULE VguiModule(){return reinterpret_cast<HMODULE>(std::uintptr_t{0x1010});}
inline HINTERFACEMODULE ExtensionModule(){return reinterpret_cast<HINTERFACEMODULE>(std::uintptr_t{0x2020});}
inline HMODULE WINAPI Module(const char* name)
{
    ++moduleRequests;
    return vguiPresent && std::strcmp(name,"vgui2.dll")==0?VguiModule():nullptr;
}
struct Extension final:IVGUI2Extension
{
    unsigned registrations{},unregistrations{};
    IVGUI2Extension_BaseUICallbacks* callback{};
    void RegisterBaseUICallbacks(IVGUI2Extension_BaseUICallbacks* p)override{callback=p;++registrations;}
    void UnregisterBaseUICallbacks(IVGUI2Extension_BaseUICallbacks*)override{callback=nullptr;++unregistrations;}
    void RegisterGameUICallbacks(IVGUI2Extension_GameUICallbacks*)override{}
    void RegisterGameUIOptionDialogCallbacks(IVGUI2Extension_GameUIOptionDialogCallbacks*)override{}
    void RegisterGameUITaskBarCallbacks(IVGUI2Extension_GameUITaskBarCallbacks*)override{}
    void RegisterGameUIBasePanelCallbacks(IVGUI2Extension_GameUIBasePanelCallbacks*)override{}
    void RegisterGameConsoleCallbacks(IVGUI2Extension_GameConsoleCallbacks*)override{}
    void RegisterClientVGUICallbacks(IVGUI2Extension_ClientVGUICallbacks*)override{}
    void RegisterKeyValuesCallbacks(IVGUI2Extension_KeyValuesCallbacks*)override{}
    void UnregisterGameUICallbacks(IVGUI2Extension_GameUICallbacks*)override{}
    void UnregisterGameUIOptionDialogCallbacks(IVGUI2Extension_GameUIOptionDialogCallbacks*)override{}
    void UnregisterGameUITaskBarCallbacks(IVGUI2Extension_GameUITaskBarCallbacks*)override{}
    void UnregisterGameUIBasePanelCallbacks(IVGUI2Extension_GameUIBasePanelCallbacks*)override{}
    void UnregisterGameConsoleCallbacks(IVGUI2Extension_GameConsoleCallbacks*)override{}
    void UnregisterClientVGUICallbacks(IVGUI2Extension_ClientVGUICallbacks*)override{}
    void UnregisterKeyValuesCallbacks(IVGUI2Extension_KeyValuesCallbacks*)override{}
    const char* GetBaseDirectory()const override{return "";}
    const char* GetCurrentLanguage()const override{return "schinese";}
};
inline Extension extension;
inline IBaseInterface* ExtensionFactory(const char* name,int*)
{return std::strcmp(name,VGUI2_EXTENSION_INTERFACE_VERSION)==0?&extension:nullptr;}
}
