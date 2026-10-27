// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "func_break.h"
#include "ehandle.h"
#include "../scenario/config.hpp"
#include <cstdint>
#include <memory>
#include <vector>

class CCSOLScenarioBreakable;

// A rules-owned context, invalidated before old map edicts are released.
// Scaling uses authoritative upgrade/character state; rewards must consume
// the scaled input before CBreakable applies material damage or overkill.
class CCSOLScenarioWallRules
{
public:
    virtual ~CCSOLScenarioWallRules() = default;
    void Invalidate() { ++m_generation; }
    std::uint64_t Generation() const { return m_generation; }
    virtual bool PlayerDamage(const EHANDLE& player, float input, float ratio, float& output) = 0;
    virtual bool MonsterDamage(float input, float ratio, float& output) = 0;
    virtual bool CreditPlayerDamage(const EHANDLE& player, float scaledDamage) = 0;
private:
    std::uint64_t m_generation{};
};

class CCSOLScenarioBreakable final : public CBreakable
{
public:
    void Spawn() override;
    void Restart() override;
    void KeyValue(KeyValueData* data) override;
    int Classify() override;
    int ObjectCaps() override { return CBreakable::ObjectCaps() | FCAP_MUST_RESET; }
    BOOL TakeDamage(entvars_t* inflictor, entvars_t* attacker, float damage, int bits) override;
    void Use(CBaseEntity* activator, CBaseEntity* caller, USE_TYPE type, float value) override;
    int Save(CSave& save) override;
    int Restore(CRestore& restore) override;
    void OnDestroy() override;
    void EXPORT MonsterTouch(CBaseEntity* other);
    bool ValidConfiguration() const { return m_valid; }
    void BindRules(std::shared_ptr<CCSOLScenarioWallRules> rules);
    static TYPEDESCRIPTION m_SaveData[];
private:
    void ConfigureTouch();
    friend class CSOLScenarioWallCall;
    float m_playerRatio{}, m_monsterRatio{};
    string_t m_master{};
    std::shared_ptr<CCSOLScenarioWallRules> m_rules;
    std::shared_ptr<std::uint64_t> m_lifetime{std::make_shared<std::uint64_t>()};
    std::uint64_t m_serial{};
    bool m_valid{true};
    bool m_damaging{};
};

// Session collection binds the same actual rules to all map zbsbreaks. It
// participates in native RestartRound via FCAP_MUST_RESET.
class CCSOLScenarioWalls
{
public:
    bool Collect(csol::scenario::ConfigError& error);
    bool BindRules(std::shared_ptr<CCSOLScenarioWallRules> rules);
    void CancelPending();
    void Reset();
    std::size_t Count() const { return m_walls.size(); }
private:
    std::vector<EntityHandle<CCSOLScenarioBreakable>> m_walls;
    std::shared_ptr<CCSOLScenarioWallRules> m_rules;
    std::uint64_t m_generation{};
    bool m_busy{};
};
