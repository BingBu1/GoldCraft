// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_wall_rewards.h"
#include "scenario_breakable_guard.h"

#include <bit>
#include <limits>

namespace
{
bool CurrentPlayer(const std::shared_ptr<CSOLScenarioWallPlayer>& state,
    std::uint64_t generation, const EHANDLE& player)
{
    if (!state || state->generation != generation || !CSOLBreakableScope::Current(nullptr)) return false;
    auto* current = state->player.Get<CBaseEntity>();
    return current && current == player.Get<CBaseEntity>() && !(current->pev->flags & FL_KILLME);
}
}

std::shared_ptr<CCSOLScenarioWallRewards> CCSOLScenarioWallRewards::Create(
    const csol::scenario::RoundTable& rounds, const csol::scenario::DifficultyResources& difficulty,
    std::shared_ptr<CCSOLScenarioWallPlayers> players, csol::scenario::ConfigError& error)
{
    error = {};
    if (!players) { error.message = "scenario wall rewards require player and upgrade services"; return {}; }
    auto rules = std::make_shared<CCSOLScenarioWallRewards>();
    if (!csol::scenario::ReadWallRewardSettings(rounds, difficulty, rules->m_settings, error)) return {};
    rules->m_players = std::move(players);
    return rules;
}

bool CCSOLScenarioWallRewards::Fail(const char* reason)
{
    if (m_error.message.empty()) m_error.message = reason;
    return false;
}

bool CCSOLScenarioWallRewards::PlayerDamage(const EHANDLE& player, float input, float ratio, float& output)
{
    if (!m_error.message.empty() || !CSOLBreakableScope::Current(nullptr)) return false;
    const auto generation = Generation();
    auto state = m_players->Find(player);
    if (Generation() != generation || !CSOLBreakableScope::Current(nullptr)) return false;
    if (!state || !CurrentPlayer(state, state->generation, player))
        return Fail("scenario wall attacker has no authoritative upgrade state");
    return csol::scenario::ResolvePlayerWallDamage(input, ratio, state->attackLevel,
        state->elevenLevelLimit, state->characterFeature, output);
}

bool CCSOLScenarioWallRewards::MonsterDamage(float input, float ratio, float& output)
{
    return m_error.message.empty() && csol::scenario::ResolveMonsterWallDamage(input, ratio, m_monsterLevel, output);
}

bool CCSOLScenarioWallRewards::CreditPlayerDamage(const EHANDLE& player, float scaledDamage)
{
    if (!m_error.message.empty() || !CSOLBreakableScope::Current(nullptr)) return false;
    const auto generation = Generation();
    auto state = m_players->Find(player);
    if (Generation() != generation || !CSOLBreakableScope::Current(nullptr)) return false;
    if (!state) return Fail("scenario wall reward player state is missing");
    const auto playerGeneration = state->generation;
    const auto current = [&]()
    {
        return Generation() == generation && CurrentPlayer(state, playerGeneration, player);
    };
    if (!current() || state->crediting) return false;
    state->crediting = true;
    struct Finish
    {
        std::shared_ptr<CSOLScenarioWallPlayer> state;
        std::uint64_t generation;
        ~Finish() { if (state->generation == generation) state->crediting = false; }
    } finish{state, playerGeneration};
    if (!csol::scenario::AccumulateWallDamage(state->accumulatedDamage, scaledDamage, state->accumulatedDamage))
        return Fail("scenario wall damage accumulation is nonfinite");
    while (state->accumulatedDamage >= m_settings.threshold)
    {
        int money{}, experience{};
        if (!csol::scenario::ScaleWallReward(m_settings.money, m_settings.moneyRatio, money) ||
            !csol::scenario::ScaleWallReward(m_settings.experience, m_settings.experienceRatio, experience))
            return Fail("scenario wall reward scaling is nonfinite");
        const bool modified = m_players->ModifyMoney(player, money);
        if (!current()) return false;
        if (!modified) return Fail("scenario wall character money modifier failed");
        auto* entity = dynamic_cast<CBasePlayer*>(player.Get<CBaseEntity>());
        if (!entity) return Fail("scenario wall reward target is not a player");
        const auto balance = static_cast<std::int64_t>(entity->m_iAccount) + money;
        if (balance < std::numeric_limits<int>::min() || balance > std::numeric_limits<int>::max())
            return Fail("scenario wall reward would overflow the native account");
        // Each earned threshold sends its clamped balance and native HUD delta.
        entity->AddAccount(money, RT_NONE, true);
        if (!current()) return false;
        // MP 11398349..113983C1: money, signed16 threshold subtraction,
        // then XP. Keep this order for reentrant account and upgrade hooks.
        state->accumulatedDamage = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(
            static_cast<int>(state->accumulatedDamage) - m_settings.threshold));
        const bool awarded = m_players->AddExperience(player, experience);
        if (!current()) return false;
        if (!awarded) return Fail("scenario wall experience consumer failed");
    }
    return true;
}
