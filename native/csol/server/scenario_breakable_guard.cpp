// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_breakable_guard.h"

#include <utility>
#include <memory>
#include <vector>

thread_local CSOLBreakableScope* CSOLBreakableScope::s_active{};
CSOLBreakableScope::CSOLBreakableScope(Guard valid, bool newOperation, bool retainForDelay)
    : m_valid(std::move(valid)), m_newOperation(newOperation),
      m_retainForDelay(retainForDelay), m_previous(s_active)
{
    if (m_valid) s_active = this;
}
CSOLBreakableScope::~CSOLBreakableScope() { s_active = m_previous; }
bool CSOLBreakableScope::Valid() const
{
    // The root owns map/round lifetime. Check it before a relay's EHANDLE,
    // whose edict storage may already belong to the next map.
    return (m_newOperation || !m_previous || m_previous->Valid()) && (!m_valid || m_valid());
}
bool CSOLBreakableScope::Current(const CBaseEntity*)
{
    // A relay changes pCaller but still belongs to its originating wall's
    // operation. Every ancestor must remain current through the whole chain.
    return !s_active || s_active->Valid();
}

CSOLBreakableScope::Guard CSOLBreakableScope::Capture()
{
    if (!s_active) return {};
    std::vector<Guard> guards;
    for (auto* scope = s_active; scope; scope = scope->m_previous)
    {
        if (scope->m_valid && scope->m_retainForDelay) guards.push_back(scope->m_valid);
        if (scope->m_newOperation) break;
    }
    return [guards = std::move(guards)]()
    {
        for (auto i = guards.rbegin(); i != guards.rend(); ++i) if (!(*i)()) return false;
        return true;
    };
}

CSOLBreakableScope::Guard CSOLBreakableScope::Follow(const CBaseEntity* entity)
{
    if (!s_active || !entity) return {};
    if (!Current(nullptr)) return []() { return false; };
    const EHANDLE handle(const_cast<CBaseEntity*>(entity));
    return [handle, entity]()
    {
        auto* current = handle.Get<CBaseEntity>();
        return current == entity && !(current->pev->flags & FL_KILLME);
    };
}

// EXPORT gives the engine-visible Think method external linkage on Windows.
// Keep its class out of an anonymous namespace, as for the SDK delay class.
class CSOLScenarioDelayedUse final : public CBaseDelay
{
public:
    void Arm(CSOLBreakableScope::Guard guard)
    {
        m_guard = std::move(guard);
        SetThink(&CSOLScenarioDelayedUse::GuardedThink);
    }
    CSOLBreakableScope::Guard LifetimeCheck() const
    {
        return [alive = m_alive]() { return *alive; };
    }
    int ObjectCaps() override { return CBaseDelay::ObjectCaps() | FCAP_DONT_SAVE; }
    void OnDestroy() override
    {
        *m_alive = false;
        m_alive.reset();
        m_guard = {}; // Engine private-data release does not run destructors.
        CBaseDelay::OnDestroy();
    }
    void EXPORT GuardedThink()
    {
        auto alive = m_alive;
        const EHANDLE self(this);
        CSOLBreakableScope scope(m_guard);
        CSOLBreakableScope selfScope(CSOLBreakableScope::Follow(this), false, false);
        if (CSOLBreakableScope::Current(this))
        {
            auto* activator = pev->owner ? CBaseEntity::Instance(pev->owner) : nullptr;
            SUB_UseTargets(activator, static_cast<USE_TYPE>(pev->button), 0);
        }
        // Removal is cleanup even when the wall/round was invalidated. A
        // target may already have freed this entity, so never reuse its this.
        if (*alive)
            if (auto* entity = self.Get<CBaseEntity>()) REMOVE_ENTITY(entity->edict());
    }
private:
    CSOLBreakableScope::Guard m_guard;
    std::shared_ptr<bool> m_alive{std::make_shared<bool>(true)};
};

CBaseDelay* CSOLCreateBreakableDelay(CSOLBreakableScope::Guard& alive)
{
    alive = {};
    auto guard = CSOLBreakableScope::Capture();
    if (!guard)
    {
        auto* delay = GetClassPtr<CCSDelay>(static_cast<CBaseDelay*>(nullptr));
        delay->SetThink(&CBaseDelay::DelayThink);
        return delay;
    }
    auto* delay = GetClassPtr<CCSDelay>(static_cast<CSOLScenarioDelayedUse*>(nullptr));
    delay->Arm(std::move(guard));
    alive = delay->LifetimeCheck();
    return delay;
}
