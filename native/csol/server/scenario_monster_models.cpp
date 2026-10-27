// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_monster_models.h"
#include "scenario_monster_pool.h"

namespace
{
struct Operation
{
    bool& busy;
    explicit Operation(bool& value) : busy(value) { busy = true; }
    ~Operation() { busy = false; }
};

bool Fail(csol::scenario::ConfigError& error, const char* reason)
{
    error = {0, reason};
    return false;
}
}

bool CCSOLMonsterModels::Reset()
{
    if (m_busy) return false;
    m_indices = {};
    m_deathSounds = {};
    m_ready = false;
    return true;
}

bool CCSOLMonsterModels::Precache(const csol::scenario::MonsterTypeTable& types,
    csol::scenario::ConfigError& error)
{
    error = {};
    if (m_busy) return Fail(error, "scenario monster model operation is already active");
    Reset();
    Operation operation(m_busy);
    std::array<bool, csol::scenario::MonsterAppearanceCount> needed{};
    // Validate every effective type before the first irreversible engine call.
    for (const auto& [key, row] : types.byId)
    {
        if (row >= types.rows.size() || types.rows[row].Key() != key)
            return Fail(error, "scenario monster type index is invalid");
        const auto* appearance = csol::scenario::FindMonsterAppearance(types.rows[row].appearance);
        if (!appearance) return Fail(error, "scenario monster appearance is not in the original catalog");
        needed[appearance->index] = true;
    }
    const auto catalog = csol::scenario::MonsterAppearances();
    std::array<int, csol::scenario::MonsterAppearanceCount> candidate{};
    std::array<bool, csol::scenario::MonsterAppearanceCount> deathSounds{};
    for (std::size_t i = 0; i < needed.size(); ++i)
    {
        if (!needed[i]) continue;
        // Duplicate catalog names/models retain their original indexes but
        // share one engine model entry. No per-spawn precache is performed.
        for (std::size_t previous = 0; previous < i; ++previous)
            if (candidate[previous] && catalog[previous].model == catalog[i].model)
            {
                candidate[i] = candidate[previous];
                break;
            }
        if (!candidate[i])
            candidate[i] = PRECACHE_MODEL(const_cast<char*>(catalog[i].model.data()));
        if (candidate[i] <= 0) return Fail(error, "scenario monster model precache failed");
        if (catalog[i].deathSound.empty()) continue;
        for (std::size_t previous = 0; previous < i; ++previous)
            if (deathSounds[previous] && catalog[previous].deathSound == catalog[i].deathSound)
            {
                deathSounds[i] = true;
                break;
            }
        if (!deathSounds[i])
        {
            if (!g_engfuncs.pfnPrecacheSound ||
                PRECACHE_SOUND(const_cast<char*>(catalog[i].deathSound.data())) <= 0)
                return Fail(error, "scenario monster death sound precache failed");
            deathSounds[i] = true;
        }
    }
    m_indices = candidate;
    m_deathSounds = deathSounds;
    m_ready = true;
    return true;
}

int CCSOLMonsterModels::ModelIndex(const csol::scenario::MonsterAppearance& appearance) const
{
    if (!m_ready || appearance.index >= m_indices.size() ||
        &csol::scenario::MonsterAppearances()[appearance.index] != &appearance) return 0;
    return m_indices[appearance.index];
}

bool CCSOLMonsterModels::EmitDeathSound(CCSOLScenarioMonster& monster,
    const csol::scenario::MonsterAppearance& appearance) const
{
    if (!ModelIndex(appearance)) return false;
    if (appearance.deathSound.empty()) return true;
    if (!m_deathSounds[appearance.index] || !g_engfuncs.pfnEmitSound) return false;
    EMIT_SOUND_DYN(monster.edict(), CHAN_BODY, appearance.deathSound.data(),
        1.0f, ATTN_NORM, 0, PITCH_NORM);
    return true;
}

bool CCSOLMonsterModels::Apply(CCSOLScenarioMonster& monster,
    const csol::scenario::RoundMonsterDeltas& delta, csol::scenario::MonsterAttributes& output,
    csol::scenario::ConfigError& error, bool (*sessionCheck)(const void*), const void* sessionContext)
{
    error = {};
    if (sessionCheck && !sessionCheck(sessionContext))
        return Fail(error, "scenario changed before monster model setup");
    if (!g_engfuncs.pfnSetModel || !g_engfuncs.pfnSetSize)
        return Fail(error, "scenario monster model or bounds service is unavailable");
    auto* pool = monster.Pool();
    if (m_busy || !m_ready || !pool || pool->Busy() || !pool->Borrowed(monster) ||
        monster.m_behaviorReady || monster.pev->takedamage != DAMAGE_NO ||
        monster.pev->deadflag != DEAD_NO || !monster.Type())
        return Fail(error, "scenario monster model requires a newly acquired inactive slot");
    const auto* appearance = csol::scenario::FindMonsterAppearance(monster.Type()->appearance);
    if (!appearance || !ModelIndex(*appearance))
        return Fail(error, "scenario monster appearance was not precached for this map");
    csol::scenario::MonsterAttributes attributes;
    if (!csol::scenario::ResolveMonsterAttributes(*monster.Type(), delta, attributes, error)) return false;

    Operation modelsOperation(m_busy);
    Operation poolOperation(pool->m_busy);
    const auto slot = monster.Slot();
    // Holding the pool operation excludes Free/Acquire during engine hooks.
    // OnDestroy can still invalidate the handle; re-resolve after each call.
    monster.pev->health = monster.pev->max_health = attributes.health;
    monster.pev->armorvalue = attributes.armor;
    monster.pev->maxspeed = attributes.speed;
    monster.SpeedState().baseSpeed = attributes.speed;
    // mode15's round-attribute stage tests the case-sensitive model path,
    // not the monster type or a guessed animation availability flag.
    monster.HostState().flinchEnabled = appearance->model.find("deimos") == std::string_view::npos &&
        appearance->model.find("zbs_bossS_jun01") == std::string_view::npos;
    SET_MODEL(monster.edict(), appearance->model.data());
    if (sessionCheck && !sessionCheck(sessionContext))
        return Fail(error, "scenario changed during monster model setup");
    auto* current = pool->At(slot);
    if (current != &monster || !current || !pool->Borrowed(*current))
        return Fail(error, "scenario monster was invalidated during model setup");
    const Vector mins(appearance->mins.data());
    const Vector maxs(appearance->maxs.data());
    SET_SIZE(current->edict(), mins, maxs);
    if (sessionCheck && !sessionCheck(sessionContext))
        return Fail(error, "scenario changed during monster bounds setup");
    current = pool->At(slot);
    if (current != &monster || !current || !pool->Borrowed(*current))
        return Fail(error, "scenario monster was invalidated during bounds setup");
    output = attributes;
    return true;
}
