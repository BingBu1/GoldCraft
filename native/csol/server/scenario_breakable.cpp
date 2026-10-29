// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_breakable.h"
#include "game_rules_gate.h"
#include "scenario_breakable_guard.h"
#include "scenario_entity_lookup.h"

#include <cmath>
#include <cstdlib>
#include <bit>
#include <cstdio>
#include <cstring>

LINK_ENTITY_TO_CLASS(zbsbreak, CCSOLScenarioBreakable, CCSEntity)
LINK_ENTITY_TO_CLASS(zbstarget, CCSOLScenarioTarget, CCSEntity)

TYPEDESCRIPTION CCSOLScenarioBreakable::m_SaveData[] =
{
    DEFINE_FIELD(CCSOLScenarioBreakable, m_playerRatio, FIELD_FLOAT),
    DEFINE_FIELD(CCSOLScenarioBreakable, m_monsterRatio, FIELD_FLOAT),
};

TYPEDESCRIPTION CCSOLScenarioTarget::m_SaveData[] =
{
    DEFINE_FIELD(CCSOLScenarioTarget, m_active, FIELD_CHARACTER),
};

class CSOLScenarioWallCall
{
    struct State
    {
        CCSOLScenarioBreakable* wall;
        EHANDLE handle;
        std::shared_ptr<std::uint64_t> lifetime;
        std::uint64_t life;
        std::shared_ptr<const CSOLScenarioEntityAccess> access;
        std::shared_ptr<CCSOLScenarioWallRules> rules;
        std::uint64_t generation, serial;
        bool Current() const
        {
            return (!access || access->Allowed()) && lifetime && *lifetime == life &&
                (!rules || (rules->EntityAccessAllowed() && rules->Generation() == generation)) &&
                handle.Get<CBaseEntity>() == wall && !(wall->pev->flags & FL_KILLME) &&
                wall->m_serial == serial;
        }
    };
public:
    explicit CSOLScenarioWallCall(CCSOLScenarioBreakable& wall, bool newOperation = false,
        std::shared_ptr<const CSOLScenarioEntityAccess> access = {})
        : m_state{&wall, EHANDLE(&wall), wall.m_lifetime,
              wall.m_lifetime ? *wall.m_lifetime : 0, std::move(access), wall.m_rules,
              wall.m_rules ? wall.m_rules->Generation() : 0, wall.m_serial},
          m_scope([state = m_state]() { return state.Current(); }, newOperation) {}
    bool Current() const { return m_state.Current() && CSOLBreakableScope::Current(nullptr); }
private:
    State m_state;
    CSOLBreakableScope m_scope;
};

int CCSOLScenarioBreakable::Save(CSave& save)
{
    CSOLScenarioSdkScope sdk;
    if (sdk.Scenario() && !sdk.Current()) return 0;
    CSOLScenarioWallCall call(*this, false, sdk.EntityAccess());
    if (!CBreakable::Save(save) || !call.Current()) return 0;
    const int result = save.WriteFields("CCSOLScenarioBreakable", this, m_SaveData, ARRAYSIZE(m_SaveData));
    return call.Current() ? result : 0;
}

int CCSOLScenarioBreakable::Restore(CRestore& restore)
{
    CSOLScenarioSdkScope sdk;
    if (sdk.Scenario() && !sdk.Current()) return 0;
    ++m_serial;
    if (m_lifetime) ++*m_lifetime;
    m_damaging = false;
    CSOLScenarioWallCall call(*this, true, sdk.EntityAccess());
    if (!CBreakable::Restore(restore) || !call.Current()) return 0;
    const int result = restore.ReadFields("CCSOLScenarioBreakable", this, m_SaveData, ARRAYSIZE(m_SaveData));
    if (!call.Current()) return 0;
    m_valid = result && std::isfinite(m_playerRatio) && std::isfinite(m_monsterRatio);
    return m_valid ? result : 0;
}

