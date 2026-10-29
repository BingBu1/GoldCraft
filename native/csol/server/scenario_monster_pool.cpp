// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_monster_pool.h"
#include "scenario_monster_resources.h"
#include "scenario_monster_effects.h"
#include "game_rules_gate.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

LINK_ENTITY_TO_CLASS(monster_entity, CCSOLScenarioMonster, CCSEntity)

namespace
{
thread_local std::shared_ptr<const CSOLScenarioEntityAccess> spawningAccess;
struct SpawnAccessScope
{
    std::shared_ptr<const CSOLScenarioEntityAccess> previous;
    explicit SpawnAccessScope(std::shared_ptr<const CSOLScenarioEntityAccess> access)
        : previous(std::move(spawningAccess)) { spawningAccess = std::move(access); }
    ~SpawnAccessScope() { spawningAccess = std::move(previous); }
};
struct Operation
{
    bool& busy;
    explicit Operation(bool& value) : busy(value) { busy = true; }
    ~Operation() { busy = false; }
};
bool Live(CBaseEntity* entity)
{
    return entity && UTIL_IsValidEntity(entity->edict());
}
bool ClearPoolEffects(CCSOLMonsterPool& pool, CCSOLScenarioMonster& monster)
{
    struct Capture
    {
        CCSOLMonsterPool& pool;
        std::size_t slot;
        std::uint64_t lease;
        const CCSOLScenarioMonster* identity;
    } capture{pool, monster.Slot(), pool.Lease(monster), &monster};
    const CSOLMonsterEffectGuard guard{[](const void* value) -> CCSOLScenarioMonster*
    {
        const auto& scope = *static_cast<const Capture*>(value);
        if (scope.pool.State() != CSOLMonsterPoolState::Ready) return nullptr;
        auto* current = scope.pool.At(scope.slot);
        return current == scope.identity && current && scope.pool.Lease(*current) == scope.lease ? current : nullptr;
    }, &capture};
    auto effects = monster.DamageEffects();
    return effects && effects->ClearAll(guard);
}
}

void CCSOLScenarioMonster::Precache()
{
    PRECACHE_MODEL(CSOLScenarioInitialMonsterModel);
}

void CCSOLScenarioMonster::Spawn()
{
    // Pool initialization sets this before DispatchSpawn: m_pool is published
    // only after Spawn has returned, so it cannot guard this precache callback.
    const auto access = spawningAccess;
    if (!access || !access->Allowed()) return;
    EHANDLE self(this);
    Precache();
    if (!access->Allowed() || self.Get<CBaseEntity>() != this || !Live(self.Get<CBaseEntity>())) return;
    pev->movetype = MOVETYPE_STEP;
    pev->solid = SOLID_SLIDEBOX;
    // No default CBaseEntity damage/death path: it deletes the edict and does
    // not implement CSO armor/rewards. Activation requires a concrete behavior.
    pev->takedamage = DAMAGE_NO;
    pev->flags |= FL_MONSTER;
    pev->deadflag = DEAD_NO;
    pev->health = pev->max_health = 100;
    pev->gravity = 1;
    pev->view_ofs = Vector(0, 0, 12);
    pev->velocity = g_vecZero;
    m_speed.emplace();
    m_host = {};
    if (!m_damageEffects) m_damageEffects = std::make_shared<CCSOLMonsterEffects>();
    m_touchComponents.emplace();
    pev->skin = std::max(pev->skin, 0);
    SetThink(nullptr);
    SetTouch(nullptr);
    SET_MODEL(edict(), CSOLScenarioInitialMonsterModel);
}

bool CCSOLScenarioMonster::InstallBehavior(std::unique_ptr<CCSOLMonsterBehavior> behavior)
{
    return m_pool && m_pool->InstallBehavior(*this, std::move(behavior));
}

