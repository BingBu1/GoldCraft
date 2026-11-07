// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "area_expanded.hpp"
#include "host_notifications.hpp"
#include "../scenario/core/scenario_player_runtime.h"
#include "../scenario/core/scenario_session.h"
#include <cmath>

namespace csol::scenario::skills
{
bool StartCrisisSkill18(
    const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session,
    const ActivationState& timers, ConfigError& error)
{
    if (!players || !state || !session || !gpGlobals) return false;
    const auto access = session->Map().EntityAccess();
    const auto skill = state->activeSkill;
    const auto life = state->skillLifeSerial;
    auto* player = state->player.Get<CBasePlayer>();
    const auto current = [&]
    {
        return access && access->Allowed() && session->Map().EntityAccess() == access &&
            players->Active() && players->Find(state->player) == state &&
            state->activeSkill == skill && state->skillLifeSerial == life &&
            state->player.Get<CBasePlayer>() == player && player && player->pev &&
            player->pev->deadflag == DEAD_NO && player->pev->health > 0 &&
            !(player->pev->flags & (FL_KILLME | FL_DORMANT));
    };
    if (!current() || !error.message.empty()) return false;
    const float duration = timers.effects.duration;
    const float expires = duration < 0 ? -1.0f : gpGlobals->time + duration;
    if (!std::isfinite(gpGlobals->time) || !std::isfinite(duration) || !std::isfinite(expires))
    { error.message = "crisis skill protection duration is invalid"; return false; }
    //106E99B0 damages Classify17 first. Water/form suppression skips that
    // sphere only: the caster still receives the following timed protection.
    if (!StartCharacterAreaDamage(18, players, state, session, error) || !current()) return false;
    CSOLScenarioStatBuff buff;
    buff.group = 3; buff.source = 10; buff.subsource = 18;
    buff.protection = true; buff.mask = 0; buff.expires = expires;
    const auto token = players->InstallStatBuff(state->player, std::move(buff));
    if (!token || !current()) return false;
    if (!players->HoldSkillProtectionRender(state->player, token, error) || !current()) return false;
    return PublishStatusDuration(*session, state->player, 40, duration,
        {255,255,255}, error) && current();
}

bool EndCloak39MonsterDamage(
    const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session,
    const Vector& origin, ConfigError& error)
{
    //106DF300 phase3: protection removal and the player-buff closure run
    // before this fixed-damage sphere. It does not use radial falloff or
    // attack-level multiplication. The circle is sent after all recipients.
    if (!ApplyFlatCharacterAreaDamage(players, state, session, origin,
        8.0f * 39.37f, 8000.0f, 0x01000000, error)) return false;
    return PublishSkillCircle(*session, {origin.x, origin.y, origin.z}, true, error);
}

bool StartExpandedAreaSkill49(
    const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session, ConfigError& error)
{
    if (!players || !state || !session || players->Find(state->player) != state) return false;
    auto* player = state->player.Get<CBasePlayer>();
    if (!player || !player->pev) return false;
    //106EEAA0 first performs the5000-damage sphere with bits0; the caster's
    // StatBuff is installed by the caller only after this operation returns.
    const Vector origin = player->pev->origin;
    return ApplyFlatCharacterAreaDamage(players, state, session, origin,
        10.0f * 39.37f, 5000.0f, 0, error);
}
}
