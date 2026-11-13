// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_monster_rewards.h"
#include "../../custom/damage_refund_account.h"

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
    std::shared_ptr<CCSOLScenarioPlayers> players, csol::scenario::ConfigError& error,
    std::shared_ptr<csol::scenario::custom::RefundCap> refunds)
{
    error = {};
    if (!players) { error.message = "scenario monster rewards require player upgrades"; return {}; }
    auto rewards = std::make_shared<CCSOLMonsterHitRewards>();
    if (!csol::scenario::ReadMonsterHitRewardSettings(difficulty, rewards->m_settings, error)) return {};
    rewards->m_players = std::move(players);
    rewards->m_refunds = std::move(refunds);
    return rewards;
}

CSOLMonsterRewardResult CCSOLMonsterHitRewards::Fail(const char* reason)
{
    if (m_error.message.empty()) m_error.message = reason;
    return CSOLMonsterRewardResult::Failed;
}

namespace
{
struct MonsterHitDraw
{
    float damage{};
    bool moneyExtractor{};
    int roomOption{};
    const char* failure{};
};
bool DrawMonsterHit(const csol::scenario::MonsterHitRewardSettings& settings, void* context,
    std::int32_t& experience, std::int32_t& money)
{
    auto& draw = *static_cast<MonsterHitDraw*>(context);
    csol::scenario::MonsterHitRewardArguments arguments;
    if (!csol::scenario::PrepareMonsterHitReward(settings, draw.damage, arguments))
    { draw.failure = "scenario monster hit reward is nonfinite"; return false; }
    if (!g_engfuncs.pfnRandomFloat) { draw.failure = "scenario monster reward RNG is unavailable"; return false; }
    const float randomValue = RANDOM_FLOAT(arguments.randomFirst, arguments.randomSecond);
    if (!csol::scenario::ResolveMonsterHitMoney(randomValue, draw.moneyExtractor, draw.roomOption, money))
    { draw.failure = "scenario monster reward RNG returned a nonfinite value"; return false; }
    experience = arguments.experience;
    return true;
}
struct MonsterHitActive
{
    CSOLMonsterSpawnCall* call{};
    bool (*ownerValid)(const void*){};
    const void* ownerContext{};
};
bool MonsterHitIsActive(const void* context)
{
    const auto& active = *static_cast<const MonsterHitActive*>(context);
    return (!active.ownerValid || active.ownerValid(active.ownerContext)) && Active(*active.call);
}
struct EntityHitActive
{
    bool (*ownerValid)(const void*){};
    const void* ownerContext{};
};
bool EntityHitIsActive(const void* context)
{
    const auto& active = *static_cast<const EntityHitActive*>(context);
    return active.ownerValid && active.ownerValid(active.ownerContext);
}
}

CSOLMonsterRewardResult CCSOLMonsterHitRewards::Credit(CSOLMonsterSpawnCall& call,
    const EHANDLE& player, const EHANDLE& inflictor, float damage, bool moneyExtractor, int roomOption,
    bool (*ownerValid)(const void*), const void* ownerContext,
    csol::scenario::custom::RefundPlayer refundPlayer)
{
    MonsterHitActive active{&call, ownerValid, ownerContext};
    MonsterHitDraw draw{damage, moneyExtractor, roomOption};
    csol::scenario::custom::RefundPair pair;
    bool refundable{};
    if (m_refunds && m_valid && MonsterHitIsActive(&active) && dynamic_cast<CBasePlayer*>(Live(player)))
    {
        const auto* monster = call.Current();
        if (!monster) return CSOLMonsterRewardResult::Cancelled;
        pair = {monster->Slot(), monster->Pool()->Lease(*monster), refundPlayer};
        refundable = true;
    }
    const auto result = Award(&MonsterHitIsActive, &active, player, inflictor, &DrawMonsterHit, &draw,
        refundable ? &pair : nullptr);
    if (result == CSOLMonsterRewardResult::Failed && draw.failure) return Fail(draw.failure);
    return result;
}

CSOLMonsterRewardResult CCSOLMonsterHitRewards::CreditEntityHit(const EHANDLE& player,
    const EHANDLE& inflictor, EntityHitDraw draw, void* drawContext,
    bool (*ownerValid)(const void*), const void* ownerContext)
{
    if (!draw || !ownerValid) return Fail("scenario entity hit reward requires a draw and an owner gate");
    EntityHitActive active{ownerValid, ownerContext};
    const auto result = Award(&EntityHitIsActive, &active, player, inflictor, draw, drawContext);
    if (result == CSOLMonsterRewardResult::Failed && m_error.message.empty())
        return Fail("scenario entity hit reward draw failed");
    return result;
}

CSOLMonsterRewardResult CCSOLMonsterHitRewards::Award(bool (*isActive)(const void*), const void* activeContext,
    const EHANDLE& player, const EHANDLE& inflictor, EntityHitDraw draw, void* drawContext,
    const csol::scenario::custom::RefundPair* refundPair)
{
    auto keepAlive = shared_from_this();
    const auto active = [&]() { return m_valid && isActive(activeContext); };
    if (!active() || m_busy) return CSOLMonsterRewardResult::Cancelled;
    if (!m_error.message.empty()) return CSOLMonsterRewardResult::Failed;
    auto* attacker = dynamic_cast<CBasePlayer*>(Live(player));
    auto* source = Live(inflictor);
    // Original validity tests are not IsAlive: an in-flight projectile can
    // still credit its valid player owner after the owner has died.
    if (!attacker || !source) return CSOLMonsterRewardResult::Skipped;
    if (!gpGlobals || !gpGlobals->pStringBase) return Fail("scenario reward string table is unavailable");
    if (FClassnameIs(source->pev, "grenade")) return CSOLMonsterRewardResult::Skipped;
    if (refundPair && !m_refunds->Remaining(*refundPair)) return Fail("scenario refund identity is unavailable");

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
    // The draw (one RANDOM_FLOAT) precedes XP and money, as MP 11386C37 and
    // 109886A1 do. An engine callback may end this map: recheck afterwards.
    std::int32_t experience{}, money{};
    const bool drawn = draw(m_settings, drawContext, experience, money);
    if (!current()) return CSOLMonsterRewardResult::Cancelled;
    if (!drawn) return CSOLMonsterRewardResult::Failed;

    // MP 11386C37..11386CA1: draw money first, award XP, then money. Unlike
    // wall rewards, this path does NOT invoke the addon money modifier v250.
    const bool experienced = state->AddExperience(experience);
    if (!current()) return CSOLMonsterRewardResult::Cancelled;
    if (!experienced) return Fail("scenario monster hit experience consumer failed");
    attacker = dynamic_cast<CBasePlayer*>(Live(player));
    if (!attacker) return CSOLMonsterRewardResult::Cancelled;
    const auto balance = static_cast<std::int64_t>(attacker->m_iAccount) + money;
    if (balance < std::numeric_limits<int>::min() || balance > std::numeric_limits<int>::max())
        return Fail("scenario monster reward would overflow the native account");
    // Native trackChange controls the HUD delta, not CSO's deferred send.
    // Send one clamped balance; the native client derives the actual change.
    csol::scenario::custom::RefundAwardScope refund(refundPair ? m_refunds.get() : nullptr,
        refundPair ? *refundPair : csol::scenario::custom::RefundPair{}, attacker,
        [](const void* context) { return (*static_cast<const decltype(current)*>(context))(); }, &current);
    attacker->AddAccount(money, RT_NONE, true);
    return current() ? CSOLMonsterRewardResult::Awarded : CSOLMonsterRewardResult::Cancelled;
}