void CCSOLScenarioMonster::TraceAttack(entvars_t* attacker, float damage,
    Vector direction, TraceResult* trace, int damageType)
{
    CSOLScenarioSdkScope scope(CSOL_ScenarioOwnerRules(m_pool));
    if (scope.Scenario() && !scope.Current()) return;
    // CSO's base trace stage scales the trace hitgroup, then queues damage;
    // TakeDamage has its own later hitgroup/armor/reward stages. Applying it
    // immediately here would break shotgun aggregation and attacker credit.
    if (!trace || !m_pool || m_pool->Busy() || !m_pool->Borrowed(*this) ||
        !m_behaviorReady || !m_behavior || pev->takedamage == DAMAGE_NO ||
        !std::isfinite(damage)) return;
    const EHANDLE self(this);
    const auto lease = m_pool->Lease(*this);
    auto* pool = m_pool;
    const auto access = pool->EntityAccess();
    auto behavior = RetainBehavior();
    m_host.lastHitgroup = trace->iHitgroup;
    if (trace->pHit && trace->pHit->pvPrivateData)
    {
        const auto* hit = CBaseEntity::Instance(trace->pHit);
        if (!access->Allowed() || pool->EntityAccess() != access ||
            self.Get<CBaseEntity>() != this || m_pool != pool ||
            pool->Lease(*this) != lease || m_behavior != behavior) return;
        // Preserve the trace's hitgroup even when only the stored group is
        // cleared for a trace referring to a different private entity.
        if (hit && hit != this) m_host.lastHitgroup = HITGROUP_GENERIC;
    }
    switch (trace->iHitgroup)
    {
    case HITGROUP_HEAD: damage *= 3.0f; break;
    case HITGROUP_CHEST:
    case HITGROUP_STOMACH: damage = static_cast<float>(static_cast<double>(damage) * 1.5); break;
    case HITGROUP_LEFTLEG:
    case HITGROUP_RIGHTLEG: damage = static_cast<float>(static_cast<double>(damage) * 0.75); break;
    default: break;
    }
    if (!std::isfinite(damage)) return;
    AddMultiDamage(attacker, this, damage, damageType);
    // The copied MP's ordinary v17C returns false and its regular blood
    // helper is empty. Do not inject stock CS blood/TE9 packets: CSO's TE9
    // has a different wire layout. Special effect/context paths are separate.
}

BOOL CCSOLScenarioMonster::TakeDamage(entvars_t* inflictor, entvars_t* attacker,
    float damage, int damageType)
{
    CSOLScenarioSdkScope scope(CSOL_ScenarioOwnerRules(m_pool));
    if (scope.Scenario() && !scope.Current()) return FALSE;
    if (!m_pool || m_pool->Busy() || !m_pool->Borrowed(*this) || !m_behaviorReady || !m_behavior ||
        pev->takedamage == DAMAGE_NO)
        return FALSE;
    auto behavior = RetainBehavior();
    const auto context = CSOL_ConsumeMonsterDamageContext(*this, inflictor, attacker, damageType);
    return behavior->TakeDamageWithContext(*this, inflictor, attacker, damage, damageType, context);
}

void CCSOLScenarioMonster::Killed(entvars_t* attacker, int gib)
{
    CSOLScenarioSdkScope scope(CSOL_ScenarioOwnerRules(m_pool));
    if (scope.Scenario() && !scope.Current()) return;
    if (m_pool && !m_pool->Busy() && m_pool->Borrowed(*this) && m_behaviorReady && m_behavior &&
        pev->deadflag != DEAD_DEAD)
    {
        auto behavior = RetainBehavior();
        behavior->Killed(*this, attacker, gib);
    }
}

void CCSOLScenarioMonster::BehaviorThink()
{
    CSOLScenarioSdkScope scope(CSOL_ScenarioOwnerRules(m_pool));
    if (scope.Scenario() && !scope.Current()) return;
    if (m_pool && !m_pool->Busy() && m_pool->Borrowed(*this) && m_behaviorReady && m_behavior)
    {
        auto behavior = RetainBehavior();
        behavior->Think(*this);
    }
}

bool CCSOLScenarioMonster::SetTouchComponent(std::size_t index,
    std::shared_ptr<CCSOLMonsterTouchComponent> component)
{
    if (m_destroying || !m_touchComponents || index >= m_touchComponents->size()) return false;
    (*m_touchComponents)[index] = std::move(component);
    return true;
}

