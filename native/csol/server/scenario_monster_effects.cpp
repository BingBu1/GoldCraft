// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_monster_effects.h"
#include "scenario_monster_factory.h"
#include "weapons.h"

#include <cmath>
#include <numbers>

#pragma clang fp contract(off)

class CCSOLScenarioDamageInflictor : public CBaseEntity, public CSOLWeaponDamageSource
{
public:
    void Spawn() override
    {
        if (!m_sourceId) m_sourceId = std::make_shared<int>(0);
        pev->solid = SOLID_NOT;
        pev->movetype = MOVETYPE_NONE;
        pev->effects |= EF_NODRAW;
        pev->takedamage = DAMAGE_NO;
    }
    int DamageWeaponId() const override { return m_sourceId ? *m_sourceId : 0; }
    std::shared_ptr<int> SourceId() const { return m_sourceId; }
    void OnDestroy() override
    {
        m_sourceId.reset();
        CBaseEntity::OnDestroy();
    }
private:
    // The active dispatch retains this cell so its final zeroing never has
    // to dereference an edict released by a nested map/death callback.
    std::shared_ptr<int> m_sourceId;
};
LINK_ENTITY_TO_CLASS(dummy_inflictor, CCSOLScenarioDamageInflictor, CCSEntity)

namespace
{
constexpr int DamageBits = 0x08000000;
struct Busy
{
    bool& value;
    explicit Busy(bool& flag) : value(flag) { value = true; }
    ~Busy() { value = false; }
};
bool Live(const EHANDLE& handle)
{
    const auto* entity = handle.Get<CBaseEntity>();
    return entity && !(entity->pev->flags & FL_KILLME);
}
Vector Rotate(const Vector& value, float yaw)
{
    const float radians = static_cast<float>(static_cast<double>(yaw / 180.0f) * std::numbers::pi);
    const float cosine = static_cast<float>(std::cos(static_cast<double>(radians)));
    const float sine = static_cast<float>(std::sin(static_cast<double>(radians)));
    return Vector(cosine * value.x - sine * value.y, cosine * value.y + sine * value.x, value.z);
}

// This bridge covers one SDK dispatch, not the lifetime of the entire DOT
// traversal. In particular, Clear/Trace and AddMultiDamage's other-target
// flushes run outside it. Nested scopes always hide the outer scope.
struct DamageDispatch
{
    static thread_local DamageDispatch* active;
    DamageDispatch* previous{active};
    CSOLMonsterSpawnCall& call;
    CSOLMonsterEffectGuard guard;
    const CCSOLScenarioMonster* target;
    EHANDLE inflictor, attacker;
    CSOLMonsterDeathContext context;
    bool armed{}, consumed{};
    DamageDispatch(CSOLMonsterSpawnCall& spawn, const CSOLMonsterEffectGuard& lifetime,
        CCSOLScenarioMonster& monster, CBaseEntity& source, CBaseEntity& player,
        CSOLMonsterDeathContext damageContext, bool direct = false)
        : call(spawn), guard(lifetime), target(&monster), inflictor(&source),
          attacker(&player), context(damageContext), armed(direct) { active = this; }
    ~DamageDispatch() { active = previous; }
};
thread_local DamageDispatch* DamageDispatch::active{};

void EXT_FUNC ApplyDotMultiDamageOriginal(entvars_t* inflictor, entvars_t* attacker)
{
    auto* scope = DamageDispatch::active;
    if (!scope || scope->call.Current() != scope->target || scope->guard.Current() != scope->target ||
        !Live(scope->inflictor) || !Live(scope->attacker)) return;
    // Only this invocation's original-function trampoline arms the context.
    // A hook's earlier ordinary TakeDamage/ApplyMultiDamage uses the normal
    // SDK entry, so cannot consume our pending context even with identical args.
    Busy armed(scope->armed);
#ifdef REGAMEDLL_API
    ApplyMultiDamage_OrigFunc(inflictor, attacker);
#else
    ApplyMultiDamage(inflictor, attacker);
#endif
}
}

CSOLMonsterDeathContext CSOL_ConsumeMonsterDamageContext(CCSOLScenarioMonster& monster,
    entvars_t* inflictor, entvars_t* attacker, int bits)
{
    auto* scope = DamageDispatch::active;
    if (!scope || !scope->armed || scope->consumed || scope->target != &monster || bits != DamageBits ||
        scope->call.Current() != &monster || scope->guard.Current() != &monster ||
        !Live(scope->inflictor) || !Live(scope->attacker) ||
        scope->inflictor.Get<CBaseEntity>()->pev != inflictor ||
        scope->attacker.Get<CBaseEntity>()->pev != attacker) return {};
    scope->consumed = true;
    return scope->context;
}

