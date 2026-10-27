// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_player_runtime.h"
#include "scenario_death_owner.h"
#include "characters.h"

#include <cmath>
#include <limits>

bool CCSOLScenarioPlayerRuntime::Active() const
{
    return m_active && m_error.message.empty() && m_session &&
        m_session->State() == CSOLScenarioSessionState::Active;
}

bool CCSOLScenarioPlayerRuntime::Fail(const char* reason)
{
    if (m_error.message.empty()) m_error.message = reason;
    return false;
}

bool CCSOLScenarioPlayerRuntime::Activate()
{
    auto keepAlive = shared_from_this();
    if (m_active || !m_session || m_session->State() != CSOLScenarioSessionState::Active) return false;
    const auto lifecycle = m_lifecycle;
    const auto weak = weak_from_this();
    if (weak.expired()) return Fail("scenario player authority must have a retained owner");
    // Equipment and upgrades keep only this weak callback, avoiding a cycle
    // with the session which owns both services.
    if (!m_session->BindEquipmentServices([weak](const EHANDLE& player)
        {
            const auto owner = weak.lock();
            return owner && owner->RecordCapEvent(player);
        })) return Fail("scenario equipment and upgrade services could not be installed");
    if (lifecycle != m_lifecycle || m_session->State() != CSOLScenarioSessionState::Active) return false;
    m_players = {};
    m_error = {};
    m_active = true;
    return true;
}

void CCSOLScenarioPlayerRuntime::Invalidate()
{
    // Invalidate before touching any handle; a retained callback may run after
    // the engine has discarded the previous map's edict array.
    m_active = false;
    ++m_lifecycle;
    for (auto& state : m_players)
    {
        if (state) state->generation = 0;
        state.reset();
    }
}

std::shared_ptr<CSOLScenarioPlayerRuntimeState> CCSOLScenarioPlayerRuntime::Resolve(const EHANDLE& handle) const
{
    if (!Active()) return {};
    auto* player = dynamic_cast<CBasePlayer*>(handle.Get<CBaseEntity>());
    if (!player || player->has_disconnected || (player->pev->flags & FL_KILLME)) return {};
    for (const auto& state : m_players)
        if (state && state->generation && state->player.Get() == handle.Get()) return state;
    return {};
}

std::shared_ptr<const CSOLScenarioPlayerRuntimeState> CCSOLScenarioPlayerRuntime::Find(const EHANDLE& player) const
{
    return Resolve(player);
}

std::shared_ptr<CSOLScenarioPlayerRuntimeState> CCSOLScenarioPlayerRuntime::Ensure(CBasePlayer& player)
{
    if (!Active()) return {};
    const EHANDLE handle(&player);
    if (auto state = Resolve(handle)) return state;
    const int index = player.entindex();
    if (!Active() || handle.Get<CBaseEntity>() != &player || player.has_disconnected ||
        (player.pev->flags & FL_KILLME) || index < 1 || index >= static_cast<int>(m_players.size())) return {};
    auto& previous = m_players[index];
    if (previous) previous->generation = 0;
    auto state = std::make_shared<CSOLScenarioPlayerRuntimeState>();
    state->player = handle;
    state->generation = ++m_generation;
    state->profileRevision = 1;
    previous = state;
    return state;
}

bool CCSOLScenarioPlayerRuntime::Prepare(CBasePlayer& player)
{
    auto keepAlive = shared_from_this();
    const auto state = Ensure(player);
    if (!state || state->preparing) return false;
    struct Preparing { bool& value; ~Preparing() { value = false; } } preparing{state->preparing};
    state->preparing = true;
    int classId{};
    if (!CSOL_CharacterScenarioClass(&player, classId))
        return Fail("selected scenario character has no verified upgrade class");
    if (Resolve(state->player) != state) return false;
    const auto players = m_session->Players();
    const auto upgrades = players ? players->Upgrades(state->player) : nullptr;
    if (state->classId == classId && state->appliedRevision == state->profileRevision &&
        upgrades && upgrades->ConfiguredClass() == classId) return true;
    const auto revision = state->profileRevision;
    // Bind the actual owned inventory before Configure publishes levels or a
    // weapon callback asks ResetMaxSpeed to consume them.
    if (!m_session->PrepareEquippedPlayer(player, classId, state->profile.characterFeature,
        state->profile.elevenLevelLimit, state->profile.equipment))
        return Fail("scenario player equipment or upgrade initialization failed");
    if (Resolve(state->player) != state || state->profileRevision != revision) return false;
    state->classId = classId;
    state->appliedRevision = revision;
    return true;
}

bool CCSOLScenarioPlayerRuntime::SetProfile(CBasePlayer& player, CSOLScenarioPlayerProfile profile)
{
    auto keepAlive = shared_from_this();
    const auto state = Ensure(player);
    if (!state || state->preparing) return false;
    state->profile = std::move(profile);
    ++state->profileRevision;
    return true;
}

void CCSOLScenarioPlayerRuntime::Remove(const EHANDLE& player)
{
    auto keepAlive = shared_from_this();
    if (!m_active) return;
    // ClientDisconnected has already marked has_disconnected. Do not use
    // Resolve's live-player filter to skip its required state invalidation.
    for (auto& state : m_players)
    {
        if (!state || state->player.Get() != player.Get()) continue;
        state->generation = 0;
        state.reset();
    }
    m_session->RemovePlayer(player);
}