void CCSOLScenarioMonster::Touch(CBaseEntity* other)
{
    CSOLScenarioSdkScope scope(CSOL_ScenarioOwnerRules(m_pool));
    if (scope.Scenario() && !scope.Current()) return;
    // CMonster vEC=11387890 is a virtual override, independent of the stock
    // m_pfnTouch member. Ownership checks adapt it to the reusable SDK pool.
    if (!m_pool || m_pool->Busy() || !m_pool->Borrowed(*this) || !m_behaviorReady || !m_behavior)
        return;
    const EHANDLE self(this), target(other);
    auto* pool = m_pool;
    const auto access = pool->EntityAccess();
    const auto lease = pool->Lease(*this);
    auto behavior = RetainBehavior();
    const auto current = [&]()
    {
        return access->Allowed() && pool->EntityAccess() == access &&
            self.Get<CBaseEntity>() == this && m_pool == pool &&
            pool->Borrowed(*this) && pool->Lease(*this) == lease && m_behavior == behavior &&
            m_touchComponents && (!other || target.Get<CBaseEntity>());
    };
    behavior->Touch(*this, other);
    if (!current()) return;
    for (std::size_t i = 0; i < m_touchComponents->size(); ++i)
    {
        auto component = (*m_touchComponents)[i];
        if (component) component->Touch(*this, target.Get<CBaseEntity>());
        if (!current()) return;
    }
}

void CCSOLScenarioMonster::FreeThink()
{
    CSOLScenarioSdkScope scope(CSOL_ScenarioOwnerRules(m_pool));
    if (scope.Scenario() && !scope.Current()) return;
    if (m_pool) m_pool->Free(*this);
}

void CCSOLScenarioMonster::OnDestroy()
{
    CSOLScenarioSdkScope scope(CSOL_ScenarioOwnerRules(m_pool));
    if (m_destroying) return;
    m_destroying = true;
    auto* owner = m_pool;
    m_pool = nullptr; // Destructors may reenter; detach before releasing them.
    m_type = nullptr;
    m_behaviorReady = false;
    m_speed.reset();
    m_touchComponents.reset();
    auto effects = std::move(m_damageEffects);
    if (owner) owner->EntityDestroyed(*this);
    auto behavior = std::move(m_behavior);
    CBaseAnimating::OnDestroy();
    behavior.reset(); // Engine Mem_Free does not run the C++ destructor.
    effects.reset(); // Destruction only; map removal does not notify DOTs.
}

CCSOLMonsterPool::~CCSOLMonsterPool()
{
    Shutdown();
}

bool CCSOLMonsterPool::InstallBehavior(CCSOLScenarioMonster& monster,
    std::unique_ptr<CCSOLMonsterBehavior> behavior)
{
    if (m_busy || !behavior || !Borrowed(monster)) return false;
    Operation operation(m_busy);
    const auto index = monster.m_slot;
    monster.m_behaviorReady = false;
    auto previous = std::move(monster.m_behavior);
    previous.reset();
    auto* current = At(index);
    if (current != &monster || !current || !Borrowed(*current)) return false;
    current->m_behavior = std::move(behavior);
    current->m_behaviorReady = true;
    return true;
}

bool CCSOLMonsterPool::Fail(const char* reason)
{
    m_error = reason;
    m_state = CSOLMonsterPoolState::Failed;
    return false;
}

CCSOLScenarioMonster* CCSOLMonsterPool::At(std::size_t slot) const
{
    // FClassnameIs expands STRING and reads the engine's string table. Check
    // it before resolving this slot, including teardown and missing-service
    // failures reached indirectly through a captured death's Lease check.
    if (!EntityAccessAllowed() || !gpGlobals || !gpGlobals->pStringBase || slot >= m_slots.size()) return nullptr;
    auto* base = m_slots[slot].entity.Get<CBaseEntity>();
    if (!Live(base) || !FClassnameIs(base->pev, "monster_entity")) return nullptr;
    auto* entity = dynamic_cast<CCSOLScenarioMonster*>(base);
    return entity && entity->m_pool == this && entity->m_slot == slot ? entity : nullptr;
}

bool CCSOLMonsterPool::Borrowed(const CCSOLScenarioMonster& monster) const
{
    return EntityAccessAllowed() && m_state == CSOLMonsterPoolState::Ready && monster.m_slot < m_slots.size() &&
        At(monster.m_slot) == &monster && m_slots[monster.m_slot].borrowed;
}

std::uint64_t CCSOLMonsterPool::Lease(const CCSOLScenarioMonster& monster) const
{
    if (!EntityAccessAllowed() || m_state != CSOLMonsterPoolState::Ready || monster.m_slot >= m_slots.size() ||
        At(monster.m_slot) != &monster) return 0;
    return m_slots[monster.m_slot].lease;
}

