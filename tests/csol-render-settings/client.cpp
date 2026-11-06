// SPDX-License-Identifier: GPL-3.0-or-later
// Complete production client TU for the server-forced r_gamma_blend setting.
#include "../../native/csol/client/render_settings.cpp"
#include <cstring>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

namespace csol::client
{
cl_enginefunc_t engine{};
metahook_api_t* api{};
unsigned sessions{};
void SynchronizeSession() { ++sessions; }
}
namespace
{
using namespace csol::client;
unsigned checks{}, failures{}, forwarded{};
std::map<std::string, pfnUserMsgHook> hooks;
char saved[8] = "0";
cvar_t blend{const_cast<char*>("r_gamma_blend"), saved, FCVAR_ARCHIVE, 0.0f, nullptr};
bool haveCvar = true;
void Check(bool value, const char* what, int line)
{ ++checks; if (!value) { ++failures; std::cerr << "FAIL " << line << ": " << what << '\n'; } }
#define CHECK(...) Check((__VA_ARGS__), #__VA_ARGS__, __LINE__)
int Send(std::uint8_t value) { return hooks.at(csol::render::Message)(csol::render::Message, 1, &value); }
}

int main()
{
    static metahook_api_t host;
    host.HookUserMsg = [](const char* name, pfnUserMsgHook fn) -> pfnUserMsgHook
    { hooks[name] = fn; return [](const char*, int, void*) { ++forwarded; return 9; }; };
    api = &host;
    engine.pfnGetCvarPointer = [](const char* name) -> cvar_t*
    { return haveCvar && !std::strcmp(name, "r_gamma_blend") ? &blend : nullptr; };
    InitializeRenderSettings();
    CHECK(hooks.contains(csol::render::Message));

    // Protocol: only -1/0/1 are encodable; only 0/1/255 decode.
    CHECK(csol::render::EncodeGammaBlend(-1) == csol::render::NoForce);
    CHECK(csol::render::EncodeGammaBlend(0) == 0 && csol::render::EncodeGammaBlend(1) == 1);
    for (float bad : {2.f, .5f, -2.f}) CHECK(!csol::render::EncodeGammaBlend(bad));
    for (int b = 2; b < 255; ++b) CHECK(!csol::render::DecodeGammaBlend(std::uint8_t(b)));

    // No message: player's own value is untouched.
    RenderSettingsFrame(); CHECK(blend.value == 0.0f);
    // Forced 1: value only; the archived string keeps the player's choice.
    CHECK(Send(1) == 1 && blend.value == 1.0f && !std::strcmp(blend.string, "0"));
    // The player changes it in the console: re-forced next frame, string kept.
    std::strcpy(saved, "0"); blend.value = 0.0f; RenderSettingsFrame();
    CHECK(blend.value == 1.0f && !std::strcmp(blend.string, "0"));
    // Session end restores the saved string value.
    ResetRenderSettings(); CHECK(blend.value == 0.0f);
    RenderSettingsFrame(); CHECK(blend.value == 0.0f);
    // Forced 0 for a player who prefers 1, then the server stops forcing.
    std::strcpy(saved, "1"); blend.value = 1.0f;
    CHECK(Send(0) == 1 && blend.value == 0.0f);
    CHECK(Send(csol::render::NoForce) == 1 && blend.value == 1.0f);
    // Invalid payloads never change the cvar; wrong size is forwarded.
    CHECK(Send(7) == 1 && blend.value == 1.0f);
    std::uint8_t two[2]{1, 1};
    CHECK(hooks.at(csol::render::Message)(csol::render::Message, 2, two) == 9 && forwarded == 1 && blend.value == 1.0f);
    // Renderer absent: forced message is accepted without a cvar write.
    haveCvar = false; gammaBlend = nullptr; blend.value = 0.0f;
    CHECK(Send(1) == 1 && blend.value == 0.0f); ResetRenderSettings(); CHECK(blend.value == 0.0f);
    haveCvar = true;
    CHECK(Send(1) == 1 && blend.value == 1.0f);
    std::ostringstream diagnostics; RenderSettingsDiagnostics(diagnostics);
    CHECK(diagnostics.str().find("\"forcedGammaBlend\":1") != std::string::npos);
    CHECK(sessions >= 6);
    std::cout << "render settings client: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