CBaseEntity* CCSOLMonsterPool::DamageInflictor(CSOLMonsterSpawnCall& call)
{
    auto* monster = call.Current();
    if (!monster || monster->Pool() != this || m_busy || m_creatingInflictor) return nullptr;
    if (Live(m_damageInflictor))
        return dynamic_cast<CCSOLScenarioDamageInflictor*>(m_damageInflictor.Get<CBaseEntity>());
    Busy busy(m_creatingInflictor);
    const auto name = ALLOC_STRING("dummy_inflictor");
    if (!call.Current()) return nullptr;
    auto* created = CREATE_NAMED_ENTITY(name);
    if (!call.SameMap()) return nullptr; // Old-map edicts may already be released.
    if (FNullEnt(created)) return nullptr;
    EHANDLE candidate(created);
    if (!call.Current())
    {
        if (Live(candidate)) UTIL_Remove(candidate.Get<CBaseEntity>());
        return nullptr;
    }
    // Publish before DispatchSpawn so teardown owns a partially spawned entity.
    m_damageInflictor = candidate;
    const int result = DispatchSpawn(candidate.Get());
    if (!call.Current())
    {
        if (call.SameMap())
        {
            if (m_damageInflictor.Get() == candidate.Get())
                m_damageInflictor = static_cast<CBaseEntity*>(nullptr);
            if (Live(candidate)) UTIL_Remove(candidate.Get<CBaseEntity>());
        }
        return nullptr;
    }
    auto* entity = Live(candidate) ?
        dynamic_cast<CCSOLScenarioDamageInflictor*>(candidate.Get<CBaseEntity>()) : nullptr;
    if (result < 0 || !entity)
    {
        m_damageInflictor = static_cast<CBaseEntity*>(nullptr);
        if (Live(candidate)) UTIL_Remove(candidate.Get<CBaseEntity>());
        return nullptr;
    }
    return entity;
}

CCSOLScenarioMonster* CCSOLMonsterEffects::Current(const CSOLMonsterEffectGuard& guard) const
{
    auto* monster = guard.Current();
    return monster && monster->DamageEffects().get() == this ? monster : nullptr;
}

int CCSOLMonsterEffects::SourceIndex(const CSOLMonsterEffectGuard& guard, const EHANDLE& source) const
{
    auto* monster = Current(guard);
    if (!monster || monster->pev->deadflag == DEAD_DEAD || !Live(source) ||
        !g_engfuncs.pfnIndexOfEdict) return -1;
    const int index = ENTINDEX(source.Get());
    monster = Current(guard);
    if (!monster || monster->pev->deadflag == DEAD_DEAD || !Live(source)) return -1;
    return index >= 1 && index <= 32 ? index : -1;
}

bool CCSOLMonsterEffects::Clear(const CSOLMonsterEffectGuard& guard, std::size_t index)
{
    if (!Current(guard)) return false;
    auto notify = m_slots[index].notify;
    if (notify && *notify && m_notifying[index].lock() != notify)
    {
        struct Notification
        {
            std::weak_ptr<std::function<void()>>& active;
            std::weak_ptr<std::function<void()>> previous;
            ~Notification() { active = std::move(previous); }
        } notification{m_notifying[index], m_notifying[index]};
        m_notifying[index] = notify;
        // Destination reentry protection: do not recursively invoke the same
        // callable still on the stack. Same-slot replacement/clear is allowed;
        // after notification we reset the CURRENT record, as in the original.
        (*notify)();
    }
    if (!Current(guard)) return false;
    notify.reset();
    if (!Current(guard)) return false;
    auto destroyed = std::move(m_slots[index].notify);
    m_slots[index] = {};
    destroyed.reset(); // Destructor only, never a second notification.
    return Current(guard) != nullptr;
}

bool CCSOLMonsterEffects::Install(const CSOLMonsterEffectGuard& guard, entvars_t* source,
    float damage, int count, std::uint8_t flag, int sourceId, int hitgroup,
    const Vector& worldOffset, std::function<void()> notify)
{
    if (!Current(guard) || !source) return false;
    const EHANDLE attacker(CBaseEntity::Instance(source));
    const int index = SourceIndex(guard, attacker);
    if (index < 0 || !Clear(guard, index)) return false;
    auto* monster = Current(guard);
    if (!monster || !Live(attacker)) return false;
    auto& slot = m_slots[index];
    slot.source = attacker;
    slot.count = count;
    slot.damage = damage;
    slot.flag = flag;
    slot.hitgroup = hitgroup;
    slot.localOffset = Rotate(worldOffset, -monster->pev->angles.y);
    slot.sourceId = sourceId;
    if (notify) slot.notify = std::make_shared<std::function<void()>>(std::move(notify));
    return true;
}

