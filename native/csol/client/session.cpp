// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "weapons.hpp"
#include "camera.hpp"
#include "projectiles.hpp"
#include "characters.hpp"
#include "status_icons.hpp"
#include "wall_status.hpp"
#include "buy_menu.hpp"
#include "damage_display.hpp"
#include "scenario_kill.hpp"
#include "scenario_effects.hpp"
#include "scenario_smoke.hpp"
#include "scenario_upgrades.hpp"
#include "scenario_abilities.hpp"
#include <ostream>
#include <algorithm>

namespace csol::client
{
namespace
{
std::string level;
double frameTime = -1;
unsigned clockCorrections{}, largeClockCorrections{};
double largestCorrection{};

void ResetSession()
{
    ClearScene();
    ResetWeapons(true);
    ResetCamera();
    ResetProjectiles();
    ResetCharacters();
    ResetStatusIcons();
    ResetWallStatus();
    ResetBuyMenu();
    ResetDamageDisplay();
    ResetScenarioKill();
    ResetScenarioEffects();
    ResetScenarioSmoke();
    ResetScenarioUpgrades();
    ResetScenarioAbilities();
}
}

void SynchronizeSession()
{
    const char* current = engine.pfnGetLevelName();
    if (!current) current = "";
    if (level == current) return;
    ResetSession();
    level = current;
    frameTime = -1;
}

void SynchronizeFrameSession()
{
    SynchronizeSession();
    // User messages observe packet time, HUD_Frame observes interpolated time.
    // Compare frame samples only, or an ordinary packet clears all definitions.
    const double now = engine.GetClientTime();
    // cl.time is a network-adjusted render clock, not a connection identity.
    // Its correction size does not identify a new connection either. The
    // server does not replay sign-on definitions after a render-clock jump.
    // Only SynchronizeSession's level lifecycle may retire that state.
    if (frameTime >= 0 && now < frameTime)
    {
        const double correction=frameTime-now;
        ++clockCorrections;largestCorrection=std::max(largestCorrection,correction);
        if(correction>1.0)++largeClockCorrections;
    }
    frameTime = now;
}
void SessionDiagnostics(std::ostream& out)
{
    out<<",\"session\":{\"clockCorrections\":"<<clockCorrections
        <<",\"largeClockCorrections\":"<<largeClockCorrections<<",\"largestCorrection\":"<<largestCorrection<<'}';
}
}
