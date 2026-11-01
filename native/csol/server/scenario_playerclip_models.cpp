// SPDX-License-Identifier: GPL-3.0-or-later
// This TU deliberately does not include ReGameDLL precompiled.h. Its public
// ReHLDS API must come from the exact engine tree used for the paired server.
// The public API assumes the engine's platform, scalar and resource types
// have been declared first; it is not a self-contained SDK header.
#include "../../../external/ReHLDS/rehlds/public/rehlds/osconfig.h"
#include "../../../external/ReHLDS/rehlds/public/rehlds/archtypes.h"
#include "../../../external/ReHLDS/rehlds/public/rehlds/maintypes.h"
#include "../../../external/ReHLDS/rehlds/public/rehlds/common_rehlds.h"
#include "../../../external/ReHLDS/rehlds/public/rehlds/custom.h"
#include "../../../external/ReHLDS/rehlds/public/rehlds/rehlds_api.h"
#include "scenario_playerclip_models.h"

model_s* CSOL_ScenarioBrushModel(int modelIndex)
{
    if (modelIndex <= 0 || modelIndex > 65535) return nullptr;
#ifdef _WIN32
    const auto module = GetModuleHandleA("swds.dll");
    const auto factory = module ? reinterpret_cast<CreateInterfaceFn>(GetProcAddress(module, "CreateInterface")) : nullptr;
#else
    const auto module = dlopen("engine_i486.so", RTLD_NOW | RTLD_NOLOAD);
    const auto factory = module ? reinterpret_cast<CreateInterfaceFn>(dlsym(module, "CreateInterface")) : nullptr;
#endif
    // ReHLDS exports the named interface through Valve's untyped factory
    // convention; IRehldsApi deliberately does not inherit IBaseInterface.
    auto* api = factory ? reinterpret_cast<IRehldsApi*>(factory(VREHLDS_HLDS_API_VERSION, nullptr)) : nullptr;
    // Exact major and at least the vendored minor; never cast the old ReAPI
    // IRehldsServerData vtable to this newer interface without checking.
    auto* data = api && api->GetMajorVersion() == REHLDS_API_VERSION_MAJOR &&
        api->GetMinorVersion() >= REHLDS_API_VERSION_MINOR ? api->GetServerData() : nullptr;
    auto* model = data ? data->GetModel(modelIndex) : nullptr;
    if (model && (model->type != mod_brush || model->name[0] != '*')) model = nullptr;
#ifndef _WIN32
    if (module) dlclose(module);
#endif
    return model;
}
