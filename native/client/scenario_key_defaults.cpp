// SPDX-License-Identifier: GPL-3.0-or-later
#include "scenario_key_defaults.hpp"
#include <metahook.h>
#include <VGUI/IGameUIFuncs.h>
#include <demo_api.h>
#include <keydefs.h>
#include <cstring>

namespace goldcraft::scenario_keys {
void apply_on_join(metahook_api_t* api, const cl_enginefunc_t& engine, std::uint64_t connection) {
    static std::uint64_t handled_connection{};
    if (!connection || handled_connection == connection || !engine.ServerInfo_ValueForKey ||
        !engine.pfnClientCmd || (engine.pDemoAPI && engine.pDemoAPI->IsPlayingback())) return;
    const char* version = engine.ServerInfo_ValueForKey("gc_sc_keys");
    if (!version || std::strcmp(version, "1") != 0) return;

    const auto factory = api && api->GetEngineFactory ? api->GetEngineFactory() : nullptr;
    auto* keys = factory ? static_cast<IGameUIFuncs*>(factory(VENGINE_GAMEUIFUNCS_VERSION, nullptr)) : nullptr;
    if (!keys) return;
    handled_connection = connection;

    // The server selects a fixed profile. Each command and physical key is local.
    const auto bind_if_empty = [&](int key, const char* command) {
        const char* binding = keys->Key_BindingForKey(key);
        if (!binding || !*binding) engine.pfnClientCmd(command);
    };
    bind_if_empty('j', "bind j cheer\n");
    bind_if_empty(K_MOUSE2, "bind MOUSE2 +attack2\n");
    if (api->FindCmd && api->FindCmd("+cof_aim") && api->FindCmd("-cof_aim"))
        bind_if_empty(K_MOUSE3, "bind MOUSE3 +cof_aim\n");
}
}