void CCSOLScenarioBreakable::KeyValue(KeyValueData* data)
{
    CBreakable::KeyValue(data);
    if (FStrEq(data->szKeyName, "hmdmgratio") || FStrEq(data->szKeyName, "zbdmgratio"))
    {
        const float value = static_cast<float>(std::atof(data->szValue));
        if (!std::isfinite(value)) m_valid = false;
        if (FStrEq(data->szKeyName, "hmdmgratio")) m_playerRatio = value;
        else m_monsterRatio = value;
        data->fHandled = TRUE;
    }
    else if (FStrEq(data->szKeyName, "master"))
    {
        m_master = ALLOC_STRING(data->szValue);
        data->fHandled = TRUE;
    }
}

int CCSOLScenarioBreakable::Classify()
{
    return m_playerRatio == 0.0f && m_monsterRatio == 0.0f ? 0 : 16;
}

bool CCSOLScenarioBreakable::StatusHealth(int& health) const
{
    if (!m_valid || !m_rules || !m_rules->EntityAccessAllowed() ||
        (m_playerRatio == 0 && m_monsterRatio == 0) || (pev->flags & FL_KILLME) ||
        (pev->effects & EF_NODRAW) || pev->solid == SOLID_NOT || !std::isfinite(pev->health) ||
        pev->health <= 0 || double(pev->health) > 2147483647.0) return false;
    health = static_cast<int>(pev->health);
    return true;
}

void CCSOLScenarioBreakable::ConfigureTouch()
{
    if (m_playerRatio == 0.0f && m_monsterRatio == 0.0f)
        SetTouch(&CCSOLScenarioBreakable::MonsterTouch);
    else SetTouch(nullptr);
}

void CCSOLScenarioBreakable::Spawn()
{
    CSOLScenarioWallCall call(*this);
    CBreakable::Spawn();
    if (!call.Current()) return;
    ConfigureTouch(); // Deliberately after the base's trigger-only flag.
    if (m_playerRatio == 0.0f && m_monsterRatio > 0.0f) pev->team = 2;
    else if (m_playerRatio > 0.0f && m_monsterRatio == 0.0f) pev->team = 1;
}

void CCSOLScenarioBreakable::Restart()
{
    CSOLScenarioSdkScope sdkScope(CSOL_ScenarioOwnerRules(m_rules ? m_rules->EntityAccess().get() : nullptr));
    if (sdkScope.Scenario() && !sdkScope.Current()) return;
    ++m_serial;
    // A legitimate new-round reset must finish even when invoked by an old
    // target chain. Returning to that chain still cancels its older scope.
    CSOLScenarioWallCall call(*this, true);
    CBreakable::Restart();
    if (!call.Current()) return;
    ConfigureTouch(); // Restart does not repeat Spawn's team assignment.
    m_damaging = false;
}

void CCSOLScenarioBreakable::BindRules(std::shared_ptr<CCSOLScenarioWallRules> rules)
{
    ++m_serial;
    if (m_lifetime) ++*m_lifetime;
    m_damaging = false;
    m_rules = std::move(rules);
}

void CCSOLScenarioBreakable::OnDestroy()
{
    CSOLScenarioSdkScope sdkScope(CSOL_ScenarioOwnerRules(m_rules ? m_rules->EntityAccess().get() : nullptr));
    ++m_serial;
    if (m_lifetime) ++*m_lifetime;
    m_lifetime.reset();
    // GoldSrc releases private storage without invoking the C++ destructor.
    m_rules.reset();
    CBreakable::OnDestroy();
}

void CCSOLScenarioBreakable::MonsterTouch(CBaseEntity* other)
{
    CSOLScenarioSdkScope sdkScope(CSOL_ScenarioOwnerRules(m_rules ? m_rules->EntityAccess().get() : nullptr));
    if (sdkScope.Scenario() && !sdkScope.Current()) return;
    if (!other || !gpGlobals) return;
    CSOLScenarioWallCall call(*this);
    const int classification = other->Classify();
    if (!call.Current() || classification != 17) return;
    pev->angles.y = m_angle;
    UTIL_MakeVectors(pev->angles);
    if (!call.Current() || !gpGlobals) return;
    g_vecAttackDir = gpGlobals->v_forward;
    pev->takedamage = DAMAGE_NO;
    pev->deadflag = DEAD_DEAD;
    pev->effects = EF_NODRAW;
    SetTouch(nullptr);
    // No master/material/health gate: this is the original monster-only
    // collision trigger. Die owns the target chain and keeps this map edict.
    CBreakable::Die();
}

