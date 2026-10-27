// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cbase.h"
#include "ehandle.h"
#include "scenario_monster_animation.h"
#include "scenario_monster_speed.h"
#include "../scenario/monster_types.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <string>

class CCSOLMonsterPool;
class CCSOLScenarioMonster;
class CCSOLMonsterModels;
class CCSOLMonsterFactory;

// Original host fields survive behavior replacement and pool reuse. Only the
// documented spawn stages reset them; these are not AI-context timers.
struct CSOLMonsterHostState
{
    float frozenRemaining{};
    float moveAfter{};
    float enhancedUntil{-1.0f};
    std::int16_t activeSkill{};
    int lastHitgroup{};
    int targetPolicy{-1}; // original host1368, retained on ordinary reuse
    bool navigationExpired{};
    bool flinchEnabled{true}; // mode15 host+1374, set from the actual model path
    bool knockbackBlocked{}; // original host+1F8, retained across pool reuse
    int damageScaleCount{}; // original host+12C4, initially zero
    float damageScale{1.0f}; // original host+12C8
    float nonlethalDamage{}; // original host+12C0, shares damageScaleCount
    float statusUpdateAt{}; // original host+12CC, independent one-second gate
    int damageEffect{}; // original host+90; cleared by the player part stage
};

// Implementations belong to the actual monster/Boss factory. Pool ownership
// must not silently supply generic movement or damage as a substitute for it.
class CCSOLMonsterBehavior
{
public:
    virtual ~CCSOLMonsterBehavior() = default;
    virtual void Think(CCSOLScenarioMonster& monster) = 0;
    virtual BOOL TakeDamage(CCSOLScenarioMonster& monster, entvars_t* inflictor,
        entvars_t* attacker, float damage, int damageType) = 0;
    virtual void Killed(CCSOLScenarioMonster& monster, entvars_t* attacker, int gib) = 0;
};

class CCSOLScenarioMonster : public CBaseAnimating
{
public:
    void Precache() override;
    void Spawn() override;
    int Classify() override { return 17; }
    void TraceAttack(entvars_t* attacker, float damage, Vector direction,
        TraceResult* trace, int damageType) override;
    BOOL TakeDamage(entvars_t* inflictor, entvars_t* attacker, float damage, int damageType) override;
    void Killed(entvars_t* attacker, int gib) override;
    void OnDestroy() override;
    void EXPORT BehaviorThink();
    void EXPORT FreeThink();

    // A factory installs a real implementation after applying the model,
    // original appearance bounds and round attributes. This alone does not
    // enable damage, schedule AI or set animation: the factory owns that order.
    // A behavior may request pool death/return, but must not synchronously
    // replace itself or delete its own private entity from inside a callback.
    bool InstallBehavior(std::unique_ptr<CCSOLMonsterBehavior> behavior);
    CCSOLMonsterBehavior* Behavior() const { return m_behavior.get(); }
    // Pin only while executing a callback. Replacement or map shutdown may
    // detach the behavior, but cannot destroy the object still on its stack.
    std::shared_ptr<CCSOLMonsterBehavior> RetainBehavior() const { return m_behavior; }
    CCSOLMonsterPool* Pool() const { return m_pool; }
    std::size_t Slot() const { return m_slot; }
    int TypeId() const { return m_typeId; }
    int SpawnPointId() const { return m_spawnPoint; }
    const csol::scenario::MonsterType* Type() const { return m_type; }
    CCSOLMonsterAnimation& Animation() { return m_animation; }
    CSOLMonsterSpeedState& SpeedState() { return *m_speed; }
    CSOLMonsterHostState& HostState() { return m_host; }

private:
    friend class CCSOLMonsterPool;
    friend class CCSOLMonsterModels;
    friend class CCSOLMonsterFactory;
    CCSOLMonsterPool* m_pool{};
    std::size_t m_slot{};
    int m_typeId{};
    int m_spawnPoint{};
    const csol::scenario::MonsterType* m_type{};
    std::shared_ptr<CCSOLMonsterBehavior> m_behavior;
    CCSOLMonsterAnimation m_animation;
    // GoldSrc frees private data without running its C++ destructor. Reset
    // explicitly in OnDestroy; optional also permits normal test destruction.
    std::optional<CSOLMonsterSpeedState> m_speed{std::in_place};
    CSOLMonsterHostState m_host;
    bool m_behaviorReady{};
    bool m_destroying{};
};

