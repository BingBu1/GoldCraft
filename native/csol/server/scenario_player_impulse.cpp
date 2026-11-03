// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_player_impulse.h"
#include "scenario_player_runtime.h"
#include "scenario_session.h"
#include "game_rules_gate.h"
#include "../scenario/player_impulse.hpp"
#include <cmath>

#pragma clang fp contract(off)

namespace
{
bool ApplyImpulse(const std::shared_ptr<CCSOLScenarioSession>& session,
    const EHANDLE& target, const Vector& requested, bool& applied,
    csol::scenario::ConfigError& error, bool ordinary, bool useModifiers = true)
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
    const auto spawnSerial = state->nativeSpawnSerial;
    const int spawns = player->m_iNumSpawns;
    const auto round = session->Map().RoundGeneration();
    const auto current = [&]
    { return access && access->Allowed() && session->Map().EntityAccess() == access &&
        CSOL_ScenarioPlayerAuthority(access.get()) == players && players->Active() && players->Find(target) == state &&
        target.Get<CBasePlayer>() == player && !player->has_disconnected &&
        !(player->pev->flags & (FL_KILLME | FL_DORMANT)) && (ordinary ? state->skillLifeSerial == life :
            state->nativeSpawnSerial == spawnSerial) &&
        player->m_iNumSpawns == spawns && session->Map().RoundGeneration() == round; };
    if (!gpGlobals || !std::isfinite(gpGlobals->time) ||
        !std::isfinite(requested.x) || !std::isfinite(requested.y) || !std::isfinite(requested.z))
    { error.message = "scenario player impulse contains a non-finite value"; return false; }
    const float now = gpGlobals->time;
    // The selected scenario rules' v3FC is10700CB0 (true, no modification).
    // v1E8 rejects active StatBuff+AC or the independent action constraint flag.
    if (ordinary)
    {
        if (state->actionKnockbackBlocked) return true;
        for (const auto& buff : state->buffs)
            if (buff.blocksKnockback && buff.Active(now)) return true;
    }
    // The scenario-registered modules, including ClassMod/Shield/Cloak/Addon,
    // inherit the verified10611B90 ret8 callback. Zombie-mode overrides are
    // outside this scenario-only entry. Their absence does not waive StatBuff.
    const Vector old = player->pev->velocity;
    bool modify = true;
    if (!ordinary)
    {
        modify = useModifiers;
        if (modify)
        {
            modify = player->IsAlive() != FALSE;
            if (!current()) return false;
        }
        if (modify)
        {
            modify = player->IsPlayer() != FALSE;
            if (!current()) return false;
        }
    }
    Vector velocity = requested;
    if (modify)
    {
        float ratio{};
        if (!csol::scenario::PlayerImpulseRatio(std::span<const CSOLScenarioStatBuff>(state->buffs),
            now, ordinary ? -1 : 1, 1.0f, ratio))
        { error.message = "scenario player impulse modifiers are invalid"; return false; }
        velocity = Vector(old.x + (requested.x - old.x) * ratio,
            old.y + (requested.y - old.y) * ratio, old.z + (requested.z - old.z) * ratio);
    }
    if (!std::isfinite(velocity.x) || !std::isfinite(velocity.y) || !std::isfinite(velocity.z))
    { error.message = "scenario player impulse result is not finite"; return false; }
    if (!current()) return false;
    player->pev->velocity = velocity;
    applied = true;
    return true;
}
}

bool CSOL_ApplyScenarioPlayerImpulse(const std::shared_ptr<CCSOLScenarioSession>& session,
    const EHANDLE& target, const Vector& requested, bool& applied,
    csol::scenario::ConfigError& error)
{
    return ApplyImpulse(session, target, requested, applied, error, true);
}

bool CSOL_ApplyZombieBombPlayerImpulse(const std::shared_ptr<CCSOLScenarioSession>& session,
    const EHANDLE& target, const Vector& requested, bool useModifiers, bool& applied,
    csol::scenario::ConfigError& error)
{
    // MP11409970, mode15, final argument1. It uses StatBuff priority1;
    // the action/blocksKnockback gates belong to other mode dispatches.
    return ApplyImpulse(session, target, requested, applied, error, false, useModifiers);
}
