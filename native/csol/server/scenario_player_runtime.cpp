// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_player_runtime.h"
#include "characters.h"

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
