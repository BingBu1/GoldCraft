// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_monster_rewards.h"

#include <limits>

namespace
{
CBaseEntity* Live(const EHANDLE& handle)
{
    auto* entity = handle.Get<CBaseEntity>();
    return entity && !(entity->pev->flags & FL_KILLME) ? entity : nullptr;
}
bool Active(CSOLMonsterSpawnCall& call)
{
    auto* monster = call.Current();
    return monster && monster->Pool()->Borrowed(*monster) &&
        !(monster->pev->flags & FL_KILLME) && !(monster->pev->effects & EF_NODRAW) &&
        monster->pev->takedamage != DAMAGE_NO && monster->pev->deadflag == DEAD_NO;
}
}

std::shared_ptr<CCSOLMonsterHitRewards> CCSOLMonsterHitRewards::Create(
    const csol::scenario::DifficultyResources& difficulty,
    std::shared_ptr<CCSOLScenarioPlayers> players, csol::scenario::ConfigError& error)
{
    error = {};
    if (!players) { error.message = "scenario monster rewards require player upgrades"; return {}; }
    auto rewards = std::make_shared<CCSOLMonsterHitRewards>();
    if (!csol::scenario::ReadMonsterHitRewardSettings(difficulty, rewards->m_settings, error)) return {};
    rewards->m_players = std::move(players);
    return rewards;
}

CSOLMonsterRewardResult CCSOLMonsterHitRewards::Fail(const char* reason)
{
    if (m_error.message.empty()) m_error.message = reason;
    return CSOLMonsterRewardResult::Failed;
}

CSOLMonsterRewardResult CCSOLMonsterHitRewards::Credit(CSOLMonsterSpawnCall& call,
    const EHANDLE& player, const EHANDLE& inflictor, float damage, bool moneyExtractor, int roomOption,
    bool (*ownerValid)(const void*), const void* ownerContext)
{
    auto keepAlive = shared_from_this();
    const auto active = [&]()
    {
        return m_valid && (!ownerValid || ownerValid(ownerContext)) && Active(call);
    };
    if (!active() || m_busy) return CSOLMonsterRewardResult::Cancelled;
    if (!m_error.message.empty()) return CSOLMonsterRewardResult::Failed;
    auto* attacker = dynamic_cast<CBasePlayer*>(Live(player));
    auto* source = Live(inflictor);
    // Original validity tests are not IsAlive: an in-flight projectile can
    // still credit its valid player owner after the owner has died.
    if (!attacker || !source) return CSOLMonsterRewardResult::Skipped;
    if (!gpGlobals || !gpGlobals->pStringBase) return Fail("scenario reward string table is unavailable");
    if (FClassnameIs(source->pev, "grenade")) return CSOLMonsterRewardResult::Skipped;

    m_busy = true;
    struct Finish { bool& busy; ~Finish() { busy = false; } } finish{m_busy};
    const auto state = m_players->Upgrades(player);
    if (!active()) return CSOLMonsterRewardResult::Cancelled;
    if (!state) return Fail("scenario monster attacker has no authoritative upgrade state");
    const auto current = [&]()
    {
        // Check the map/round/lease before inspecting handles from that map.
        if (!active() || !state->Current() || !active()) return false;
        const auto registered = m_players->Upgrades(player);
        return registered == state && state->Current() && active() &&
            Live(player) && Live(inflictor);
    };
    if (!current()) return CSOLMonsterRewardResult::Cancelled;
    csol::scenario::MonsterHitRewardArguments arguments;
    if (!csol::scenario::PrepareMonsterHitReward(m_settings, damage, arguments))
        return Fail("scenario monster hit reward is nonfinite");
    if (!g_engfuncs.pfnRandomFloat) return Fail("scenario monster reward RNG is unavailable");
    const float randomValue = RANDOM_FLOAT(arguments.randomFirst, arguments.randomSecond);
    if (!current()) return CSOLMonsterRewardResult::Cancelled;
    std::int32_t money{};
    if (!csol::scenario::ResolveMonsterHitMoney(randomValue, moneyExtractor, roomOption, money))
        return Fail("scenario monster reward RNG returned a nonfinite value");

    // MP 11386C37..11386CA1: draw money first, award XP, then money. Unlike
    // wall rewards, this path does NOT invoke the addon money modifier v250.
    const bool experienced = state->AddExperience(arguments.experience);
    if (!current()) return CSOLMonsterRewardResult::Cancelled;
    if (!experienced) return Fail("scenario monster hit experience consumer failed");
    attacker = dynamic_cast<CBasePlayer*>(Live(player));
    if (!attacker) return CSOLMonsterRewardResult::Cancelled;
    const auto balance = static_cast<std::int64_t>(attacker->m_iAccount) + money;
    if (balance < std::numeric_limits<int>::min() || balance > std::numeric_limits<int>::max())
        return Fail("scenario monster reward would overflow the native account");
    // Native trackChange controls the HUD delta, not CSO's deferred send.
    // Send one clamped balance; the native client derives the actual change.
    attacker->AddAccount(money, RT_NONE, true);
    return current() ? CSOLMonsterRewardResult::Awarded : CSOLMonsterRewardResult::Cancelled;
}
