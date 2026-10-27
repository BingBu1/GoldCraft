// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_monster_rewards.h"
#include "scenario_equipment.h"
#include "scenario_weapon_state.h"
#include "../scenario/monster_damage.hpp"

// A hit owns its context and entity handles. Rules must re-resolve Current()
// after callbacks before using either the victim or its attacker/inflictor.
class CSOLMonsterDamageCall
{
public:
    CSOLMonsterDamageCall(CSOLMonsterSpawnCall& spawn, EHANDLE inflictor,
        EHANDLE attacker, float damage, int bits, CSOLMonsterDeathContext context,
        const bool& ownerActive, bool (*ownerValid)(const void*), const void* ownerContext);
    CCSOLScenarioMonster* Current() const;
    CBaseEntity* Inflictor() const;
    CBaseEntity* Attacker() const;
    CBasePlayer* Player() const;
    CSOLMonsterSpawnCall& Spawn() const { return m_spawn; }
    const EHANDLE& InflictorHandle() const { return m_inflictor; }
    const EHANDLE& AttackerHandle() const { return m_attacker; }
    float OriginalDamage() const { return m_originalDamage; }
    float damage{};
    int bits{};
    CSOLMonsterDeathContext context;
private:
    CSOLMonsterSpawnCall& m_spawn;
    EHANDLE m_inflictor, m_attacker;
    const CCSOLMonsterBehavior* m_behavior{};
    float m_originalDamage{};
    bool m_hadInflictor{}, m_hadAttacker{};
    const bool& m_ownerActive;
    bool (*m_ownerValid)(const void*);
    const void* m_ownerContext{};
};

struct CSOLMonsterHitAccount
{
    bool moneyExtractor{};
    int roomOption{};
};

// These are the remaining actual mode15 rule/weapon consumers, in their
// original order. They are required, not optional successful no-op callbacks.
// Damage/armor, hit rewards, health, death dispatch and flinch belong below.
class CCSOLMonsterDamageServices
{
public:
    virtual ~CCSOLMonsterDamageServices() = default;
    virtual const bool& DamageSuppressed() const = 0;
    // Original player byte +21F2. Its owner must provide actual state; do not
    // guess a zombie/bot/death meaning. The item-catalog prefix is concrete.
    virtual bool WeaponEffectsBlocked21F2(CSOLMonsterDamageCall& call) const = 0;
    virtual bool WeaponEffectsBlocked34F0(CSOLMonsterDamageCall& call) const = 0;
    // Remaining v1D8 addon consumer and weapon-damage statistics. Both this
    // and the preceding stages run before the friendly-fire rejection.
    virtual bool RulesAfterAttackDamage(CSOLMonsterDamageCall& call,
        csol::scenario::ConfigError& error) = 0;
    // Remaining 11386504..11386B1A effects after the concrete part sum.
    // Do not repeat the target+90 reset or kinds1/6/7/14 here. Also resolve
    // server-owned item353 and room state, independently of character class.
    virtual bool BeforeHitReward(CSOLMonsterDamageCall& call, CSOLMonsterHitAccount& account,
        csol::scenario::ConfigError& error) = 0;
    // Weapon modifiers and 1143DD80 notification. The latter's float return
    // is discarded in CSO and must not replace call.damage.
    virtual bool AfterHitReward(CSOLMonsterDamageCall& call,
        csol::scenario::ConfigError& error) = 0;
    // Rules v1DC, player statistics/status, appearance health-summary tail.
    // This stage sees the clamped damage after the actual health subtraction.
    virtual bool AfterHealthDamage(CSOLMonsterDamageCall& call,
        csol::scenario::ConfigError& error) = 0;
};

class CCSOLOrdinaryMonsterDamage final
{
public:
    CCSOLOrdinaryMonsterDamage(CCSOLScenarioMap& map, CCSOLMonsterDeathRequests& deaths,
        std::shared_ptr<CCSOLMonsterHitRewards> rewards,
        std::shared_ptr<CCSOLScenarioPlayers> players,
        std::shared_ptr<CCSOLScenarioEquipment> equipment,
        std::shared_ptr<CCSOLScenarioWeaponState> weapons,
        std::shared_ptr<CCSOLMonsterDamageServices> services)
        : m_map(map), m_deaths(deaths), m_rewards(std::move(rewards)),
          m_players(std::move(players)), m_equipment(std::move(equipment)),
          m_weapons(std::move(weapons)), m_services(std::move(services)) {}
    void Invalidate() { m_valid = false; }
    BOOL Apply(CSOLMonsterSpawnCall& spawn, entvars_t* inflictor, entvars_t* attacker,
        float damage, int bits, csol::scenario::ConfigError& error,
        bool (*ownerValid)(const void*), const void* ownerContext,
        CSOLMonsterDeathContext context = {});
private:
    CCSOLScenarioMap& m_map;
    CCSOLMonsterDeathRequests& m_deaths;
    std::shared_ptr<CCSOLMonsterHitRewards> m_rewards;
    std::shared_ptr<CCSOLScenarioPlayers> m_players;
    std::shared_ptr<CCSOLScenarioEquipment> m_equipment;
    std::shared_ptr<CCSOLScenarioWeaponState> m_weapons;
    // Rules/runtime owns this damage consumer and the referenced map/deaths.
    // Pin the provider before touching those references, without owning it
    // permanently through runtime -> pool -> behavior -> damage -> runtime.
    std::weak_ptr<CCSOLMonsterDamageServices> m_services;
    bool m_valid{true};
};
