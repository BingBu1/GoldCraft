// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_monster_position.h"
#include "scenario_messages.h"
#include "../scenario/boss_skills.hpp"

struct CSOLBossControlState
{
    std::shared_ptr<CSOLMonsterSpawnState> spawn;
    const CCSOLMonsterBehavior* behavior{};
    std::uint64_t lease{};
    EHANDLE target;
    std::optional<std::size_t> row;
    CSOLMonsterPositionState position;
    csol::scenario::BossSkillClocks clocks;
    csol::scenario::BossSkillTimer effect, path, avoidance, movement;
    csol::scenario::BossSkillTimer knownGoodClock{0.0f, -1.0f, -1.0f};
    Vector knownGood{}, previousKnownGood{};
    float deltaX{}, deltaY{}, motionRemainder{}, launchOriginZ{-999999.0f}, solidAfter{};
    int repeatCount{};
    bool knownGoodValid{}, previousKnownGoodValid{}, busy{};
};

class CCSOLBossControlServices : public CCSOLMonsterSpawnOwner
{
public:
    // Ver2 v354 is conditional on the real type/skill permission directory.
    // A provider must resolve that directory and emit its real effect; do not
    // supply an unconditional success/no-op to make an incomplete Boss spawn.
    virtual bool RulesSkillStarted(CSOLMonsterSpawnCall& call,
        csol::scenario::ConfigError& error) = 0;
    virtual const CCSOLScenarioMessages& BossMessages() const = 0;
};

enum class CSOLBossBeginResult { Waiting, Started, Cancelled, Failed };

// Common SK control used by the independent Oberon behavior. This is not a
// substitute for its Think, locomotion, damage or derived skill effects. The
// factory must install a complete behavior/provider before using this owner.
class CCSOLMonsterBossControl : public std::enable_shared_from_this<CCSOLMonsterBossControl>
{
public:
    // The behavior owns this with shared_ptr. Each entry retains the control
    // through engine hooks which may detach the containing behavior/state.
    CCSOLMonsterBossControl(std::shared_ptr<csol::scenario::BossSkills> skills,
        std::weak_ptr<CCSOLBossControlServices> services)
        : m_skills(std::move(skills)), m_services(std::move(services)) {}
    bool Reset(CSOLMonsterSpawnCall& call, CSOLMonsterSpawnState& spawn,
        csol::scenario::ConfigError& error);
    std::shared_ptr<CSOLBossControlState> State() const { return m_state; }
    // Samples each row's clock, then each live player via INDEXENT/IsAlive.
    // Captures handles for candidates so callback-driven slot reuse cannot
    // silently turn a selected old player into a new client.
    bool Select(CSOLMonsterSpawnCall& call, csol::scenario::ConfigError& error);
    // Complete the common begin stages and rules v354 first. Only Started
    // authorizes the caller's immediate Oberon type5/6/7/9 extension.
    CSOLBossBeginResult TryBegin(CSOLMonsterSpawnCall& call, csol::scenario::ConfigError& error);
    // interruptState is the behavior-owned original host+1322 value. All
    // nonzero causes enter this stage, but only -1 releases the current row.
    bool Interrupt(CSOLMonsterSpawnCall& call, std::int16_t& interruptState,
        csol::scenario::ConfigError& error);
    bool Finish(CSOLMonsterSpawnCall& call, csol::scenario::ConfigError& error);

private:
    using StatePtr = std::shared_ptr<CSOLBossControlState>;
    CCSOLScenarioMonster* Current(CSOLMonsterSpawnCall& call, const StatePtr& state) const;
    bool ResetMotion(CSOLMonsterSpawnCall& call, const StatePtr& state);
    std::shared_ptr<csol::scenario::BossSkills> m_skills;
    std::weak_ptr<CCSOLBossControlServices> m_services;
    StatePtr m_state;
};