void CCSOLScenarioBreakable::Use(CBaseEntity* activator, CBaseEntity* caller, USE_TYPE type, float value)
{
    CSOLScenarioSdkScope sdkScope(CSOL_ScenarioOwnerRules(m_rules ? m_rules->EntityAccess().get() : nullptr));
    if (sdkScope.Scenario() && !sdkScope.Current()) return;
    CSOLScenarioWallCall call(*this);
    CBreakable::Use(activator, caller, type, value);
}

BOOL CCSOLScenarioBreakable::TakeDamage(entvars_t* inflictor, entvars_t* attacker,
    float damage, int bits)
{
    CSOLScenarioSdkScope sdkScope(CSOL_ScenarioOwnerRules(m_rules ? m_rules->EntityAccess().get() : nullptr));
    if (sdkScope.Scenario() && !sdkScope.Current()) return FALSE;
    auto rules = m_rules;
    if (!rules || !m_valid || m_damaging || !inflictor || !attacker || !std::isfinite(damage)) return FALSE;
    CSOLScenarioWallCall call(*this);
    EHANDLE source(CBaseEntity::Instance(inflictor)), actor(CBaseEntity::Instance(attacker));
    const auto current = [&]() { return call.Current(); };
    if (!source.Get<CBaseEntity>() || !actor.Get<CBaseEntity>()) return FALSE;
    if (!UTIL_IsMasterTriggered(m_master, actor.Get<CBaseEntity>()) || !current()) return FALSE;
    auto* entity = actor.Get<CBaseEntity>();
    if (!entity) return FALSE;
    const bool player = entity->IsPlayer() != FALSE;
    if (!current()) return FALSE;
    m_damaging = true;
    // Keep cancellation checks inside the cleanup: callbacks may free this
    // private object or restart the same wall before returning.
    struct Finish
    {
        decltype(current)& valid;
        CCSOLScenarioBreakable* wall;
        ~Finish() { if (valid()) wall->m_damaging = false; }
    } finish{current, this};
    float scaled{};
    const bool resolved = player ? rules->PlayerDamage(actor, damage, m_playerRatio, scaled) :
        rules->MonsterDamage(damage, m_monsterRatio, scaled);
    if (!current() || !resolved || !std::isfinite(scaled) || !(scaled > 0)) return FALSE;
    if (player)
    {
        const bool credited = rules->CreditPlayerDamage(actor, scaled);
        if (!current() || !credited) return FALSE;
    }
    entity = actor.Get<CBaseEntity>();
    auto* originator = source.Get<CBaseEntity>();
    if (!entity || !originator) return FALSE;
    // Original CBreakable checks this after CZBSBreak credits the scaled
    // input. The destination SDK omits that gate for direct virtual damage.
    // Keep the original order while protecting targets from melee callers.
    if (pev->takedamage == DAMAGE_NO) return FALSE;
    const float delay = m_flDelay;
    const BOOL result = CBreakable::TakeDamage(originator->pev, entity->pev, scaled, bits);
    // Base sends targets once. Restore only its delay mutation, without
    // replaying targets or touching a replacement map/round entity.
    if (!current()) return FALSE;
    m_flDelay = delay;
    return result;
}

