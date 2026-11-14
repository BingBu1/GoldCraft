// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "hd_spray.hpp"
#include "weapons/weapons.hpp"
#include "weapons/guest_cof.hpp"
#include "hud_style.hpp"
#include "camera.hpp"
#include "weapons/projectiles.hpp"
#include "characters.hpp"
#include "scenario/core/status_icons.hpp"
#include "scenario/core/wall_status.hpp"
#include "buy_menu.hpp"
#include "scenario/core/damage_display.hpp"
#include "scenario/core/scenario_kill.hpp"
#include "scenario/core/scenario_map_vote.hpp"
#include "scenario/core/scenario_effects.hpp"
#include "character_skill_effects.hpp"
#include "scenario/core/scenario_smoke.hpp"
#include "scenario/monsters/elite_visuals.hpp"
#include "scenario/core/scenario_upgrades.hpp"
#include "scenario/core/scenario_abilities.hpp"
#include "scenario/core/human_kick.hpp"
#include "scenario/core/challenge_phase.hpp"
#include "scenario/bosses/boss_health.hpp"
#include "scenario/core/scenario_round_hud_client.hpp"
#include "scenario/core/scenario_respawn_ui.hpp"
#include "scenario/core/playerclip.hpp"
#include "render_settings.hpp"
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
    ResetGuestCof();
    ClearScene();
    ResetWeapons(true);
    ResetHudStyle();
    ResetCamera();
    ResetProjectiles();
    ResetCharacters();
    ResetStatusIcons();
    ResetWallStatus();
    ResetBuyMenu();
    ResetScenarioMapVote();
    ResetDamageDisplay();
    ResetSpray();
    ResetScenarioKill();
    ResetScenarioEffects();
    ResetCharacterSkillEffects();
    ResetScenarioSmoke();
    ResetEliteVisuals();
    ResetScenarioUpgrades();
    ResetScenarioAbilities();
    ResetHumanKick();
    ResetChallengePhase();
    ResetBossHealth();
    ResetScenarioRoundHud();
    ResetScenarioRespawnUi();
    ResetPlayerClips();
    ResetRenderSettings();
}
}

void ResetResourceSession()
{
    // Resource-overlay removal is a real connection boundary. Use the same
    // owner resets as the existing level lifecycle, without unregistering hooks.
    ResetSession();level.clear();frameTime=-1;
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
