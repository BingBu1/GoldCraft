// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_player_impulse.h"
#include "scenario_player_runtime.h"
#include "scenario_session.h"
#include "game_rules_gate.h"
#include "../scenario/player_impulse.hpp"
#include <cmath>

#pragma clang fp contract(off)

bool CSOL_ApplyScenarioPlayerImpulse(const std::shared_ptr<CCSOLScenarioSession>& session,
    const EHANDLE& target, const Vector& requested, bool& applied,
    csol::scenario::ConfigError& error)
{
    applied = false;
    if (!session || !session->Map().EntityAccessAllowed() || !error.message.empty()) return false;
    const auto access = session->Map().EntityAccess();
    const auto players = CSOL_ScenarioPlayerAuthority(access.get());
    if (!players || !players->Active())
    { error.message = "scenario player impulse authority is unavailable"; return false; }
    const auto state = players->Find(target);
    auto* player = target.Get<CBasePlayer>();
    if (!state || !player || !player->pev || player->has_disconnected ||
        (player->pev->flags & (FL_KILLME | FL_DORMANT))) return true;
    const auto life = state->skillLifeSerial;
    const auto current = [&]
    { return access && access->Allowed() && session->Map().EntityAccess() == access &&
        CSOL_ScenarioPlayerAuthority(access.get()) == players && players->Active() && players->Find(target) == state &&
        target.Get<CBasePlayer>() == player && state->skillLifeSerial == life; };
    if (!gpGlobals || !std::isfinite(gpGlobals->time) ||
        !std::isfinite(requested.x) || !std::isfinite(requested.y) || !std::isfinite(requested.z))
    { error.message = "scenario player impulse contains a non-finite value"; return false; }
    const float now = gpGlobals->time;
    // The selected scenario rules' v3FC is10700CB0 (true, no modification).
    // v1E8 rejects active StatBuff+AC or the independent action constraint flag.
    if (state->actionKnockbackBlocked) return true;
    for (const auto& buff : state->buffs)
        if (buff.blocksKnockback && buff.Active(now)) return true;
    // The scenario-registered modules, including ClassMod/Shield/Cloak/Addon,
    // inherit the verified10611B90 ret8 callback. Zombie-mode overrides are
    // outside this scenario-only entry. Their absence does not waive StatBuff.
    float ratio{};
    if (!csol::scenario::PlayerImpulseRatio(std::span<const CSOLScenarioStatBuff>(state->buffs),
        now, -1, 1.0f, ratio))
    { error.message = "scenario player impulse modifiers are invalid"; return false; }
    const Vector old = player->pev->velocity;
    const Vector velocity(old.x + (requested.x - old.x) * ratio,
        old.y + (requested.y - old.y) * ratio, old.z + (requested.z - old.z) * ratio);
    if (!std::isfinite(velocity.x) || !std::isfinite(velocity.y) || !std::isfinite(velocity.z))
    { error.message = "scenario player impulse result is not finite"; return false; }
    if (!current()) return false;
    player->pev->velocity = velocity;
    applied = true;
    return true;
}