void CCSOLScenarioTarget::Spawn()
{
    CSOLScenarioSdkScope sdk;
    if (!sdk.Scenario() || !sdk.Current()) return;
    ++m_serial;
    if (m_lifetime) ++*m_lifetime;
    CSOLScenarioWallCall call(*this, true, sdk.EntityAccess());
    CBreakable::Spawn();
    if (!sdk.Current() || !call.Current()) return;
    ConfigureTouch();
    if (m_playerRatio == 0.0f && m_monsterRatio > 0.0f) pev->team = 2;
    else if (m_playerRatio > 0.0f && m_monsterRatio == 0.0f) pev->team = 1;
    m_active = !(pev->spawnflags & SF_BREAK_TRIGGER_ONLY);
    // Original target explicitly uses the current max_health; its source is
    // not CBreakable's saved m_flHealth and must not be synthesized here.
    pev->health = pev->max_health;
    m_iconPublished = false;
    m_damaging = false;
    pev->nextthink = gpGlobals->time;
}

void CCSOLScenarioTarget::Restart()
{
    CSOLScenarioSdkScope sdk(CSOL_ScenarioOwnerRules(m_rules ? m_rules->EntityAccess().get() : nullptr));
    if (!sdk.Scenario() || !sdk.Current()) return;
    ++m_serial;
    CSOLScenarioWallCall call(*this, true, sdk.EntityAccess());
    CBreakable::Restart();
    if (!sdk.Current() || !call.Current()) return;
    ConfigureTouch();
    m_active = !(pev->spawnflags & SF_BREAK_TRIGGER_ONLY);
    pev->health = pev->max_health;
    m_damaging = false;
    // Restart does not reset icon publication, nextthink or Spawn's team.
}

void CCSOLScenarioTarget::KeyValue(KeyValueData* data)
{
    if (std::strncmp(data->szKeyName, "icon", 4) == 0)
    {
        int icon{};
        Vector position;
        // Reject malformed input rather than retaining uninitialized values.
        // The valid resource format and low-byte conversion follow CSO.
        if (std::sscanf(data->szValue, "%d %f %f %f", &icon, &position.x, &position.y, &position.z) != 4 ||
            !std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
            m_valid = false;
        else
        {
            m_icon = std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(icon));
            m_iconPosition = position;
        }
        data->fHandled = TRUE;
        return;
    }
    CCSOLScenarioBreakable::KeyValue(data);
}

void CCSOLScenarioTarget::Use(CBaseEntity*, CBaseEntity*, USE_TYPE, float)
{
    CSOLScenarioSdkScope sdk(CSOL_ScenarioOwnerRules(m_rules ? m_rules->EntityAccess().get() : nullptr));
    if (!sdk.Scenario() || !sdk.Current()) return;
    CSOLScenarioWallCall call(*this);
    if (!call.Current()) return;
    m_active = !m_active;
    pev->takedamage = m_active ? DAMAGE_YES : DAMAGE_NO;
}

BOOL CCSOLScenarioTarget::TakeDamage(entvars_t* inflictor, entvars_t* attacker, float damage, int bits)
{
    CSOLScenarioSdkScope sdk(CSOL_ScenarioOwnerRules(m_rules ? m_rules->EntityAccess().get() : nullptr));
    if (!sdk.Scenario() || !sdk.Current()) return FALSE;
    CSOLScenarioWallCall call(*this);
    if (!call.Current() || !m_active) return FALSE;
    const BOOL result = CCSOLScenarioBreakable::TakeDamage(inflictor, attacker, damage, bits);
    return sdk.Current() && call.Current() ? result : FALSE;
}

void CCSOLScenarioTarget::SetClearPointProtection(bool enabled)
{
    // This setter has no callbacks. The manager validates the current map
    // and target identity immediately before invoking it.
    pev->takedamage = enabled || ((pev->spawnflags & SF_BREAK_TRIGGER_ONLY) && !m_active)
        ? DAMAGE_NO : DAMAGE_YES;
}

int CCSOLScenarioTarget::Save(CSave& save)
{
    CSOLScenarioSdkScope sdk;
    if (!sdk.Scenario() || !sdk.Current()) return 0;
    CSOLScenarioWallCall call(*this, false, sdk.EntityAccess());
    if (!CCSOLScenarioBreakable::Save(save) || !call.Current()) return 0;
    const int result = save.WriteFields("CCSOLScenarioTarget", this, m_SaveData, ARRAYSIZE(m_SaveData));
    return call.Current() ? result : 0;
}

