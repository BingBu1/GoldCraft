// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_death_notice.h"
#include "scenario_death_rules.h"
#include "scenario_death_targets.h"
#include "scenario_monster_models.h"

class CCSOLScenarioPlayers;

// A stack-owned transaction. Services must call Current again after any engine
// or plugin callback, before using the monster or resolving player handles.
// The owner/map/dispatcher outlive the call; reset the dispatcher BEFORE the
// engine releases an old map's edict array or starts a replacement round.
class CSOLMonsterDeathCall
{
public:
    CSOLMonsterDeathCall(CCSOLScenarioMap& map, CCSOLMonsterDeathRequests& requests,
        CCSOLScenarioMonster& monster, const CSOLMonsterDeathArguments& arguments);
    bool SameSession() const;
    bool BelongsTo(const CCSOLScenarioMap& map) const { return &m_map == &map; }
    CCSOLScenarioMonster* Current() const;
    CBaseEntity* Inflictor() const;
    CBaseEntity* Attacker() const;
    // A disconnected/replaced player must not receive the remaining player
    // tail. The already committed monster death still runs effects, targets,
    // death rules, Golden cleanup and PostDeath with no attacker.
    void RevokeAttacker() { m_arguments.attacker = static_cast<CBaseEntity*>(nullptr); }
    const CSOLMonsterDeathContext& Context() const { return m_arguments.context; }
    bool SuppressSound() const { return m_arguments.suppressSound; }

private:
    friend class CCSOLMonsterDeathOwner;
    CCSOLScenarioMap& m_map;
    CCSOLMonsterDeathRequests& m_requests;
    std::uint64_t m_mapGeneration{}, m_roundGeneration{}, m_requestGeneration{};
    std::shared_ptr<const CSOLScenarioEntityAccess> m_access;
    CCSOLMonsterDeathStages m_stages;
    CSOLMonsterDeathArguments m_arguments;
};

struct CSOLMonsterDeathParameters
{
    std::int32_t bonusMoney{};
    std::int16_t slotSkill{};
    bool shortDeath{};
};

// Concrete ordinary/Boss behaviors provide these callbacks. No default success
// methods hide missing frozen effects or derived behavior. Parameters reads
// only the factory's attribute-stage state and invokes no engine callbacks.
class CCSOLMonsterDeathBehavior : public CCSOLMonsterBehavior
{
public:
    virtual CSOLMonsterDeathParameters DeathParameters() const = 0;
    virtual bool BeforeDeath(CSOLMonsterDeathCall& call, csol::scenario::ConfigError& error) = 0;
    virtual bool SelectDeathSequence(CSOLMonsterDeathCall& call, int& sequence,
        csol::scenario::ConfigError& error) = 0;
    virtual bool AdjustDeathDuration(CSOLMonsterDeathCall& call, float& seconds,
        csol::scenario::ConfigError& error) = 0;
    virtual bool PostDeath(CSOLMonsterDeathCall& call, csol::scenario::ConfigError& error) = 0;
};

// Required game-rule services, intentionally abstract until their real owners
// are installed. Local kill money and opaque external drop rewards are separate
// operations. Implementations must preserve the call's generation checks even
// within a service that itself performs more than one external callback.
class CCSOLMonsterDeathServices
{
public:
    virtual ~CCSOLMonsterDeathServices() = default;
    // Resolve the active Session registry, not a null pointer captured before
    // Session activation. This accessor does not invoke engine callbacks.
    virtual std::shared_ptr<CCSOLScenarioPlayers> Players() const = 0;
    // Authoritative item353 flag (player+20AE), independent of class/addon19.
    virtual bool MoneyExtractor(CSOLMonsterDeathCall& call, const EHANDLE& player,
        bool& enabled, csol::scenario::ConfigError& error) = 0;
    // The original room query runs only when the extractor flag is set.
    virtual bool RoomOption(CSOLMonsterDeathCall& call, int& option,
        csol::scenario::ConfigError& error) = 0;
    virtual bool RulesDeath(CSOLMonsterDeathCall& call, csol::scenario::ConfigError& error) = 0;
    // The owner has committed the manager's team counter. Continue with
    // root character/current-weapon effects -> clearpoint -> frags/message
    // -> attribution -> drops -> counters -> attacker notification.
    // Null attackers never enter this service.
    virtual bool PlayerKilledMonster(CSOLMonsterDeathCall& call,
        csol::scenario::ConfigError& error) = 0;
    // Even without an attacker, restore the tracked original type and erase
    // its Golden record. A missing player skips only the reward/announcement.
    virtual bool GoldenDeath(CSOLMonsterDeathCall& call, csol::scenario::ConfigError& error) = 0;
};

// Concrete synchronous mode15/SubType1 execution order. The factory and rules
// supply actual behaviors/services before gameplay can be enabled. Keep this
// owner alive until every active callback has unwound, including map resets.
class CCSOLMonsterDeathOwner final : public CCSOLMonsterDeathExecutor
{
public:
    CCSOLMonsterDeathOwner(CCSOLScenarioMap& map, CCSOLMonsterPool& pool,
        CCSOLMonsterModels& models, const CCSOLScenarioMessages& messages,
        CCSOLMonsterDeathServices& services, int mapId);
    bool Reset();
    // Ending batches and death-table recursion share the map's live filter.
    CSOLMonsterDrainResult Drain(std::int32_t limit, bool includeBosses);
    CCSOLMonsterDeathRequests& Requests() { return m_requests; }
    const csol::scenario::ConfigError& Error() const { return m_error; }

private:
    // Enter through Requests so deferred and recursively repeated deaths share
    // the same gates. This remains callable by the dispatcher via its base.
    bool ExecuteDeath(CCSOLScenarioMonster& monster,
        const CSOLMonsterDeathArguments& arguments) override;
    bool AwardKillMoney(CSOLMonsterDeathCall& call, std::int32_t baseMoney,
        csol::scenario::ConfigError& error);
    CCSOLScenarioMap& m_map;
    CCSOLMonsterPool& m_pool;
    CCSOLMonsterModels& m_models;
    CCSOLMonsterDeathServices& m_services;
    int m_mapId{};
    CCSOLMonsterDeathRequests m_requests;
    CCSOLScenarioDeathNotice m_notice;
    CCSOLScenarioDeathTargets m_targets;
    CCSOLScenarioDeathRules m_rules;
    csol::scenario::ConfigError m_error;
};
