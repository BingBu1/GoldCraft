// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/client/scenario_key_defaults.hpp"
#include <metahook.h>
#include <VGUI/IGameUIFuncs.h>
#include <demo_api.h>
#include <keydefs.h>
#include <array>
#include <cassert>
#include <cstring>
#include <string>
#include <vector>

namespace {
const char* profile{};
bool playback{}, aim_down{}, aim_up{};
std::vector<std::string> commands;
struct Keys final : IGameUIFuncs {
    std::array<const char*, 256> bindings{};
    const char* Key_BindingForKey(int key) override { return bindings.at(key); }
    bool IsKeyDown(const char*, bool&) override { return false; }
    const char* Key_NameForKey(int) override { return nullptr; }
    vgui::KeyCode GetVGUI2KeyCodeForBind(const char*) override { return {}; }
    void GetVideoModes(vmode_s**, int*) override {}
    void GetCurrentVideoMode(int*, int*, int*) override {}
    void GetCurrentRenderer(char*, int, int*) override {}
    bool IsConnectedToVACSecureServer() override { return false; }
    int Key_KeyStringToKeyNum(const char*) override { return -1; }
} keys;
IBaseInterface* key_interface(const char* version, int*) {
    assert(std::strcmp(version, VENGINE_GAMEUIFUNCS_VERSION) == 0);
    return &keys;
}
}

int main() {
    metahook_api_t api{};
    api.GetEngineFactory = []() -> CreateInterfaceFn { return key_interface; };
    api.FindCmd = [](const char* name) -> cmd_function_t* {
        static cmd_function_t command{};
        if (std::strcmp(name, "+cof_aim") == 0) return aim_down ? &command : nullptr;
        assert(std::strcmp(name, "-cof_aim") == 0);
        return aim_up ? &command : nullptr;
    };
    cl_enginefunc_t engine{};
    demo_api_t demo{};
    demo.IsPlayingback = [] { return int(playback); };
    engine.pDemoAPI = &demo;
    engine.ServerInfo_ValueForKey = [](const char* key) {
        assert(std::strcmp(key, "gc_sc_keys") == 0);
        return profile;
    };
    engine.pfnClientCmd = [](const char* text) { commands.emplace_back(text); return 1; };
    const auto join = [&](unsigned generation) { goldcraft::scenario_keys::apply_on_join(&api, engine, generation); };
    join(1);
    for (const char* other : {"", "0", "2", "1;quit"}) { profile = other; join(1); }
    profile = "1";
    playback = true; join(1); playback = false;
    join(0);
    assert(commands.empty());

    aim_down = aim_up = true;
    keys.bindings[K_MOUSE3] = ""; // GoldSrc returns either null or an empty command for an empty key.
    join(1);
    assert((commands == std::vector<std::string>{"bind j cheer\n", "bind MOUSE2 +attack2\n", "bind MOUSE3 +cof_aim\n"}));
    commands.clear();
    join(1);  // A later ResetHUD must allow players to leave a key unbound during this connection.
    assert(commands.empty());

    keys.bindings['j'] = "radio3";
    keys.bindings[K_MOUSE2] = "+attack2; echo custom";
    keys.bindings[K_MOUSE3] = "+voicerecord";
    join(2);
    assert(commands.empty());
    keys.bindings['j'] = nullptr;
    keys.bindings[K_MOUSE2] = "+attack2"; // Stock CS profile.
    keys.bindings[K_MOUSE3] = nullptr;
    join(3);
    assert((commands == std::vector<std::string>{"bind j cheer\n", "bind MOUSE3 +cof_aim\n"}));
    commands.clear();
    keys.bindings['j'] = "cheer";
    aim_up = false; join(4);
    aim_up = true; aim_down = false; join(5);
    keys.bindings[K_MOUSE3] = "+cof_aim";
    aim_down = true; join(6);
    assert(commands.empty());
}