bool CCSOLMonsterEffects::ClearSource(const CSOLMonsterEffectGuard& guard,
    entvars_t* source, bool& exhausted)
{
    exhausted = false;
    if (!Current(guard)) return false;
    const EHANDLE attacker(source ? CBaseEntity::Instance(source) : nullptr);
    const int index = SourceIndex(guard, attacker);
    if (index >= 0 && !Clear(guard, index)) return false;
    const auto* monster = Current(guard);
    if (!monster) return false;
    const float health = monster->pev->health;
    for (std::size_t i = 1; i < Slots; ++i)
    {
        const auto& slot = m_slots[i];
        if (!Live(slot.source) || slot.count <= 0) continue;
        if (slot.flag == 0 && health == 1.0f) continue;
        if (slot.flag == 1 && health <= 0) continue;
        return true;
    }
    exhausted = true;
    return true;
}

bool CCSOLMonsterEffects::ClearAll(const CSOLMonsterEffectGuard& guard)
{
    for (std::size_t i = 0; i < Slots; ++i)
        if (!Clear(guard, i)) return false;
    return true;
}

bool CSOL_ClearMonsterEffects(CSOLMonsterDeathCall& call)
{
    const CSOLMonsterEffectGuard guard{[](const void* context)
        { return static_cast<const CSOLMonsterDeathCall*>(context)->Current(); }, &call};
    auto* monster = guard.Current();
    if (!monster) return false;
    auto effects = monster->DamageEffects();
    return effects && effects->ClearAll(guard);
}

bool CCSOLMonsterEffects::Tick(CSOLMonsterSpawnCall& call,
    const CSOLMonsterEffectGuard& guard, csol::scenario::ConfigError& error)
{
    if (!Current(guard) || m_ticking) return false;
    Busy busy(m_ticking);
    // CSO allocated/cleared 33 records but ticked only 0..31. Include slot32
    // for the requested 32-player server; preserve direct index addressing.
    for (std::size_t i = 0; i < Slots; ++i)
    {
        auto* monster = Current(guard);
        if (!monster) return false;
        auto& record = m_slots[i];
        const EHANDLE attacker = record.source;
        const float damage = record.damage;
        if (record.count > 0 && Live(attacker))
        {
            --record.count;
            const int sourceId = record.sourceId;
            auto* pool = monster->Pool();
            auto* dummy = dynamic_cast<CCSOLScenarioDamageInflictor*>(pool->DamageInflictor(call));
            monster = Current(guard);
            if (!monster) return false;
            if (!dummy)
            { error.message = "scenario DOT inflictor allocation failed"; return false; }
            const EHANDLE inflictor(dummy);
            auto sourceState = dummy->SourceId();
            if (!sourceState)
            { error.message = "scenario DOT inflictor source state is unavailable"; return false; }
            *sourceState = sourceId;
            // Always clear to zero after a nested dispatch; do not restore
            // the old ID. The cell survives round cancellation/entity removal.
            struct ResetSource
            {
                std::shared_ptr<int> source;
                ~ResetSource() { *source = 0; }
            } reset{std::move(sourceState)};
            const auto valid = [&]()
                { return Current(guard) && Live(attacker) && Live(inflictor); };
            if (!valid()) return false;
            CSOLMonsterDeathContext context;
            context.bytes[7] = 1;
            if (record.hitgroup != -1)
            {
                context.bytes[5] = context.bytes[6] = 1;
                TraceResult trace{};
                trace.pHit = monster->edict();
                trace.iHitgroup = record.hitgroup;
                trace.vecEndPos = monster->pev->origin + Rotate(record.localOffset, monster->pev->angles.y);
                trace.vecPlaneNormal = Vector(0, 0, 1);
                ClearMultiDamage();
                if (!valid()) return false;
                Current(guard)->TraceAttack(attacker.Get<CBaseEntity>()->pev, damage,
                    Vector(0, 0, -1), &trace, DamageBits);
                if (!valid()) return false;
                DamageDispatch dispatch(call, guard, *Current(guard), *inflictor.Get<CBaseEntity>(),
                    *attacker.Get<CBaseEntity>(), context);
#ifdef REGAMEDLL_API
                g_ReGameHookchains.m_ApplyMultiDamage.callChain(ApplyDotMultiDamageOriginal,
                    inflictor.Get<CBaseEntity>()->pev, attacker.Get<CBaseEntity>()->pev);
#else
                ApplyDotMultiDamageOriginal(inflictor.Get<CBaseEntity>()->pev, attacker.Get<CBaseEntity>()->pev);
#endif
            }
            else
            {
                DamageDispatch dispatch(call, guard, *monster, *dummy, *attacker.Get<CBaseEntity>(), context, true);
                monster->TakeDamage(dummy->pev, attacker.Get<CBaseEntity>()->pev, damage, DamageBits);
            }
        }
        monster = Current(guard);
        if (!monster) return false;
        // A hit may replace this slot. Inspect the current count/callback,
        // never the entry snapshot; null sources do not consume positive count.
        if ((m_slots[i].count <= 0 || monster->pev->health <= 0) && !Clear(guard, i)) return false;
    }
    return true;
}
