// SPDX-License-Identifier: GPL-3.0-or-later
// Controlled engine globals/network edges; the actual production engine
// lightstyle service and the complete current PF writer are linked unchanged.
#include "precompiled.h"
#include "goldcraft/lightstyle_api.hpp"
#include "lightstyles.h"

ServerBoundary g_psv;
StaticBoundary g_psvs;
RehldsBoundary g_rehlds_sv;
InterfaceReg* InterfaceReg::s_pInterfaceRegs{};
InterfaceReg::InterfaceReg(InstantiateInterfaceFn factory, const char* name)
    : m_CreateFn(factory), m_pName(name), m_pNext(s_pInterfaceRegs) { s_pInterfaceRegs = this; }
extern "C" IBaseInterface* CreateInterface(const char* name, int* result)
{
    for (auto* row = InterfaceReg::s_pInterfaceRegs; row; row = row->m_pNext)
        if (!std::strcmp(row->m_pName, name))
        { if (result) *result = IFACE_OK; return row->m_CreateFn(); }
    if (result) *result = IFACE_FAILED;
    return nullptr;
}
#include "lightstyle_actual.inc"

extern "C" __declspec(dllexport) void EnvironmentTestMap(const char* baseline)
{
    ++g_psvs.spawncount; g_psv.state = ss_active;
    PF_lightstyle_I(0, baseline);
}
extern "C" __declspec(dllexport) void EnvironmentTestExternal(const char* pattern)
{ PF_lightstyle_I(0, pattern); }
extern "C" __declspec(dllexport) bool EnvironmentTestState(goldcraft::MapLightStyleStatus* output)
{
    auto* service = static_cast<goldcraft::IMapLightStyleService*>(CreateInterface(goldcraft::MapLightStyleServiceVersion, nullptr));
    return output && service && service->Status(*output);
}
