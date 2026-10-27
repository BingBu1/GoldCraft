// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_death_owner.h"

#include <utility>

CSOLMonsterDeathCall::CSOLMonsterDeathCall(CCSOLScenarioMap& map,
    CCSOLMonsterDeathRequests& requests, CCSOLScenarioMonster& monster,
    const CSOLMonsterDeathArguments& arguments)
    : m_map(map), m_requests(requests), m_mapGeneration(map.SpawnPoints().Generation()),
      m_roundGeneration(map.RoundGeneration()), m_requestGeneration(requests.Generation()),
      m_stages(monster, [](const void* context)
      {
          return static_cast<const CSOLMonsterDeathCall*>(context)->SameSession();
      }, this), m_arguments(arguments)
{
}

bool CSOLMonsterDeathCall::SameSession() const
{
    // Never dereference a retained edict until these owner generations match.
    return m_requestGeneration == m_requests.Generation() &&
        m_mapGeneration == m_map.SpawnPoints().Generation() &&
        m_roundGeneration == m_map.RoundGeneration() &&
        m_map.State() == CSOLScenarioMapState::RoundActive;
}

CCSOLScenarioMonster* CSOLMonsterDeathCall::Current() const
{
    if (!SameSession() || !m_requests.Error().empty()) return nullptr;
    return m_stages.Current();
}

CBaseEntity* CSOLMonsterDeathCall::Inflictor() const
{
    return Current() ? m_arguments.inflictor.Get<CBaseEntity>() : nullptr;
}

CBaseEntity* CSOLMonsterDeathCall::Attacker() const
{
    return Current() ? m_arguments.attacker.Get<CBaseEntity>() : nullptr;
}

CCSOLMonsterDeathOwner::CCSOLMonsterDeathOwner(CCSOLScenarioMap& map,
    CCSOLMonsterPool& pool, CCSOLMonsterModels& models,
    const CCSOLScenarioMessages& messages, CCSOLMonsterDeathServices& services, int mapId)
    : m_map(map), m_pool(pool), m_models(models), m_services(services), m_mapId(mapId),
      m_requests(*this), m_notice(map, m_requests, messages), m_targets(map, m_requests),
      m_rules(map, pool, m_requests)
{
}

bool CCSOLMonsterDeathOwner::Reset()
{
    if (!m_requests.Reset()) return false;
    m_error = {};
    return true;
}