void CCSOLMonsterPool::Park(CCSOLScenarioMonster& monster, bool clearCallbacks)
{
    monster.pev->effects |= EF_NODRAW;
    monster.pev->solid = SOLID_NOT;
    monster.pev->movetype = MOVETYPE_NONE;
    monster.pev->health = 0;
    monster.pev->deadflag = DEAD_DEAD;
    if (clearCallbacks)
    {
        monster.m_behaviorReady = false;
        monster.SetThink(nullptr);
        monster.SetTouch(nullptr);
    }
    // Original Free leaves nextthink, type identity and behavior intact.
}

void CCSOLMonsterPool::RemoveOwned()
{
    const auto access = m_entityAccess;
    if (!access->Allowed()) return;
    const auto inflictor = m_damageInflictor;
    m_damageInflictor = static_cast<CBaseEntity*>(nullptr);
    if (Live(inflictor.Get<CBaseEntity>())) UTIL_Remove(inflictor.Get<CBaseEntity>());
    for (auto& slot : m_slots)
    {
        if (!access->Allowed()) return;
        auto* base = slot.entity.Get<CBaseEntity>();
        // A matching private object marked FL_KILLME still needs detaching;
        // its OnDestroy runs later and must not call a dead pool owner.
        auto* monster = base ? dynamic_cast<CCSOLScenarioMonster*>(base) : nullptr;
        if (monster && monster->m_pool == this)
        {
            monster->m_pool = nullptr;
            monster->m_type = nullptr;
            monster->m_behaviorReady = false;
            monster->SetThink(nullptr);
            monster->SetTouch(nullptr);
            const auto handle = slot.entity;
            slot = {};
            if (m_registered) --m_registered;
            auto behavior = std::move(monster->m_behavior);
            auto effects = std::move(monster->m_damageEffects);
            behavior.reset();
            effects.reset();
            // A concrete behavior's destructor may invoke engine callbacks.
            // Resolve the entity again before touching its private data.
            if (!access->Allowed()) return;
            base = handle.Get<CBaseEntity>();
            if (Live(base) && base == monster && dynamic_cast<CCSOLScenarioMonster*>(base) == monster)
                UTIL_Remove(base);
        }
        slot = {};
    }
    m_idle.clear();
    m_active = 0;
    m_registered = 0;
    m_category = {};
}

bool CCSOLMonsterPool::Initialize()
{
    if (!EntityAccessAllowed() || m_busy || m_state != CSOLMonsterPoolState::Empty) return false;
    Operation operation(m_busy);
    m_error.clear();
    for (std::size_t i = 0; i < m_slots.size(); ++i)
    {
        const auto name = ALLOC_STRING("monster_entity");
        if (!EntityAccessAllowed()) return false;
        const auto edict = CREATE_NAMED_ENTITY(name);
        if (!EntityAccessAllowed()) return false;
        EHANDLE created(edict);
        const bool nullEntity = FNullEnt(edict);
        if (!EntityAccessAllowed()) return false;
        if (nullEntity || created.Get() != edict)
        {
            RemoveOwned();
            return Fail("scenario monster entity allocation failed");
        }
        SpawnAccessScope spawnAccess(m_entityAccess);
        const int result = DispatchSpawn(edict);
        if (!EntityAccessAllowed()) return false;
        auto* base = created.Get<CBaseEntity>();
        auto* monster = Live(base) && FClassnameIs(base->pev, "monster_entity") ?
            dynamic_cast<CCSOLScenarioMonster*>(base) : nullptr;
        if (result < 0 || !monster || monster->m_pool || m_state == CSOLMonsterPoolState::Failed)
        {
            if (Live(base) && (!monster || !monster->m_pool)) UTIL_Remove(base);
            RemoveOwned();
            return Fail("scenario monster Spawn rejected or returned an invalid private object");
        }
        monster->m_pool = this;
        monster->m_slot = i;
        monster->m_type = nullptr;
        m_slots[i].entity = monster;
        m_slots[i].identity = monster;
        ++m_registered;
        Park(*monster);
        m_idle.push_back(i);
    }
    // A later Spawn callback can remove an earlier slot. Do not overwrite its
    // failure notification with Ready after validating only the newest entity.
    if (m_state == CSOLMonsterPoolState::Failed || !Validate())
    {
        RemoveOwned();
        return Fail("scenario monster pool changed during initialization");
    }
    m_state = CSOLMonsterPoolState::Ready;
    return true;
}

