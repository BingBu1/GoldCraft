// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "func_break.h"
#include "ehandle.h"
#include "scenario_entity_access.h"
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
    void BindEntityAccess(std::shared_ptr<const CSOLScenarioEntityAccess> access) { m_access = std::move(access); }
    bool EntityAccessAllowed() const { return m_access && m_access->Allowed(); }
    std::shared_ptr<const CSOLScenarioEntityAccess> EntityAccess() const { return m_access; }
    virtual bool PlayerDamage(const EHANDLE& player, float input, float ratio, float& output) = 0;
    virtual bool MonsterDamage(float input, float ratio, float& output) = 0;
    virtual bool CreditPlayerDamage(const EHANDLE& player, float scaledDamage) = 0;
private:
    std::uint64_t m_generation{};
    std::shared_ptr<const CSOLScenarioEntityAccess> m_access{std::make_shared<CSOLScenarioEntityAccess>()};
};

class CCSOLScenarioBreakable : public CBreakable
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
    friend class CCSOLScenarioTarget;
    float m_playerRatio{}, m_monsterRatio{};
    string_t m_master{};
    std::shared_ptr<CCSOLScenarioWallRules> m_rules;
    std::shared_ptr<std::uint64_t> m_lifetime{std::make_shared<std::uint64_t>()};
    std::uint64_t m_serial{};
    bool m_valid{true};
    bool m_damaging{};
};

// CZBSTarget has its own active byte. Clearpoint protection changes damage
// permission without changing that byte; Use always toggles it, even for
// USE_ON/USE_OFF. Target HUD and post-break failure handling are separate
// unfinished consumers and do not become available by registering this class.
class CCSOLScenarioTarget final : public CCSOLScenarioBreakable
{
public:
    void Spawn() override;
    void Restart() override;
    void KeyValue(KeyValueData* data) override;
    void Use(CBaseEntity* activator, CBaseEntity* caller, USE_TYPE type, float value) override;
    BOOL TakeDamage(entvars_t* inflictor, entvars_t* attacker, float damage, int bits) override;
    int Save(CSave& save) override;
    int Restore(CRestore& restore) override;
    bool Active() const { return m_active != 0; }
    void SetClearPointProtection(bool enabled);
    static TYPEDESCRIPTION m_SaveData[];
private:
    // FIELD_CHARACTER matches this one-byte destination member. The original
    // save descriptor's type numbers are not a portable serialization ABI.
    char m_active{};
    std::int8_t m_icon{};
    Vector m_iconPosition{};
    bool m_iconPublished{};
};

// Session collection binds the same actual rules to all map zbsbreaks. It
// participates in native RestartRound via FCAP_MUST_RESET.
class CCSOLScenarioWalls
{
public:
    void BindEntityAccess(std::shared_ptr<const CSOLScenarioEntityAccess> access) { m_access = std::move(access); }
    bool Collect(csol::scenario::ConfigError& error);
    bool BindRules(std::shared_ptr<CCSOLScenarioWallRules> rules);
    void CancelPending();
    void Reset();
    std::size_t Count() const { return m_walls.size(); }
    bool Busy() const { return m_busy; }
private:
    std::vector<EntityHandle<CCSOLScenarioBreakable>> m_walls;
    std::shared_ptr<CCSOLScenarioWallRules> m_rules;
    std::uint64_t m_generation{};
    bool m_busy{};
    std::shared_ptr<const CSOLScenarioEntityAccess> m_access{std::make_shared<CSOLScenarioEntityAccess>()};
};