bool CCSOLMonsterDeathOwner::ExecuteDeath(CCSOLScenarioMonster& monster,
    const CSOLMonsterDeathArguments& arguments)
{
    if (!m_error.message.empty()) return false;
    if (monster.Pool() != &m_pool || !m_map.Tables() ||
        m_map.Tables()->scenarioType != 1 || m_mapId <= 0)
    {
        m_error.message = "scenario death owner requires the selected mode15/subtype1 map and pool";
        return false;
    }
    CSOLMonsterDeathCall call(m_map, m_requests, monster, arguments);
    csol::scenario::ConfigError error;
    const auto step = [&](bool success, const char* reason)
    {
        // A reset may already have started a fresh owner's error state. Never
        // publish an old callback's failure into that replacement session.
        if (!call.SameSession()) return false;
        if (success && call.Current() && m_error.message.empty()) return true;
        if (m_error.message.empty())
            m_error = error.message.empty() ? csol::scenario::ConfigError{0, reason} : error;
        return false;
    };
    if (!step(call.Current() != nullptr, "scenario death capture is no longer current")) return false;
    const auto run = [&]() -> bool
    {
        // Capture at actual execution, not when a deferred request was queued.
        auto initial = std::dynamic_pointer_cast<CCSOLMonsterDeathBehavior>(monster.RetainBehavior());
        if (!step(initial != nullptr, "scenario monster has no concrete death behavior")) return false;
        if (!step(m_notice.Send(call.m_stages, error), "scenario death notification failed")) return false;
        if (!step(initial->BeforeDeath(call, error), "scenario beforeDeath failed")) return false;
        if (!step(call.m_stages.BeginAnimation(), "scenario death animation state failed")) return false;
        auto sequenceBehavior = std::dynamic_pointer_cast<CCSOLMonsterDeathBehavior>(call.Current()->RetainBehavior());
        if (!step(sequenceBehavior != nullptr, "scenario current death behavior is unavailable")) return false;
        int sequence{};
        if (!step(sequenceBehavior->SelectDeathSequence(call, sequence, error), "scenario death sequence failed")) return false;
        if (!step(call.m_stages.FinishAnimation(call.Current()->Animation(), sequence),
            "scenario death animation failed")) return false;
        if (!step(call.m_stages.ReturnPointAndSound(m_map.SpawnPoints().Points(), m_models,
            call.SuppressSound()), "scenario death point or sound failed")) return false;

        auto moneyBehavior = std::dynamic_pointer_cast<CCSOLMonsterDeathBehavior>(call.Current()->RetainBehavior());
        if (!step(moneyBehavior != nullptr, "scenario money attribute owner is unavailable")) return false;
        if (call.Attacker())
        {
            // Original mode15 grenade kills skip this immediate money branch.
            // Drop rewards and the later player-kill branch still run normally.
            const auto* inflictor = call.Inflictor();
            if (!inflictor || !FClassnameIs(inflictor->pev, "grenade"))
            {
                const auto money = moneyBehavior->DeathParameters().bonusMoney;
                if (!step(true, "scenario changed while reading money attributes")) return false;
                if (!step(m_services.AwardKillMoney(call, money, error), "scenario kill money failed")) return false;
            }
        }
        if (!step(call.m_stages.MarkDead(), "scenario dead state failed")) return false;
        const auto* appearance = csol::scenario::FindMonsterAppearance(call.Current()->Type()->appearance);
        if (!step(appearance != nullptr, "scenario death appearance is unavailable")) return false;
        float seconds = appearance->deathSeconds;
        auto durationBehavior = std::dynamic_pointer_cast<CCSOLMonsterDeathBehavior>(call.Current()->RetainBehavior());
        if (!step(durationBehavior != nullptr, "scenario death-duration behavior is unavailable")) return false;
        if (!step(durationBehavior->AdjustDeathDuration(call, seconds, error), "scenario death duration failed")) return false;
        auto timingBehavior = std::dynamic_pointer_cast<CCSOLMonsterDeathBehavior>(call.Current()->RetainBehavior());
        if (!step(timingBehavior != nullptr, "scenario timer attribute owner is unavailable")) return false;
        const auto parameters = timingBehavior->DeathParameters();
        if (!step(true, "scenario changed while reading death timer attributes")) return false;
        if (!step(call.m_stages.ScheduleReturn(seconds, parameters.shortDeath, 1,
            parameters.slotSkill, m_mapId), "scenario death return timer failed")) return false;

        // RulesDeath is called even for an environmental death with no attacker.
        if (!step(m_services.RulesDeath(call, error), "scenario rules death callback failed")) return false;
        if (call.Attacker() && !step(m_services.PlayerKilledMonster(call, error),
            "scenario player kill services failed")) return false;
        if (!step(m_services.ClearEffects(call, error), "scenario death effect cleanup failed")) return false;
        if (!step(m_targets.Fire(call.m_stages, error), "scenario death targets failed")) return false;
        // Use callbacks may have replaced the type. Do not cache it before Fire.
        const int actualType = call.Current()->TypeId();
        if (!step(m_rules.ApplyRule(actualType, m_services.DrainTeamFilter(), error),
            "scenario death table failed")) return false;
        if (!step(m_services.GoldenDeath(call, error), "scenario Golden death cleanup failed")) return false;
        // Keep the entry behavior alive even if another callback installed a new
        // behavior. Sequence and duration intentionally used the current one.
        return step(initial->PostDeath(call, error), "scenario postDeath failed");
    };
    // Releasing the retained behaviors can itself invoke engine callbacks.
    // Check again after all releases, including the early failure paths.
    const bool completed = run();
    return step(completed, "scenario changed while releasing death behaviors");
}