bool CCSOLMonsterPool::Shutdown()
{
    if (m_busy) return false;
    Operation operation(m_busy);
    RemoveOwned();
    // Engine teardown owns the remaining OnDestroy calls after revocation.
    // Their registration records must survive until those calls have returned.
    if (m_registered) return false;
    m_state = CSOLMonsterPoolState::Empty;
    m_error.clear();
    return true;
}

bool CCSOLMonsterPool::BindEntityAccess(std::shared_ptr<const CSOLScenarioEntityAccess> access)
{
    if (!access || !access->Allowed() || m_busy || m_registered ||
        m_state != CSOLMonsterPoolState::Empty) return false;
    m_entityAccess = std::move(access);
    return true;
}

bool CCSOLMonsterPool::ForgetMap()
{
    // Never abandon live entities that still point to this owner. Shutdown or
    // actual engine OnDestroy must detach them before forgetting the map.
    if (m_busy || m_registered) return false;
    m_slots = {};
    m_damageInflictor = static_cast<CBaseEntity*>(nullptr);
    m_idle.clear();
    m_active = 0;
    m_category = {};
    m_state = CSOLMonsterPoolState::Empty;
    m_error.clear();
    return true;
}

bool CCSOLMonsterPool::Validate()
{
    for (std::size_t i = 0; i < m_slots.size(); ++i)
        if (!At(i)) return Fail("scenario monster pool lost an owned entity");
    return true;
}

bool CCSOLMonsterPool::Return(std::size_t slot, bool clearCallbacks)
{
    auto* monster = At(slot);
    if (!monster) return Fail("scenario monster return has no valid entity");
    auto& record = m_slots[slot];
    // NODRAW is the original guard against duplicate return. Do not infer
    // that a non-solid/dead entity has already returned: death precedes Free.
    if (record.borrowed && !(monster->pev->effects & EF_NODRAW))
    {
        if (!m_active || !m_category[record.priority])
            return Fail("scenario monster pool counters are inconsistent");
        Park(*monster, clearCallbacks);
        record.borrowed = false;
        --m_active;
        --m_category[record.priority];
        m_idle.push_back(slot);
    }
    else if (clearCallbacks)
    {
        monster->SetThink(nullptr);
        monster->SetTouch(nullptr);
    }
    return true;
}

bool CCSOLMonsterPool::Free(CCSOLScenarioMonster& monster)
{
    if (!EntityAccessAllowed() || m_busy || m_state != CSOLMonsterPoolState::Ready || At(monster.m_slot) != &monster)
        return false;
    Operation operation(m_busy);
    return Return(monster.m_slot);
}

bool CCSOLMonsterPool::ParkForDeath(CCSOLScenarioMonster& monster)
{
    if (!EntityAccessAllowed() || m_busy || m_state != CSOLMonsterPoolState::Ready || At(monster.m_slot) != &monster)
        return false;
    Operation operation(m_busy);
    return Return(monster.m_slot, false);
}

bool CCSOLMonsterPool::RestartRound()
{
    if (m_busy || m_state != CSOLMonsterPoolState::Ready) return false;
    Operation operation(m_busy);
    if (!Validate()) return false;
    // Preserve idle FIFO. Append only borrowed slots, in ascending slot order.
    for (std::size_t i = 0; i < m_slots.size(); ++i)
    {
        // Destination round reset must not carry an old player's DOT into
        // the new round. Ordinary Free and early death parking still retain it.
        if (!ClearPoolEffects(*this, *At(i))) return Fail("scenario round effect cleanup cancelled");
        if (!Return(i)) return false;
        At(i)->m_type = nullptr;
    }
    return true;
}

