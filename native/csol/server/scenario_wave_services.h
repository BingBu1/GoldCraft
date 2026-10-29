// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../scenario/trigger_wave_actions.hpp"
#include <memory>

class CCSOLScenarioRuntime;
class CCSOLScenarioSession;

class CCSOLScenarioWaveObserver
{
public:
    virtual ~CCSOLScenarioWaveObserver() = default;
    virtual bool WaveActivated(std::int16_t id, csol::scenario::ConfigError& error) = 0;
};

// Ordinary Ver2 has an actual empty controller observer list at construction.
// AbilityMode/RogueLite registration and complete FinishRound are separate.
class CCSOLScenarioWaveServices final : public csol::scenario::TriggerWaveEvents
{
public:
    CCSOLScenarioWaveServices(std::weak_ptr<CCSOLScenarioRuntime> runtime,
        std::shared_ptr<CCSOLScenarioSession> session)
        : m_runtime(std::move(runtime)), m_session(std::move(session)) {}
    bool Check(csol::scenario::ConfigError& error) const override;
    void WaveActivated(std::uint16_t id) override;
    void BeforeSpatialWaveEnd(std::uint16_t id) override;
    void WavesChanged(std::span<const std::uint16_t> active) override;
    void GoalReached() override;
    // Trusted registrations. Snapshot/token removal and recursive-dispatch
    // rejection are host safety adaptations; CSO's list mutation semantics
    // are not established. Preserve current order, honor removals before later
    // slots, and defer newly registered observers until the next dispatch.
    std::uint64_t Register(std::shared_ptr<CCSOLScenarioWaveObserver> observer);
    bool Unregister(std::uint64_t token);

private:
    struct Subscription
    {
        std::uint64_t token{};
        bool enabled{true};
        std::shared_ptr<CCSOLScenarioWaveObserver> observer;
    };
    std::shared_ptr<CCSOLScenarioRuntime> Pin() const;
    std::weak_ptr<CCSOLScenarioRuntime> m_runtime;
    std::shared_ptr<CCSOLScenarioSession> m_session;
    std::vector<std::shared_ptr<Subscription>> m_observers;
    std::uint64_t m_sequence{};
    std::size_t m_dispatchDepth{};
    csol::scenario::ConfigError m_error;
    bool m_cancelled{};
};