enum class CSOLMonsterPoolState { Empty, Ready, Failed };

// Mode15/SubType1 pool, including zs_deadend. GameRules must own this longer
// than its entities, call Shutdown while their map is live, then ForgetMap.
// Accepted continuation does neither RestartRound nor Initialize.
class CCSOLMonsterPool
{
public:
    static constexpr std::size_t Capacity = 45;
    static constexpr std::array<int, 3> CategoryLimits{39, 4, 2};
    CCSOLMonsterPool() = default;
    ~CCSOLMonsterPool();
    CCSOLMonsterPool(const CCSOLMonsterPool&) = delete;
    CCSOLMonsterPool& operator=(const CCSOLMonsterPool&) = delete;

    bool Initialize();
    bool Shutdown();
    // Forget old-map handles without accessing the previous edict array.
    bool ForgetMap();
    bool RestartRound();
    // This is the low-level pool allocation AFTER point selection, type lookup
    // and attempted-spawn quota consumption. It never repeats initial Spawn.
    // The factory must finish attributes/behavior before enabling damage/Think.
    // The type table is borrowed and must outlive all slots until RestartRound
    // clears their type pointers, or Shutdown detaches the entities.
    CCSOLScenarioMonster* Acquire(const csol::scenario::MonsterType& type,
        int point, csol::scenario::SpawnPoints& points,
        std::span<const std::size_t> protectedSlots = {},
        const csol::scenario::MonsterType** previousType = nullptr);
    // Death animation selection/rewards belong to the behavior. This records
    // the original immediate non-solid state and delayed pool return.
    bool BeginDeath(CCSOLScenarioMonster& monster, float freeAt,
        csol::scenario::SpawnPoints& points);
    bool Free(CCSOLScenarioMonster& monster);
    // beforeDeath's frozen path returns the slot immediately but preserves
    // Think/Touch. Its captured death continues and installs the final Think.
    bool ParkForDeath(CCSOLScenarioMonster& monster);
    CCSOLScenarioMonster* At(std::size_t slot) const;
    bool HasSolidMonster(int type) const;
    bool Borrowed(const CCSOLScenarioMonster& monster) const;
    // Free retains this identity so an early pool return can finish its death
    // callbacks. A subsequent Acquire always has a different nonzero lease.
    // Zero means that this object has never been borrowed or is no longer ours.
    std::uint64_t Lease(const CCSOLScenarioMonster& monster) const;
    std::size_t ActiveCount() const { return m_active; }
    std::size_t AvailableCount() const { return m_idle.size(); }
    std::size_t CategoryCount(std::size_t priority) const;
    CSOLMonsterPoolState State() const { return m_state; }
    const std::string& Error() const { return m_error; }
    bool Busy() const { return m_busy; }
    // Engine private-data destruction happens before serial/free changes.
    void EntityDestroyed(CCSOLScenarioMonster& monster);

private:
    friend class CCSOLScenarioMonster;
    friend class CCSOLMonsterModels;
    struct Slot
    {
        EntityHandle<CCSOLScenarioMonster> entity;
        std::uint64_t lease{};
        bool borrowed{};
        std::uint8_t priority{};
    };
    bool Fail(const char* reason);
    bool Validate();
    bool Return(std::size_t slot, bool clearCallbacks = true);
    bool InstallBehavior(CCSOLScenarioMonster& monster, std::unique_ptr<CCSOLMonsterBehavior> behavior);
    void Park(CCSOLScenarioMonster& monster, bool clearCallbacks = true);
    void RemoveOwned();
    std::array<Slot, Capacity> m_slots{};
    std::array<std::size_t, 3> m_category{};
    std::deque<std::size_t> m_idle;
    std::size_t m_active{};
    std::size_t m_registered{};
    // Do not reset across Shutdown/Initialize or ForgetMap on this owner.
    std::uint64_t m_lastLease{};
    CSOLMonsterPoolState m_state{CSOLMonsterPoolState::Empty};
    std::string m_error;
    bool m_busy{};
};
