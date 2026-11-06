// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cbase.h"
#include "ehandle.h"
#include "scenario_entity_access.h"
#include "../wall_upgrade.hpp"
#include <memory>

// The rules retain the actual player generation, CT score and first-join /
// BeginRound participation byte. These operations are required services;
// missing message, account or external-event owners cannot acknowledge them.
class CCSOLWallUpgradeServices
{
public:
    virtual ~CCSOLWallUpgradeServices() = default;
    virtual bool Current() const = 0;
    virtual bool PlayerCurrent(const EHANDLE& player) const = 0;
    virtual bool ReadAccount(const EHANDLE& player, int& account) = 0;
    virtual bool ReadEligibility(const EHANDLE& player, std::int16_t& ctWins,
        std::uint8_t& participation) = 0;
    virtual bool WriteAccount(const EHANDLE& player, int account) = 0;
    // Set the existing real MonsterDamage consumer before publishing state.
    virtual bool LevelChanged(std::int8_t level) = 0;
    virtual bool PublishState(bool enabled, std::int8_t level) = 0;
    // Original external event51 has no buffer and no local acknowledgement.
    // A host ledger must retain the request; it is not a chat announcement.
    virtual bool RecordServiceEvent(const EHANDLE& player, int event) = 0;
    virtual bool PublishUpgrader(const EHANDLE& player) = 0;
    virtual bool PublishDenied(const EHANDLE& player, const char* token) = 0;
    // Original valid-client traversal, arbitrary-entity attachment, and
    // actual engine message ID belong to the rules' message owner.
    virtual bool ShowIndicator(const EHANDLE& machine) = 0;
    virtual bool RemoveIndicator(const EHANDLE& machine) = 0;
    virtual void Failed(const char* reason) = 0;
};

class CCSOLScenarioWallUpgrade : public std::enable_shared_from_this<CCSOLScenarioWallUpgrade>
{
public:
    enum class Result { Ignored, Denied, Upgraded, Cancelled, Failed };
    CCSOLScenarioWallUpgrade(bool enabled, std::shared_ptr<CCSOLWallUpgradeServices> services,
        std::shared_ptr<const CSOLScenarioEntityAccess> access);
    bool Current() const;
    void Invalidate() { m_valid = false; ++m_generation; }
    bool Enabled() const { return m_enabled; }
    std::int8_t Level() const { return m_level; }
    const std::shared_ptr<const CSOLScenarioEntityAccess>& EntityAccess() const { return m_access; }
    Result Use(const EHANDLE& player);
    bool ShowIndicator(const EHANDLE& machine);
    bool RemoveIndicator(const EHANDLE& machine);
private:
    std::shared_ptr<CCSOLWallUpgradeServices> m_services;
    std::shared_ptr<const CSOLScenarioEntityAccess> m_access;
    std::uint64_t m_generation{};
    std::int8_t m_level{csol::wall_upgrade::InitialLevel};
    bool m_enabled{}, m_valid{true}, m_busy{};
};

class CCSOLScenarioWallUpgradeMachine final : public CBaseAnimating
{
public:
    void Spawn() override;
    void Precache() override;
    int ObjectCaps() override { return FCAP_ACROSS_TRANSITION | FCAP_IMPULSE_USE; }
    void OnDestroy() override;
    void EXPORT MachineThink();
    void EXPORT UseWallUpgrade(CBaseEntity* activator, CBaseEntity* caller, USE_TYPE type, float value);
    bool Bind(std::shared_ptr<CCSOLScenarioWallUpgrade> state);
    bool StatusLevel(int& level) const;
    bool Spawned() const { return m_spawned; }
private:
    friend class CSOLWallMachineCall;
    std::shared_ptr<CCSOLScenarioWallUpgrade> m_state;
    std::shared_ptr<const CSOLScenarioEntityAccess> m_access;
    std::shared_ptr<std::uint64_t> m_lifetime{std::make_shared<std::uint64_t>()};
    bool m_spawned{};
};
