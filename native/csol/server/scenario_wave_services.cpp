// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_wave_services.h"
#include "scenario_runtime.h"
#include "scenario_session.h"
#include "scenario_death_services.h"

#include <algorithm>
#include <bit>
#include <limits>

std::shared_ptr<CCSOLScenarioRuntime> CCSOLScenarioWaveServices::Pin() const
{
    const auto runtime = m_runtime.lock();
    if (!runtime || runtime->Closing() || runtime->waveEvents.get() != this ||
        runtime->session != m_session || !m_session || !m_session->Map().EntityAccessAllowed()) return {};
    return runtime;
}

bool CCSOLScenarioWaveServices::Check(csol::scenario::ConfigError& error) const
{
    if (!Pin()) { error = {}; return false; }
    if (!m_error.message.empty()) { error = m_error; return false; }
    if (m_cancelled) { error = {}; return false; }
    return true;
}

std::uint64_t CCSOLScenarioWaveServices::Register(std::shared_ptr<CCSOLScenarioWaveObserver> observer)
{
    const auto runtime = Pin();
    if (!runtime || !observer || m_cancelled || !m_error.message.empty()) return 0;
    for (const auto& slot : m_observers)
        if (slot->enabled && slot->observer == observer) return slot->token;
    if (m_sequence == std::numeric_limits<std::uint64_t>::max()) return 0;
    auto slot = std::make_shared<Subscription>();
    slot->token = ++m_sequence;
    slot->observer = std::move(observer);
    m_observers.push_back(slot);
    return slot->token;
}

bool CCSOLScenarioWaveServices::Unregister(std::uint64_t token)
{
    const auto runtime = Pin();
    if (!runtime || !token) return false;
    const auto keepAlive = runtime->waveEvents;
    const auto found = std::find_if(m_observers.begin(), m_observers.end(),
        [token](const auto& slot) { return slot->token == token; });
    if (found == m_observers.end()) return false;
    auto retired = *found;
    retired->enabled = false;
    m_observers.erase(found);
    // Destruction can reenter; there are no registry writes after this point.
    return true;
}

void CCSOLScenarioWaveServices::WaveActivated(std::uint16_t id)
{
    const auto runtime = Pin();
    if (!runtime || m_cancelled || !m_error.message.empty()) return;
    const auto keepAlive = runtime->waveEvents;
    auto& map = m_session->Map();
    const auto access = map.EntityAccess();
    const auto round = map.RoundGeneration();
    const auto generation = runtime->Generation();
    if (map.State() != CSOLScenarioMapState::RoundActive) { m_cancelled = true; return; }
    const auto current = [&]()
    {
        return Pin() == runtime && runtime->Generation() == generation && access->Allowed() &&
            map.EntityAccess() == access && map.RoundGeneration() == round &&
            map.State() == CSOLScenarioMapState::RoundActive;
    };
    if (m_dispatchDepth) { m_error.message = "scenario wave observer dispatch reentered"; return; }
    struct DispatchScope { std::size_t& depth; ~DispatchScope() { --depth; } } scope{m_dispatchDepth};
    ++m_dispatchDepth;
    const auto batch = m_observers;
    for (const auto& slot : batch)
    {
        if (!current() || m_cancelled || !m_error.message.empty()) return;
        if (!slot->enabled) continue;
        auto observer = slot->observer;
        csol::scenario::ConfigError error;
        bool accepted{};
        try { accepted = observer->WaveActivated(std::bit_cast<std::int16_t>(id), error); }
        catch (...) { error.message = "scenario wave observer threw an exception"; }
        observer.reset();
        if (!current() || m_cancelled || !m_error.message.empty()) return;
        if (!accepted || !error.message.empty())
        {
            m_error = std::move(error);
            m_cancelled = m_error.message.empty();
            return;
        }
    }
}

void CCSOLScenarioWaveServices::BeforeSpatialWaveEnd(std::uint16_t)
{
    const auto runtime = Pin();
    if (!runtime || m_cancelled || !m_error.message.empty()) return;
    const auto deaths = runtime->deathServices;
    if (!deaths || !deaths->Golden().EmptyPlan())
        m_error.message = "scenario spatial wave end requires the loaded Golden plan consumer";
    // Empty pending plans have no record to erase or candidate to reschedule.
}

void CCSOLScenarioWaveServices::WavesChanged(std::span<const std::uint16_t>)
{
    // Dedicated MP has no summary packet here. Map already owns the optional
    // local 30-entry cache and checks identity around its engine query.
}

void CCSOLScenarioWaveServices::GoalReached()
{
    if (Pin())
        m_error.message = "scenario FinishRound consumers are not completely implemented";
    // Original g enters FinishRound directly. There is no additional wave
    // observer broadcast which could stand in for its missing result/awards.
}
