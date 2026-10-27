// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_death_requests.h"
#include <functional>

class CSOLMonsterSpawnCall;
class CSOLMonsterDeathCall;

// A caller supplies its session/round/lease transaction, including death's
// early parked state. This must not be replaced by a generic alive test.
struct CSOLMonsterEffectGuard
{
    CCSOLScenarioMonster* (*resolve)(const void*);
    const void* context;
    CCSOLScenarioMonster* Current() const { return resolve ? resolve(context) : nullptr; }
};

struct CSOLMonsterDamageEffect
{
    EHANDLE source;
    int count{};
    float damage{};
    std::uint8_t flag{};
    int hitgroup{-1};
    Vector localOffset{};
    int sourceId{};
    // Notification and destruction are distinct. Holding the object keeps
    // a callback alive if it causes engine private data to be released.
    std::shared_ptr<std::function<void()>> notify;
};

class CCSOLMonsterEffects
{
public:
    static constexpr std::size_t Slots = 33;
    const CSOLMonsterDamageEffect& At(std::size_t index) const { return m_slots.at(index); }
    bool Install(const CSOLMonsterEffectGuard& guard, entvars_t* source, float damage,
        int count, std::uint8_t flag, int sourceId, int hitgroup,
        const Vector& worldOffset, std::function<void()> notify);
    bool ClearSource(const CSOLMonsterEffectGuard& guard, entvars_t* source, bool& exhausted);
    bool ClearAll(const CSOLMonsterEffectGuard& guard);
    bool Tick(CSOLMonsterSpawnCall& call, const CSOLMonsterEffectGuard& guard,
        csol::scenario::ConfigError& error);
private:
    CCSOLScenarioMonster* Current(const CSOLMonsterEffectGuard& guard) const;
    int SourceIndex(const CSOLMonsterEffectGuard& guard, const EHANDLE& source) const;
    bool Clear(const CSOLMonsterEffectGuard& guard, std::size_t index);
    std::array<CSOLMonsterDamageEffect, Slots> m_slots;
    std::array<std::weak_ptr<std::function<void()>>, Slots> m_notifying;
    bool m_ticking{};
};

bool CSOL_ClearMonsterEffects(CSOLMonsterDeathCall& call);

// Consumed once at the actual pool TakeDamage entry, before behavior/provider
// callbacks. Normal SDK damage has the default context; unrelated targets,
// inflictors, attackers, leases and recursive calls cannot inherit this one.
CSOLMonsterDeathContext CSOL_ConsumeMonsterDamageContext(CCSOLScenarioMonster& monster,
    entvars_t* inflictor, entvars_t* attacker, int bits);