int CCSOLScenarioTarget::Restore(CRestore& restore)
{
    CSOLScenarioSdkScope sdk;
    if (!sdk.Scenario() || !sdk.Current()) return 0;
    ++m_serial;
    if (m_lifetime) ++*m_lifetime;
    m_damaging = false;
    CSOLScenarioWallCall call(*this, true, sdk.EntityAccess());
    if (!CBreakable::Restore(restore) || !call.Current()) return 0;
    if (!restore.ReadFields("CCSOLScenarioBreakable", this,
        CCSOLScenarioBreakable::m_SaveData, ARRAYSIZE(CCSOLScenarioBreakable::m_SaveData)) || !call.Current()) return 0;
    const int result = restore.ReadFields("CCSOLScenarioTarget", this, m_SaveData, ARRAYSIZE(m_SaveData));
    if (!call.Current()) return 0;
    m_valid = result && std::isfinite(m_playerRatio) && std::isfinite(m_monsterRatio);
    return m_valid ? result : 0;
}

bool CCSOLScenarioWalls::Collect(csol::scenario::ConfigError& error)
{
    const auto access = m_access;
    if (!access || !access->Allowed()) return false;
    error = {};
    if (m_busy || !g_engfuncs.pfnFindEntityByString)
    {
        error = {0, "scenario wall collection is busy or unavailable"};
        return false;
    }
    struct Busy { bool& value; Busy(bool& v) : value(v) { value = true; } ~Busy() { value = false; } } busy(m_busy);
    const auto generation = m_generation;
    std::vector<EntityHandle<CCSOLScenarioBreakable>> candidate;
    for (const char* classname : {"zbsbreak", "zbstarget"})
    {
        CBaseEntity* entity{};
        while (true)
        {
            const auto found = CSOL_LookupScenarioEntity(entity, classname, access);
            if (!access->Allowed() || m_generation != generation) { error = {0, "scenario map changed during wall collection"}; return false; }
            if (!found.valid) { error = {0, "scenario wall lookup was cancelled"}; return false; }
            entity = found.entity;
            if (!entity) break;
            auto* wall = dynamic_cast<CCSOLScenarioBreakable*>(entity);
            if (!wall || !wall->ValidConfiguration())
            {
                error = {0, "invalid scenario wall or target configuration"};
                return false;
            }
            candidate.emplace_back(wall);
        }
    }
    for (const auto& handle : candidate)
        if (auto* wall = handle.Get<CCSOLScenarioBreakable>()) wall->BindRules(m_rules);
    m_walls = std::move(candidate);
    return true;
}

bool CCSOLScenarioWalls::BindRules(std::shared_ptr<CCSOLScenarioWallRules> rules)
{
    if (!m_access || !m_access->Allowed() || m_busy || !rules) return false;
    if (m_rules && m_rules != rules) m_rules->Invalidate();
    m_rules = std::move(rules);
    m_rules->BindEntityAccess(m_access);
    for (const auto& handle : m_walls)
        if (auto* wall = handle.Get<CCSOLScenarioBreakable>()) wall->BindRules(m_rules);
    return true;
}

void CCSOLScenarioWalls::Reset()
{
    ++m_generation;
    if (m_rules) m_rules->Invalidate();
    for (const auto& handle : m_walls)
    {
        if (!m_access || !m_access->Allowed()) break;
        if (auto* wall = handle.Get<CCSOLScenarioBreakable>()) wall->BindRules(nullptr);
    }
    m_walls.clear();
    m_rules.reset();
}

void CCSOLScenarioWalls::CancelPending()
{
    // Session-owned round starts also invalidate delayed target work, even
    // when they do not go through native CleanUpMap's DelayedUse removal.
    if (m_rules) m_rules->Invalidate();
    for (const auto& handle : m_walls)
    {
        if (!m_access || !m_access->Allowed()) break;
        if (auto* wall = handle.Get<CCSOLScenarioBreakable>()) wall->BindRules(m_rules);
    }
}
