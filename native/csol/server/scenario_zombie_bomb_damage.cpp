// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_zombie_bomb.h"
#include "scenario_monster_skills.h"
#include "scenario_player_runtime.h"
#include "scenario_player_impulse.h"
#include "scenario_runtime.h"
#include "scenario_session.h"
#include "game_rules_gate.h"
#include <algorithm>
#include <bit>
#include <cmath>

#pragma clang fp contract(off)

namespace
{
// MP114BBE90: one Newton step with the original seed and float stores.
// Both the direction and distance falloff use this approximation.
float InverseLength(float square)
{
    const auto bits = std::bit_cast<std::int32_t>(square);
    const float seed = std::bit_cast<float>(std::uint32_t{0x5f3759d5} -
        static_cast<std::uint32_t>(bits >> 1));
    float product = square * 0.5f;
    product *= seed;
    product *= seed;
    return (1.5f - product) * seed;
}

struct DamageCall
{
    std::shared_ptr<CCSOLScenarioRuntime> runtime;
    std::shared_ptr<CCSOLScenarioSession> session;
    std::shared_ptr<CCSOLScenarioPlayerRuntime> players;
    std::shared_ptr<const CSOLScenarioEntityAccess> access;
    const CSOLScenarioSdkScope& sdk;
    const CSOLZombieBombHitSource& source;
    std::uint64_t round;
    bool Current() const
    {
        return sdk.Current() && access && access->Allowed() && !runtime->Closing() &&
            runtime->session == session && runtime->players == players && players->Active() &&
            session->Map().EntityAccess() == access && session->Map().RoundGeneration() == round && source.Current();
    }
    CBaseEntity* Attacker() const
    {
        auto* creator = source.Creator();
        return creator ? creator : source.Current();
    }
};

struct PlayerCall
{
    const DamageCall& damage;
    EHANDLE handle;
    std::shared_ptr<const CSOLScenarioPlayerRuntimeState> state;
    const CBasePlayer* identity;
    int spawns;
    std::uint64_t spawnSerial;
    CBasePlayer* Current() const
    {
        if (!damage.Current() || !state || damage.players->Find(handle) != state ||
            state->nativeSpawnSerial != spawnSerial) return nullptr;
        auto* player = handle.Get<CBasePlayer>();
        return player == identity && player && !player->has_disconnected &&
            !(player->pev->flags & (FL_KILLME | FL_DORMANT)) && player->m_iNumSpawns == spawns ? player : nullptr;
    }
};

bool HitPlayer(PlayerCall& call, const Vector& center, float damage, csol::scenario::ConfigError& error)
{
    auto* player = call.Current();
    if (!player) return true;
    const Vector delta = player->pev->origin - center;
    const float square = (delta.y * delta.y + delta.x * delta.x) + delta.z * delta.z;
    if (!std::isfinite(square))
    { error.message = "scenario zombie bomb player distance is not finite"; return false; }
    if (!(square < 250.0f * 250.0f)) return true;
    const float inverse = InverseLength(square);
    const float distance = 1.0f / inverse;
    const float falloff = std::max(0.0f, (250.0f - distance) / 250.0f);
    float force = 350.0f;
    if (damage != 0)
    {
        bool immune{};
        const CSOLMonsterSkillGuard guard{
            [](const void* value) { return static_cast<const PlayerCall*>(value)->Current() != nullptr; }, &call};
        const bool prepared = call.damage.players->PrepareZombieBombHit(call.handle,
            guard, damage, force, immune, error);
        if (!call.damage.Current()) return false;
        player = call.Current();
        if (!player) return true;
        if (!prepared) return false;
        if (immune) return true;
        auto* inflictor = call.damage.source.Current();
        auto* attacker = call.damage.Attacker();
        if (!inflictor || !attacker) return false;
        const int flags = attacker->pev->flags & FL_CLIENT ? DMG_EXPLOSION : DMG_BLAST;
        player->TakeDamage(inflictor->pev, attacker->pev, damage, flags);
        if (!call.damage.Current()) return false;
        player = call.Current();
        if (!player) return true;
    }
    // Zero damage skips protection and TakeDamage, but still applies force
    // and both messages. Damage return=false does not suppress this stage.
    const Vector direction(delta.x * inverse, delta.y * inverse, delta.z * inverse);
    const float amount = force * falloff;
    Vector impulse(direction.x * amount, direction.y * amount, direction.z * amount);
    if (!(player->pev->flags & FL_ONGROUND)) impulse *= 0.9f;
    else if (player->pev->flags & FL_DUCKING) impulse *= 0.8f;
    Vector requested = player->pev->velocity + impulse;
    requested.z = std::min(1500.0f, requested.z);
    bool applied{};
    // This producer only has a monster creator. The original friendly/self
    // condition therefore permits modifiers; the consumer still tests life.
    const bool moved = CSOL_ApplyZombieBombPlayerImpulse(call.damage.session,
        call.handle, requested, true, applied, error);
    if (!call.damage.Current()) return false;
    player = call.Current();
    if (!player) return true;
    if (!moved) return false;
    if (!call.damage.session->Messages().SendMonsterSkillShake(player->edict()))
    { error.message = "scenario zombie bomb screen shake failed"; return false; }
    if (!call.damage.Current()) return false;
    player = call.Current();
    if (!player) return true;
    if (!call.damage.session->Messages().SendOberonImpact(player->edict(), false))
    { error.message = "scenario zombie bomb view kick failed"; return false; }
    return call.damage.Current();
}
}

