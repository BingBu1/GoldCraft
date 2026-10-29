// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_death_owner.h"
#include "../scenario/golden.hpp"
#include <optional>

class CCSOLScenarioRuntime;
class CSOLMonsterDamageCall;

// Local shutdown handoff, not official backend delivery. Writes one immutable
// JSONL snapshot; retries reuse directory and require identical existing bytes.
// Called only after SDK operations and registered private objects have drained.
bool CSOL_ArchiveScenarioShutdown(const CCSOLScenarioRuntime& runtime,
    const std::string& root, std::string& directory, std::string& error);

// Local authority fields, not a fabricated official player/account ID. The
// enclosing service is owned by one Runtime; generations cannot cross owners.
struct CSOLScenarioDeathRecord
{
    enum class Kind { Kill, Frags, Event20, Ribbons };
    Kind kind{};
    std::uint64_t sequence{}, death{}, mapGeneration{}, roundGeneration{}, playerGeneration{};
    std::int32_t source{}, item{};
    std::uint16_t flags{}, wireItem{};
    std::int32_t attackerTeam{}, monsterTeam{}, frags{};
    std::array<std::uint16_t, 2> event20{};
    std::array<std::uint8_t, 6> ribbons{};
    std::size_t ribbonCount{};
    // This journal is local and has not been submitted to an official backend.
};

// Concrete mode15/subtype1 death consumer. Factory/AI and wave composition
// remain separate requirements for enabling combat; installing this owner
// alone must not turn a Selection session into Combat.
class CCSOLScenarioDeathServices final : public CCSOLMonsterDeathServices
{
public:
    explicit CCSOLScenarioDeathServices(std::weak_ptr<CCSOLScenarioRuntime> runtime)
        : m_runtime(std::move(runtime)) {}
    bool Precache(csol::scenario::ResourceFileApi files, csol::scenario::ResourceError& error);
    std::shared_ptr<CCSOLScenarioPlayers> Players() const override;
    bool MoneyExtractor(CSOLMonsterDeathCall& call, const EHANDLE& player,
        bool& enabled, csol::scenario::ConfigError& error) override;
    bool RoomOption(CSOLMonsterDeathCall& call, int& option,
        csol::scenario::ConfigError& error) override;
    bool RulesDeath(CSOLMonsterDeathCall& call, csol::scenario::ConfigError& error) override;
    bool PlayerKilledMonster(CSOLMonsterDeathCall& call, csol::scenario::ConfigError& error) override;
    bool GoldenDeath(CSOLMonsterDeathCall& call, csol::scenario::ConfigError& error) override;
    std::span<const CSOLScenarioDeathRecord> Records() const { return m_records; }
    // Acknowledge only after a local consumer has actually saved this prefix.
    // Does not assert external submission, ribbon ownership or reward credit.
    bool Acknowledge(std::uint64_t throughSequence);
    const csol::scenario::GoldenResources& Golden() const { return m_golden; }
    std::optional<int> CachedMapId() const { return m_cachedMapId; }
    // Same concrete Golden owner as death handling, called on every creature
    // hit before active-skill/statistic consumers. Empty must be loaded state.
    bool GoldenHit(CSOLMonsterDamageCall& call, csol::scenario::ConfigError& error);

private:
    std::shared_ptr<CCSOLScenarioRuntime> Pin(CSOLMonsterDeathCall& call) const;
    bool Append(CSOLScenarioDeathRecord record, csol::scenario::ConfigError& error);
    std::weak_ptr<CCSOLScenarioRuntime> m_runtime;
    csol::scenario::GoldenResources m_golden;
    std::optional<int> m_cachedMapId;
    std::uint64_t m_recordSequence{}, m_deathSequence{};
    std::vector<CSOLScenarioDeathRecord> m_records;
};
