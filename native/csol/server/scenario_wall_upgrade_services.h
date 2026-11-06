// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_wall_upgrade.h"
#include <span>
#include <string>
#include <vector>

class CCSOLScenarioRuntime;
struct CSOLWallUpgradeRecord
{
    std::uint64_t sequence{}, mapGeneration{}, roundGeneration{};
    int playerIndex{}, userid{}, event{};
    std::string playerName;
    // A retained local request. ShutdownSnapshot persists it; this is never
    // acknowledgement from CSO's external SharedData service.
};

class CCSOLScenarioWallUpgradeServices final : public CCSOLWallUpgradeServices
{
public:
    explicit CCSOLScenarioWallUpgradeServices(std::weak_ptr<CCSOLScenarioRuntime> runtime);
    bool Current() const override;
    bool PlayerCurrent(const EHANDLE& player) const override;
    bool ReadAccount(const EHANDLE& player, int& account) override;
    bool ReadEligibility(const EHANDLE& player, std::int16_t& ctWins, std::uint8_t& participation) override;
    bool WriteAccount(const EHANDLE& player, int account) override;
    bool LevelChanged(std::int8_t level) override;
    bool PublishState(bool enabled, std::int8_t level) override;
    bool RecordServiceEvent(const EHANDLE& player, int event) override;
    bool PublishUpgrader(const EHANDLE& player) override;
    bool PublishDenied(const EHANDLE& player, const char* token) override;
    bool ShowIndicator(const EHANDLE& machine) override;
    bool RemoveIndicator(const EHANDLE& machine) override;
    void Failed(const char* reason) override;
    std::span<const CSOLWallUpgradeRecord> Records() const { return m_records; }
private:
    std::shared_ptr<CCSOLScenarioRuntime> Pin() const;
    bool Indicator(const EHANDLE& machine, bool remove);
    std::weak_ptr<CCSOLScenarioRuntime> m_runtime;
    std::shared_ptr<const CSOLScenarioEntityAccess> m_access;
    std::vector<CSOLWallUpgradeRecord> m_records;
    std::uint64_t m_nextRecord{};
};