CCSOLScenarioMonster* CCSOLMonsterPool::Acquire(const csol::scenario::MonsterType& type,
    int point, csol::scenario::SpawnPoints& points, std::span<const std::size_t> protectedSlots,
    const csol::scenario::MonsterType** previousType)
{
    if (previousType) *previousType = nullptr;
    if (m_busy || m_state != CSOLMonsterPoolState::Ready) return nullptr;
    Operation operation(m_busy);
    if (!Validate()) return nullptr;
    if (type.priority < 0 || type.priority > 2)
    {
        Fail("unsupported scenario monster priority");
        return nullptr;
    }
    const auto priority = static_cast<std::size_t>(type.priority);
    if (priority == 0 && m_category[0] >= CategoryLimits[0]) return nullptr;
    if (m_lastLease == std::numeric_limits<std::uint64_t>::max())
    {
        Fail("scenario monster lease identity exhausted");
        return nullptr;
    }
    if (m_idle.empty() && priority != 0)
    {
        for (std::size_t i = 0; i < m_slots.size(); ++i)
        {
            const auto* oldType = At(i)->m_type;
            if (!oldType || oldType->priority != 0 ||
                std::find(protectedSlots.begin(), protectedSlots.end(), i) != protectedSlots.end()) continue;
            auto* old = At(i);
            // Original eviction has no alive/solid gate. A dying ordinary
            // monster can return its point quota again here before Free.
            points.ReturnPointQuota(old->pev->team, old->m_spawnPoint);
            old->pev->solid = SOLID_NOT;
            if (!Return(i)) return nullptr;
            if (!ClearPoolEffects(*this, *At(i)))
            { Fail("scenario evicted monster effect cleanup cancelled"); return nullptr; }
            break;
        }
    }
    if (m_idle.empty()) return nullptr;
    const auto index = m_idle.front();
    m_idle.pop_front();
    auto* monster = At(index);
    auto& slot = m_slots[index];
    slot.lease = ++m_lastLease;
    slot.borrowed = true;
    // Host callback guard, not a CSO status. A cancelled old-lease message
    // cannot keep the next borrow locked or clear its limits on unwinding.
    monster->SpeedState().clearingLimits = false;
    slot.priority = static_cast<std::uint8_t>(priority);
    ++m_active;
    ++m_category[priority];
    if (previousType) *previousType = monster->m_type;
    monster->m_type = &type;
    monster->m_typeId = type.id;
    monster->m_spawnPoint = point;
    monster->m_behaviorReady = false;
    monster->pev->team = 1;
    monster->pev->flags = FL_MONSTER;
    monster->pev->effects &= ~EF_NODRAW;
    monster->pev->deadflag = DEAD_NO;
    monster->pev->solid = SOLID_SLIDEBOX;
    monster->pev->movetype = MOVETYPE_STEP;
    // Attribute/behavior installation is not implemented by the pool. Keep
    // this intermediate object non-damageable with no scheduled old behavior.
    monster->pev->takedamage = DAMAGE_NO;
    monster->SetThink(nullptr);
    monster->SetTouch(nullptr);
    return monster;
}

bool CCSOLMonsterPool::BeginDeath(CCSOLScenarioMonster& monster, float freeAt,
    csol::scenario::SpawnPoints& points)
{
    if (m_busy || !Borrowed(monster) || monster.pev->deadflag == DEAD_DEAD ||
        !std::isfinite(freeAt) || freeAt <= 0) return false;
    Operation operation(m_busy);
    if (m_slots[monster.m_slot].priority != 2)
        points.ReturnPointQuota(monster.pev->team, monster.m_spawnPoint);
    monster.pev->takedamage = DAMAGE_NO;
    monster.pev->deadflag = DEAD_DEAD;
    monster.pev->solid = SOLID_NOT;
    monster.SetThink(&CCSOLScenarioMonster::FreeThink);
    monster.pev->nextthink = freeAt;
    return true;
}

bool CCSOLMonsterPool::HasSolidMonster(int type) const
{
    if (m_state != CSOLMonsterPoolState::Ready) return false;
    for (std::size_t i = 0; i < m_slots.size(); ++i)
        if (const auto* monster = At(i); monster && monster->pev->solid != SOLID_NOT &&
            monster->TypeId() == type) return true;
    return false;
}

std::size_t CCSOLMonsterPool::CategoryCount(std::size_t priority) const
{
    return priority < m_category.size() ? m_category[priority] : 0;
}

void CCSOLMonsterPool::EntityDestroyed(CCSOLScenarioMonster& monster)
{
    if (monster.m_slot >= m_slots.size()) return;
    // The engine guarantees this private object for the OnDestroy call only.
    // Other edicts may already be gone; compare a saved address, not EHANDLE.
    if (m_slots[monster.m_slot].identity != &monster) return;
    m_slots[monster.m_slot] = {};
    if (m_registered) --m_registered;
    if (EntityAccessAllowed()) Fail("scenario monster private data was destroyed before pool shutdown");
}