bool CCSOLScenarioPlayerRuntime::RecordCapEvent(const EHANDLE& player)
{
    const auto state = Resolve(player);
    if (!state) return false;
    if (!state->capEventPending)
    {
        state->capEventPending = true;
        state->capEventSequence = ++m_eventSequence;
    }
    return true;
}

bool CCSOLScenarioPlayerRuntime::AcknowledgeCapEvent(const EHANDLE& player,
    std::uint64_t generation, std::uint64_t sequence)
{
    const auto state = Resolve(player);
    if (!state || !state->capEventPending || state->generation != generation ||
        state->capEventSequence != sequence) return false;
    state->capEventPending = false;
    return true;
}

bool CCSOLScenarioPlayerRuntime::RecordActiveWeaponKill(CSOLMonsterDeathCall& call,
    csol::scenario::ConfigError& error)
{
    auto keepAlive = shared_from_this();
    if (!Active() || !call.Current()) return false;
    auto* player = dynamic_cast<CBasePlayer*>(call.Attacker());
    if (!player) { error.message = "scenario weapon kill has no player attacker"; return false; }
    const EHANDLE handle(player);
    const auto state = Resolve(handle);
    if (!state) return false;
    const auto current = [&]
    { return Active() && call.Current() && Resolve(handle) == state && call.Attacker() == player; };
    auto* weapon = dynamic_cast<CBasePlayerWeapon*>(player->m_pActiveItem);
    if (!weapon || (weapon->pev->flags & FL_KILLME)) return true;
    const auto weapons = m_session->Weapons();
    if (!weapons || !weapons->Current())
    { error.message = "scenario weapon kill catalog is unavailable"; return false; }
    std::int32_t item{};
    // This phase deliberately uses the current weapon, not the lethal source.
    if (!weapons->Item(*player, weapon->m_iId, item, error) || !current()) return false;
    // CSO captures the catalog item before its virtual Bot query. A callback
    // may switch weapons, but this statistic still belongs to that item.
    const bool bot = player->IsBot();
    if (!current()) return false;
    if (!bot && item && !state->profile.weaponEffectsBlocked21F2) ++state->weaponKills[item];
    return true;
}

bool CCSOLScenarioPlayerRuntime::AddMonsterFrag(CSOLMonsterDeathCall& call, bool& incremented,
    csol::scenario::ConfigError& error)
{
    #pragma clang fp contract(off)
    auto keepAlive = shared_from_this();
    incremented = false;
    if (!Active() || !call.Current()) return false;
    auto* player = dynamic_cast<CBasePlayer*>(call.Attacker());
    if (!player) { error.message = "scenario frag has no player attacker"; return false; }
    const EHANDLE handle(player);
    const auto state = Resolve(handle);
    if (!state) return false;
    const auto current = [&]
    { return Active() && call.Current() && Resolve(handle) == state && call.Attacker() == player; };
    const float before = player->pev->frags;
    const bool add = before < 65000.0f;
    if (add) player->pev->frags = before + 1.0f;
    incremented = add;
    const int index = player->entindex();
    if (!current()) return false;
    if (index < 1 || index > 32)
    { error.message = "scenario frag player index is invalid"; return false; }
    // Index lookup is an engine callback. As in CSO, sample frags afterwards.
    const double integer = std::trunc(static_cast<double>(player->pev->frags));
    if (!std::isfinite(integer) || integer < std::numeric_limits<std::int32_t>::min() ||
        integer > std::numeric_limits<std::int32_t>::max())
    { error.message = "scenario frags cannot be represented by the original message conversion"; return false; }
    // Even a capped value is broadcast. Keep the comparison/add separate;
    // min(before + 1, 65000) changes the original noninteger boundary.
    const auto messages = m_session->Messages();
    if (!messages.SendPlayerFrags(index, static_cast<std::int32_t>(integer)))
    { if (current()) error.message = "scenario frag notification failed"; return false; }
    return current();
}

bool CCSOLScenarioPlayerRuntime::ClassifyKill(CSOLMonsterDeathCall& call, int kind,
    csol::scenario::AnnounceCandidates& output, csol::scenario::ConfigError& error)
{
    auto keepAlive = shared_from_this();
    output = {};
    if (!Active() || !call.Current()) return false;
    auto* player = dynamic_cast<CBasePlayer*>(call.Attacker());
    if (!player) { error.message = "scenario classification has no player attacker"; return false; }
    const EHANDLE handle(player);
    const auto state = Resolve(handle);
    if (!state) return false;
    if (player->pev->flags & FL_DORMANT) return true;
    const auto candidates = state->classification.Record(kind);
    const bool bot = player->IsBot();
    if (!Active() || !call.Current() || Resolve(handle) != state || call.Attacker() != player) return false;
    // Original counters (including C/D resets) have already changed for Bots.
    if (bot || (player->pev->flags & FL_DORMANT)) return true;
    const auto table = m_session->Announcements();
    if (!table) { error.message = "scenario announce table is unavailable"; return false; }
    for (const auto id : candidates.Values())
        if (table->Allowed(15, id)) output.ids[output.count++] = id;
    return true;
}
