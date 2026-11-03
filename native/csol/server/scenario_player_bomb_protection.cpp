// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_player_runtime.h"
#include "scenario_monster_skills.h"
#include "scenario_entity_lookup.h"
#include <cmath>
#include <ctime>
#include <random>

#pragma clang fp contract(off)

namespace
{
bool PublishProtection(CCSOLScenarioSession& session, CBasePlayer& player,
    float duration, CSOLMonsterSkillGuard guard, csol::scenario::ConfigError& error)
{
    const auto fail = [&](const char* message) { error.message = message; return false; };
    const auto messages = session.Messages();
    if (!guard.Current()) return false;
    if (!messages.SendZombieBombProtection(player.edict(), duration))
        return fail("scenario zombie bomb protection overlay failed");
    if (!guard.Current()) return false;
    // MP1140A4B0 handles player/monster recipients without an IsAlive gate.
    // This entry owns a concrete player. Alpha-test render mode suppresses
    // only the world attachment, after the owner's screen message.
    const bool isPlayer = player.IsPlayer() != FALSE;
    if (!guard.Current()) return false;
    if (!isPlayer || player.pev->rendermode == kRenderTransAlpha) return true;
    const float height = player.pev->maxs.z;
    if (!std::isfinite(height) || !g_engfuncs.pfnPEntityOfEntIndex ||
        !g_engfuncs.pfnEntOffsetOfPEntity || !g_engfuncs.pfnIndexOfEdict)
        return fail("scenario zombie bomb protection attachment services are unavailable");
    const auto access = session.Map().EntityAccess();
    for (int slot = 1;; ++slot)
    {
        if (!guard.Current()) return false;
        if (!gpGlobals || gpGlobals->maxClients < 0 || gpGlobals->maxClients > MAX_CLIENTS ||
            gpGlobals->maxClients >= gpGlobals->maxEntities)
            return fail("scenario zombie bomb protection client capacity is invalid");
        if (slot > gpGlobals->maxClients) return true;
        auto* receiver = CSOL_ScenarioPlayerEntity(slot, access);
        if (!guard.Current()) return false;
        if (!receiver) continue;
        const EHANDLE recipient(receiver);
        const int offset = OFFSET(receiver->edict());
        if (!guard.Current()) return false;
        receiver = recipient.Get<CBaseEntity>();
        if (!offset || !receiver) continue;
        const int index = ENTINDEX(player.edict());
        if (!guard.Current()) return false;
        receiver = recipient.Get<CBaseEntity>();
        if (!receiver) continue;
        if (!messages.SendMonsterDamageEffect(receiver->edict(), index, 53, height, duration))
            return fail("scenario zombie bomb protection attachment failed");
    }
}

std::uint32_t PartPercent(std::mt19937& random)
{
    // MP 114E08F4: integer bounded sampling. The low product must be >=96;
    // modulo100, float scaling and WeaponAttributeRandom are not equivalent.
    std::uint64_t product;
    do { product = std::uint64_t(random()) * 100; }
    while (static_cast<std::uint32_t>(product) < 96);
    return static_cast<std::uint32_t>(product >> 32);
}
}

bool CCSOLScenarioPlayerRuntime::PrepareZombieBombHit(const EHANDLE& handle,
    CSOLMonsterSkillGuard guard, float& damage, float& force, bool& immune,
    csol::scenario::ConfigError& error, std::uint32_t (*seed)())
{
    immune = false;
    const auto keepAlive = shared_from_this();
    const auto state = Resolve(handle);
    auto* player = state ? dynamic_cast<CBasePlayer*>(handle.Get<CBaseEntity>()) : nullptr;
    if (!player || !gpGlobals || !std::isfinite(gpGlobals->time) ||
        !std::isfinite(damage) || !std::isfinite(force)) return false;
    const auto life = state->monsterLifeSerial;
    const auto spawnSerial = state->nativeSpawnSerial;
    const auto lifecycle = m_lifecycle;
    const int spawn = player->m_iNumSpawns;
    const auto current = [&]
    { return guard.Current() && m_lifecycle == lifecycle && Resolve(handle) == state &&
        state->monsterLifeSerial == life && state->nativeSpawnSerial == spawnSerial &&
        handle.Get<CBaseEntity>() == player &&
        player->m_iNumSpawns == spawn; };
    if (!current()) return false;
    // MP 11531709 jumps straight to impulse; it does not consume either RNG.
    if (damage == 0) return true;
    if (state->zombieBombProtectionUntil == 0)
    {
        const auto weapons = m_session ? m_session->Weapons() : nullptr;
        if (!weapons || !weapons->Current())
        { error.message = "scenario zombie bomb part directory is unavailable"; return false; }
        const auto collectionCurrent = [&]
        { return current() && m_session->Weapons() == weapons && weapons->Current(); };
        const CSOLMonsterSkillGuard partGuard{
            [](const void* value) { return (*static_cast<const decltype(collectionCurrent)*>(value))(); },
            &collectionCurrent};
        std::vector<CSOLWeaponPartEffect> records;
        if (!weapons->CollectPlayerParts(*player, *state, 15, partGuard, records, error) ||
            !collectionCurrent()) return false;
        if (!records.empty())
        {
            std::uint32_t value;
            try { value = seed ? seed() : static_cast<std::uint32_t>(_time64(nullptr)); }
            catch (...)
            {
                if (!current()) return false;
                error.message = "scenario zombie bomb part entropy is unavailable";
                return false;
            }
            if (!collectionCurrent()) return false;
            std::mt19937 random(value);
            double duration{}, percentage = 1;
            std::uint32_t part{};
            for (const auto& record : records)
            {
                const auto sample = PartPercent(random);
                if (record.parameters[0] > double(sample) && record.parameters[1] > duration)
                {
                    duration = record.parameters[1];
                    percentage = record.parameters[2];
                    part = record.id;
                }
            }
            if (duration > 0)
            {
                const double until = double(gpGlobals->time) + duration;
                const double ratio = percentage * .01;
                if (!std::isfinite(until) || !std::isfinite(ratio))
                { error.message = "scenario zombie bomb protection overflowed"; return false; }
                state->zombieBombProtectionRatio = ratio;
                state->zombieBombProtectionUntil = until;
                state->zombieBombProtectionPart = part;
                if (!PublishProtection(*m_session, *player, static_cast<float>(duration),
                    partGuard, error) || !collectionCurrent()) return false;
            }
        }
    }
    const double now = double(gpGlobals->time);
    if (state->zombieBombProtectionUntil > 0 && state->zombieBombProtectionUntil > now)
    {
        const double ratio = state->zombieBombProtectionRatio;
        damage = static_cast<float>(double(damage) - double(damage) * ratio);
        force = static_cast<float>(double(force) - double(force) * ratio);
    }
    const CSOLMonsterSkillGuard resistanceGuard{
        [](const void* value) { return (*static_cast<const decltype(current)*>(value))(); }, &current};
    return CheckMonsterAddon13(handle, resistanceGuard, immune, error) && current();
}
