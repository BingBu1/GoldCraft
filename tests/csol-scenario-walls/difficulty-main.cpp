// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
#include <array>
#include <limits>

namespace wall_test
{
std::shared_ptr<CSOLScenarioEntityAccess> NewDifficultyMap();
namespace
{
using csol::scenario::ConfigError;
using csol::scenario::DifficultyResources;

struct Tables
{
    difficulty_test::Files files;
    csol::scenario::RoundTable rounds;
    DifficultyResources loaded;
    Tables()
    {
        rounds.source.records.resize(5);
        rounds.source.records[2].fields = {"threshold", "125"};
        rounds.source.records[3].fields = {"money", "200"};
        rounds.source.records[4].fields = {"experience", "50"};
        std::string csv = "header\r\n";
        for (int row = 1; row <= 2; ++row)
        {
            csv += "99,physical-row";
            for (int column = 2; column <= 21; ++column)
            {
                // Deliberately distinct STR_HP, STR_EXP and STR_MONEY; all
                // other columns poison accidental column substitutions.
                const float value = column == 19 ? (row == 1 ? 2.f : 5.f) :
                    column == 20 ? (row == 1 ? 1.25f : 3.f) :
                    column == 21 ? (row == 1 ? .75f : 2.f) : 91.f;
                csv += "," + std::to_string(value);
            }
            csv += "\r\n";
        }
        files.contents[difficulty_test::DefaultPath] = difficulty_test::Encrypt(csv);
        csol::scenario::ResourceError error;
        Require(loaded.BeginMap("wall_difficulty", 1, files.Api(), error), "actual difficulty source loads");
    }
    DifficultyResources Select(std::uint16_t row)
    {
        DifficultyResources selected; ConfigError error;
        Require(loaded.PrepareSelection(row, selected, error), "actual difficulty selection succeeds");
        return selected;
    }
};

void RoundDifficultyCases()
{
    Engine e; const auto access = NewDifficultyMap(); Tables tables;
    auto& player = e.New<Player>("player"); player.pev->flags |= FL_CLIENT;
    player.m_iAccount = 7500;
    auto& monster = e.New<Actor>("test_monster");
    auto& human = e.Wall("1", "0");
    auto& zombie = e.Wall("0", "1", 700);
    auto& mixed = e.Wall("1", "1", 800);
    auto& touch = e.Wall("0", "0", 900);
    auto& negative = e.Wall("-1", "0", 600);
    auto& target = e.New<CCSOLScenarioTarget>("zbstarget");
    target.pev->model = Engine::String("*2"); target.pev->health = 123;
    target.pev->max_health = 777;
    Engine::KV(target, "hmdmgratio", "1"); Engine::KV(target, "zbdmgratio", "0");
    target.Spawn();
    auto& ordinary = e.New<CBreakable>("func_breakable");
    ordinary.pev->model = Engine::String("*3"); ordinary.pev->health = 333; ordinary.Spawn();
    human.pev->max_health = 1111; zombie.pev->max_health = 2222;

    CCSOLScenarioWalls walls; walls.BindEntityAccess(access); ConfigError error;
    Require(walls.Collect(error) && walls.Count() == 6, "actual collection excludes ordinary func_breakable");
    auto players = std::make_shared<Players>();
    players->state->player = &player; players->state->generation = 1; players->state->attackLevel = 1;
    std::shared_ptr<CCSOLScenarioWallRewards> active;
    const auto bind = [&](const DifficultyResources& selected)
    {
        const auto previous = active;
        const auto generation = previous ? previous->Generation() : 0;
        active = CCSOLScenarioWallRewards::Create(tables.rounds, selected, players, error);
        Require(bool(active) && walls.BindRules(active), "new wall reward owner binds to real entities");
        if (previous) Check(previous->Generation() != generation, "replay retires previous reward generation");
    };
    const auto cleanAndScale = [&](const DifficultyResources& selected, std::uint64_t round)
    {
        walls.CancelPending();
        // This is the real SDK scanner used by CleanUpMap for FCAP_MUST_RESET.
        // Player respawn and the remainder of Gate/CleanUpMap are outside this test.
        UTIL_ResetEntities();
        Check(human.pev->health == 1000 && zombie.pev->health == 700 &&
            mixed.pev->health == 800 && touch.pev->health == 900 && negative.pev->health == 600,
            "actual native Restart restores each saved map health before difficulty");
        Check(target.pev->health == target.pev->max_health && target.m_flHealth == 123,
            "target Restart uses current max_health independently of saved breakable health");
        float multiplier{};
        Require(selected.Parameter(15, 19, multiplier, error) &&
            walls.ApplyRoundDifficulty(multiplier, round, error), "post-Restart STR_HP applies");
        Check(human.pev->health == 1000 * multiplier && human.m_flHealth == 1000 &&
            human.pev->max_health == 1111, "only current health scales; both restart baselines stay original");
        Check(zombie.pev->health == 700 && zombie.pev->max_health == 2222 && mixed.pev->health == 800 &&
            touch.pev->health == 900 && negative.pev->health == 600 && target.pev->health == target.pev->max_health &&
            ordinary.pev->health == 333, "monster/mixed/touch/negative/target/ordinary walls do not receive STR_HP");
    };
    const auto hit = [&](int money, int experience)
    {
        const int before = player.m_iAccount; const auto xpBefore = players->xp.size();
        const auto messagesBefore = e.messages.size(); const float health = human.pev->health;
        std::vector<std::string> order;
        players->callback = [&](std::string_view step)
        {
            if (step == "modify" || step == "xp") order.emplace_back(step);
            if (step == "xp") Check(players->state->accumulatedDamage == 0 && human.pev->health == health,
                "threshold subtraction precedes XP and native health mutation");
        };
        e.boundary = [&](std::string_view step, CBaseEntity*)
        {
            if (step == "message-end" && e.messages.back().type == gmsgMoney)
            {
                order.emplace_back("money");
                Check(players->state->accumulatedDamage == 125 && human.pev->health == health,
                    "real Money message precedes threshold subtraction and native damage");
            }
        };
        Require(human.TakeDamage(player.pev, player.pev, 125, DMG_BULLET) != FALSE, "actual player hit accepted");
        e.boundary = {}; players->callback = {};
        Check(human.pev->health == health - 125 && player.m_iAccount == before + money &&
            players->xp.size() == xpBefore + 1 && players->xp.back() == experience,
            "selected STR_MONEY and STR_EXP reach actual account and XP provider separately from STR_HP");
        Check(order == std::vector<std::string>({"modify", "money", "xp"}), "actual award sequence is modifier/Money/XP");
        Require(e.messages.size() == messagesBefore + 1, "exactly one native Money message per threshold");
        const auto& message = e.messages.back();
        Check(message.type == gmsgMoney && message.recipient == Engine::Index(player.edict()) &&
            message.integers == std::vector<int>({before + money, 1}), "native Money includes current balance and visible delta flag");
    };

    auto low = tables.Select(1); bind(low); cleanAndScale(low, 1); hit(150, 62);
    const auto firstHealth = human.pev->health;
    Check(walls.ApplyRoundDifficulty(2, 1, error) && human.pev->health == firstHealth,
        "duplicate same round does not heal or multiply a damaged wall");
    const auto balance = player.m_iAccount;
    Require(human.TakeDamage(player.pev, player.pev, 25, DMG_BULLET) != FALSE, "partial reward accumulates");
    const auto rewardOwner = active;
    cleanAndScale(low, 2);
    Check(active == rewardOwner && player.m_iAccount == balance && players->state->accumulatedDamage == 25,
        "ordinary next stage retains reward owner, balance and partial damage without cumulative health scaling");
    // Complete the partial threshold through the actual damage path. Player
    // Reset/Clear during a full replay belongs to the separate Gate test.
    Require(human.TakeDamage(player.pev, player.pev, 100, DMG_BULLET) != FALSE, "ordinary stage keeps earned threshold remainder");
    Check(player.m_iAccount == balance + 150 && players->state->accumulatedDamage == 0,
        "ordinary stage pays the remaining threshold once");
    auto high = tables.Select(2); bind(high); cleanAndScale(high, 3); hit(400, 150);
    const auto highHealth = human.pev->health;
    Check(!walls.ApplyRoundDifficulty(2, 1, error) && !error.message.empty() && human.pev->health == highHealth,
        "expired low-round callback cannot multiply current high-difficulty health");
    Check(walls.ApplyRoundDifficulty(5, 3, error) && human.pev->health == highHealth,
        "current round remains idempotent after expired callback rejection");
    target.pev->max_health = 912;
    bind(low); cleanAndScale(low, 4); hit(150, 62);
    Check(target.pev->health == 912 && target.pev->max_health == 912 && target.m_flHealth == 123,
        "low-high-low never multiplies or replaces target max_health");
    Check(!zombie.TakeDamage(player.pev, player.pev, 100, DMG_BULLET) && zombie.pev->health == 700,
        "monster-only wall still rejects player damage after replay");
    Check(zombie.TakeDamage(monster.pev, monster.pev, 100, DMG_BULLET) && zombie.pev->health == 600,
        "monster-only wall keeps its native monster damage behavior");
    Check(mixed.TakeDamage(monster.pev, monster.pev, 100, DMG_BULLET) && mixed.pev->health == 700,
        "mixed wall still accepts monster damage without receiving STR_HP");
    walls.Reset();
    Require(walls.Collect(error) && walls.BindRules(active), "collection can start a new map/session after Reset");
    cleanAndScale(low, 1);
    Check(human.pev->health == 2000, "Reset permits the low round number in the replacement session");
}

void AtomicValidationCases()
{
    Engine e; const auto access = NewDifficultyMap();
    auto& first = e.Wall("1", "0", 20);
    auto& second = e.Wall("1", "0", 30);
    auto& removed = e.Wall("1", "0", 40);
    CCSOLScenarioWalls walls; walls.BindEntityAccess(access); ConfigError error;
    Require(walls.Collect(error), "validation collection starts");
    second.pev->health = std::numeric_limits<float>::max();
    Check(!walls.ApplyRoundDifficulty(2, 1, error) && first.pev->health == 20 &&
        second.pev->health == std::numeric_limits<float>::max(), "later overflow rejects whole health batch without partial writes");
    second.Restart(); removed.pev->flags |= FL_KILLME;
    Require(walls.ApplyRoundDifficulty(2, 1, error), "failed batch did not consume round token");
    Check(first.pev->health == 40 && second.pev->health == 60 && removed.pev->health == 40,
        "valid retry commits once and skips dying handles");
    const auto before = first.pev->health;
    access->Revoke();
    Check(!walls.ApplyRoundDifficulty(3, 2, error) && first.pev->health == before,
        "retired map cannot apply difficulty to previous entities");
}
}
}

int main()
{
    try { wall_test::RoundDifficultyCases(); wall_test::AtomicValidationCases(); }
    catch (const std::exception& error)
    { ++difficulty_test::failures; std::cerr << "EXCEPTION " << error.what() << '\n'; }
    std::cout << "checks=" << difficulty_test::checks << " failures=" << difficulty_test::failures << '\n';
    return difficulty_test::failures ? 1 : 0;
}