bool CSOL_ApplyZombieBombDamage(const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    const CSOLZombieBombHitSource& source, float damage, csol::scenario::ConfigError& error)
{
    const auto session = runtime ? runtime->session : nullptr;
    const auto players = runtime ? runtime->players : nullptr;
    if (!session || !players || !source.Current()) return false;
    CSOLScenarioSdkScope sdk(CSOL_ScenarioOwnerRules(session.get()));
    DamageCall call{runtime, session, players, session->Map().EntityAccess(), sdk, source,
        session->Map().RoundGeneration()};
    if (!call.Current()) return false;
    if (!gpGlobals || gpGlobals->maxClients < 0 || gpGlobals->maxClients > 32 ||
        gpGlobals->maxEntities <= 0 || !g_engfuncs.pfnPEntityOfEntIndex ||
        !g_engfuncs.pfnFindEntityInSphere || !g_engfuncs.pfnEntOffsetOfPEntity || !std::isfinite(damage))
    { error.message = "scenario zombie bomb damage services are unavailable"; return false; }
    // MP11428180 updates the hidden entity, after its visual message.
    source.Current()->pev->origin.z -= 20.0f;
    const Vector center = source.Current()->pev->origin;
    const int clients = gpGlobals->maxClients;
    for (int slot = 1; slot <= clients; ++slot)
    {
        auto* entry = INDEXENT(slot);
        if (!call.Current()) return false;
        if (!entry || entry->free) continue;
        const EHANDLE handle(entry);
        auto* player = dynamic_cast<CBasePlayer*>(handle.Get<CBaseEntity>());
        if (!player || player->has_disconnected || (player->pev->flags & (FL_KILLME | FL_DORMANT))) continue;
        const auto state = players->Find(handle);
        if (!state)
        { error.message = "scenario zombie bomb player state is unavailable"; return false; }
        PlayerCall victim{call, handle, state, player, player->m_iNumSpawns, state->nativeSpawnSerial};
        if (!HitPlayer(victim, center, damage, error)) return false;
        if (!call.Current()) return false;
    }

    // Preserve the engine sphere traversal order. The cursor is an edict
    // slot, never a CBaseEntity pointer retained across TakeDamage callbacks.
    edict_t* cursor{};
    int remaining = gpGlobals->maxEntities;
    for (;;)
    {
        auto* entry = g_engfuncs.pfnFindEntityInSphere(cursor, center, 250.0f);
        if (!call.Current()) return false;
        if (!entry) break;
        const EHANDLE handle(entry);
        const int offset = OFFSET(entry);
        if (!call.Current()) return false;
        if (!offset) break;
        if (--remaining < 0 || entry == cursor)
        { error.message = "scenario zombie bomb sphere traversal did not advance"; return false; }
        cursor = entry;
        auto* target = handle.Get<CBaseEntity>();
        if (!target || (target->pev->flags & FL_KILLME) || target == source.Current()) continue;
        const auto currentTarget = [&]() -> CBaseEntity*
        {
            if (!call.Current()) return nullptr;
            auto* current = handle.Get<CBaseEntity>();
            return current == target && !(current->pev->flags & FL_KILLME) ? current : nullptr;
        };
        const bool isPlayer = target->IsPlayer() != FALSE;
        if (!call.Current()) return false;
        if (isPlayer || !currentTarget()) continue;
        bool excluded{};
        for (int ignored : {17, 45, 28})
        {
            const int classification = currentTarget()->Classify();
            if (!call.Current()) return false;
            if (!currentTarget() || classification == ignored) { excluded = true; break; }
        }
        if (excluded) continue;
        target = currentTarget();
        if (!target || target->pev->takedamage == DAMAGE_NO) continue;
        auto* inflictor = source.Current();
        auto* attacker = call.Attacker();
        if (!inflictor || !attacker) return false;
        target->TakeDamage(inflictor->pev, attacker->pev, damage, DMG_EXPLOSION);
        if (!call.Current()) return false;
    }
    return true;
}
