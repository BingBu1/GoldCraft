// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Local x86 CreateInterface contract. Include the host interface.h first.
// Mode changes are requests for the next map; never swap rules mid-round.
namespace csol
{
enum class GameMode : int { Native = 0, ZombiePlague = 1, Scenario = 2 };
constexpr const char* GameModeServiceVersion = "CSOLGameMode001";
class IGameModeService : public IBaseInterface
{
public:
    // Called once by the AMXX module's PluginsLoaded stage, before the first
    // plugin_precache forward. It starts a new map generation, including a
    // same-name reload. Invalid CVAR input returns -1 and remains unconfirmed.
    virtual int BeginMap() = 0;
    virtual int LockedMode() const = 0;
    // The first primary plugin acknowledges only after applying mode policy
    // to every loaded plugin. This must precede worldspawn/GameRules creation.
    virtual bool ConfirmPluginIsolation(int mode) = 0;
    virtual bool PluginIsolationReady() const = 0;
    virtual void EndMap() = 0;
};
}
