// SPDX-License-Identifier: GPL-3.0-or-later
#include "gun_smoke.hpp"
#include "../command_chain.hpp"

namespace csol::client {
namespace {
bool enabled{};
xcommand_t previous{};
void Toggle()
{
    enabled=!enabled;
    if(previous)previous();
}
}
void InitializeGunSmoke()
{
    // One process-wide toggle mirrors the original client byte. The shared
    // command-chain helper handles repeat HUD_Init and preserves native guns.
    if(api&&api->HookCmd)InstallChainedCommand("gunsmoke",Toggle,previous,true);
}
bool GunSmokeEnabled(){return enabled;}
}
