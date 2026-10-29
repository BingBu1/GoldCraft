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
#include <ostream>
#include <algorithm>

namespace csol::client
{
namespace
{
std::string level;
double frameTime = -1;
unsigned clockCorrections{}, clockResets{};
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
    // Small corrections must not discard sign-on weapon definitions/inventory.
    if (frameTime >= 0 && now < frameTime)
    {
        const double correction=frameTime-now;
        ++clockCorrections;largestCorrection=std::max(largestCorrection,correction);
        if(correction>1.0){++clockResets;ResetSession();}
    }
    frameTime = now;
}
void SessionDiagnostics(std::ostream& out)
{
    out<<",\"session\":{\"clockCorrections\":"<<clockCorrections
        <<",\"clockResets\":"<<clockResets<<",\"largestCorrection\":"<<largestCorrection<<'}';
}
}
