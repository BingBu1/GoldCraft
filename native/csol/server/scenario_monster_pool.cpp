// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_monster_pool.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

LINK_ENTITY_TO_CLASS(monster_entity, CCSOLScenarioMonster, CCSEntity)

namespace
{
constexpr const char* InitialModel = "models/player/zombi_origin/zombi_origin.mdl";
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
}

void CCSOLScenarioMonster::Precache()
{
    PRECACHE_MODEL(const_cast<char*>(InitialModel));
}

void CCSOLScenarioMonster::Spawn()
{
    EHANDLE self(this);
    Precache();
    if (self.Get<CBaseEntity>() != this || !Live(self.Get<CBaseEntity>())) return;
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
    pev->skin = std::max(pev->skin, 0);
    SetThink(nullptr);
    SetTouch(nullptr);
    SET_MODEL(edict(), InitialModel);
}

bool CCSOLScenarioMonster::InstallBehavior(std::unique_ptr<CCSOLMonsterBehavior> behavior)
{
    return m_pool && m_pool->InstallBehavior(*this, std::move(behavior));
}

void CCSOLScenarioMonster::TraceAttack(entvars_t* attacker, float damage,
    Vector direction, TraceResult* trace, int damageType)
{
    // CSO's base trace stage scales the trace hitgroup, then queues damage;
    // TakeDamage has its own later hitgroup/armor/reward stages. Applying it
    // immediately here would break shotgun aggregation and attacker credit.
    if (!trace || !m_pool || m_pool->Busy() || !m_pool->Borrowed(*this) ||
        !m_behaviorReady || !m_behavior || pev->takedamage == DAMAGE_NO ||
        !std::isfinite(damage)) return;
    const EHANDLE self(this);
    const auto lease = m_pool->Lease(*this);
    auto* pool = m_pool;
    auto behavior = RetainBehavior();
    m_host.lastHitgroup = trace->iHitgroup;
    if (trace->pHit && trace->pHit->pvPrivateData)
    {
        const auto* hit = CBaseEntity::Instance(trace->pHit);
        if (self.Get<CBaseEntity>() != this || m_pool != pool ||
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
    if (!m_pool || m_pool->Busy() || !m_pool->Borrowed(*this) || !m_behaviorReady || !m_behavior ||
        pev->takedamage == DAMAGE_NO)
        return FALSE;
    auto behavior = RetainBehavior();
    return behavior->TakeDamage(*this, inflictor, attacker, damage, damageType);
}

void CCSOLScenarioMonster::Killed(entvars_t* attacker, int gib)
{
    if (m_pool && !m_pool->Busy() && m_pool->Borrowed(*this) && m_behaviorReady && m_behavior &&
        pev->deadflag != DEAD_DEAD)
    {
        auto behavior = RetainBehavior();
        behavior->Killed(*this, attacker, gib);
    }
}

void CCSOLScenarioMonster::BehaviorThink()
{
    if (m_pool && !m_pool->Busy() && m_pool->Borrowed(*this) && m_behaviorReady && m_behavior)
    {
        auto behavior = RetainBehavior();
        behavior->Think(*this);
    }
}

void CCSOLScenarioMonster::FreeThink()
{
    if (m_pool) m_pool->Free(*this);
}

void CCSOLScenarioMonster::OnDestroy()
{
    if (m_destroying) return;
    m_destroying = true;
    auto* owner = m_pool;
    m_pool = nullptr; // Destructors may reenter; detach before releasing them.
    m_behaviorReady = false;
    m_speed.reset();
    if (owner) owner->EntityDestroyed(*this);
    auto behavior = std::move(m_behavior);
    CBaseAnimating::OnDestroy();
    behavior.reset(); // Engine Mem_Free does not run the C++ destructor.
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
    if (!gpGlobals || !gpGlobals->pStringBase || slot >= m_slots.size()) return nullptr;
    auto* base = m_slots[slot].entity.Get<CBaseEntity>();
    if (!Live(base) || !FClassnameIs(base->pev, "monster_entity")) return nullptr;
    auto* entity = dynamic_cast<CCSOLScenarioMonster*>(base);
    return entity && entity->m_pool == this && entity->m_slot == slot ? entity : nullptr;
}

bool CCSOLMonsterPool::Borrowed(const CCSOLScenarioMonster& monster) const
{
    return m_state == CSOLMonsterPoolState::Ready && monster.m_slot < m_slots.size() &&
        At(monster.m_slot) == &monster && m_slots[monster.m_slot].borrowed;
}

std::uint64_t CCSOLMonsterPool::Lease(const CCSOLScenarioMonster& monster) const
{
    if (m_state != CSOLMonsterPoolState::Ready || monster.m_slot >= m_slots.size() ||
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
    for (auto& slot : m_slots)
    {
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
            auto behavior = std::move(monster->m_behavior);
            behavior.reset();
            // A concrete behavior's destructor may invoke engine callbacks.
            // Resolve the entity again before touching its private data.
            base = slot.entity.Get<CBaseEntity>();
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
    if (m_busy || m_state != CSOLMonsterPoolState::Empty) return false;
    Operation operation(m_busy);
    m_error.clear();
    for (std::size_t i = 0; i < m_slots.size(); ++i)
    {
        const auto edict = CREATE_NAMED_ENTITY(ALLOC_STRING("monster_entity"));
        if (FNullEnt(edict))
        {
            RemoveOwned();
            return Fail("scenario monster entity allocation failed");
        }
        EHANDLE created(edict);
        const int result = DispatchSpawn(edict);
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
    m_state = CSOLMonsterPoolState::Empty;
    m_error.clear();
    return true;
}

bool CCSOLMonsterPool::ForgetMap()
{
    // Never abandon live entities that still point to this owner. Shutdown or
    // actual engine OnDestroy must detach them before forgetting the map.
    if (m_busy || m_registered) return false;
    m_slots = {};
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
    if (m_busy || m_state != CSOLMonsterPoolState::Ready || At(monster.m_slot) != &monster)
        return false;
    Operation operation(m_busy);
    return Return(monster.m_slot);
}

bool CCSOLMonsterPool::ParkForDeath(CCSOLScenarioMonster& monster)
{
    if (m_busy || m_state != CSOLMonsterPoolState::Ready || At(monster.m_slot) != &monster)
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
    // OnDestroy has already detached owner, so compare the actual handle.
    if (m_slots[monster.m_slot].entity.Get<CBaseEntity>() != &monster) return;
    m_slots[monster.m_slot].entity = nullptr;
    if (m_registered) --m_registered;
    Fail("scenario monster private data was destroyed before pool shutdown");
}
