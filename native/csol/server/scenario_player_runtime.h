// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_session.h"
#include <utility>

// Local dedicated-server authority. These fields are owned state, not guesses
// based on a model name, current weapon, team or client-provided message.
struct CSOLScenarioPlayerProfile
{
    csol::scenario::EquipmentLoadout equipment;
    std::uint8_t characterFeature{};
    bool elevenLevelLimit{}, moneyExtractor{};
    bool weaponEffectsBlocked21F2{}, weaponEffectsBlocked34F0{};
    std::vector<int> targetFeature3Categories;
};

struct CSOLScenarioPlayerRuntimeState
{
    EHANDLE player;
    std::uint64_t generation{}, profileRevision{}, appliedRevision{};
    int classId{-1};
    CSOLScenarioPlayerProfile profile;
    // Original service event 0x26 is retained locally until its consumer
    // acknowledges it. Recording it does not grant a commercial reward.
    bool capEventPending{};
    std::uint64_t capEventSequence{};
    bool preparing{};
};

class CCSOLScenarioPlayerRuntime final : public std::enable_shared_from_this<CCSOLScenarioPlayerRuntime>
{
public:
    explicit CCSOLScenarioPlayerRuntime(std::shared_ptr<CCSOLScenarioSession> session)
        : m_session(std::move(session)) {}
    bool Activate();
    void Invalidate();
    bool Prepare(CBasePlayer& player);
    void Remove(const EHANDLE& player);
    // Trusted server code supplies inventory/ability state. No client command
    // calls this setter. A new local player starts with an actual empty profile.
    bool SetProfile(CBasePlayer& player, CSOLScenarioPlayerProfile profile);
    std::shared_ptr<const CSOLScenarioPlayerRuntimeState> Find(const EHANDLE& player) const;
    bool AcknowledgeCapEvent(const EHANDLE& player, std::uint64_t generation, std::uint64_t sequence);
    bool Active() const;
    const csol::scenario::ConfigError& Error() const { return m_error; }

private:
    std::shared_ptr<CSOLScenarioPlayerRuntimeState> Resolve(const EHANDLE& player) const;
    std::shared_ptr<CSOLScenarioPlayerRuntimeState> Ensure(CBasePlayer& player);
    bool RecordCapEvent(const EHANDLE& player);
    bool Fail(const char* reason);

    std::shared_ptr<CCSOLScenarioSession> m_session;
    std::array<std::shared_ptr<CSOLScenarioPlayerRuntimeState>, 33> m_players{};
    std::uint64_t m_generation{}, m_eventSequence{}, m_lifecycle{};
    csol::scenario::ConfigError m_error;
    bool m_active{};
};
