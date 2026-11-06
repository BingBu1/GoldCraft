// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "user_message_chain.hpp"
#include "render_settings.hpp"
#include "../render_settings.hpp"
#include <cmath>
#include <cstdlib>

namespace csol::client
{
namespace
{
pfnUserMsgHook previous{};
cvar_t* gammaBlend{};
int forced = -1;
bool applied{};
std::uint64_t received{}, invalid{}, forcedFrames{};

cvar_t* GammaBlend()
{
    if (!gammaBlend && engine.pfnGetCvarPointer) gammaBlend = engine.pfnGetCvarPointer("r_gamma_blend");
    return gammaBlend;
}
// Only cvar->value is forced. cvar->string keeps the player's own choice, so
// Host_WriteConfiguration (which runs before client shutdown) never saves the
// server's value into config.cfg, even if the game quits while connected.
void Apply()
{
    if (forced < 0) return;
    auto* cvar = GammaBlend();
    if (!cvar) return;
    const float value = static_cast<float>(forced);
    if (cvar->value != value) cvar->value = value;
    applied = true;
}
int Receive(const char* name, int size, void* data)
{
    if (!data || size != csol::render::MessageBytes) { ++invalid; return previous ? previous(name, size, data) : 1; }
    SynchronizeSession();
    const auto value = csol::render::DecodeGammaBlend(*static_cast<const std::uint8_t*>(data));
    if (!value) { ++invalid; return 1; }
    ++received;
    if (*value < 0) ResetRenderSettings();
    else { forced = *value; Apply(); }
    return 1;
}
}

void InitializeRenderSettings() { InstallChainedUserMessage(csol::render::Message, Receive, previous); }
void RenderSettingsFrame()
{
    if (forced < 0) return;
    Apply();
    ++forcedFrames;
}
void ResetRenderSettings()
{
    if (applied && gammaBlend && gammaBlend->string)
    {
        const float saved = std::strtof(gammaBlend->string, nullptr);
        gammaBlend->value = std::isfinite(saved) ? saved : 0.0f;
    }
    forced = -1;
    applied = false;
}
void RenderSettingsDiagnostics(std::ostream& out)
{
    out << ",\"renderSettings\":{\"forcedGammaBlend\":" << forced << ",\"received\":" << received
        << ",\"invalid\":" << invalid << ",\"forcedFrames\":" << forcedFrames
        << ",\"gammaBlendValue\":" << (gammaBlend ? gammaBlend->value : -1.0f) << '}';
}
}
