// SPDX-License-Identifier: GPL-3.0-or-later
// Original zs_2gate wall records with actual SDK wall/reward/Restart methods.
// The full Session and engine world are exercised by a separate fixture.
#include "fixture.hpp"
#include "actual_2gate_walls.inc"

namespace wall_test
{
std::shared_ptr<CSOLScenarioEntityAccess> NewDifficultyMap();
namespace
{
void GateCases(const std::filesystem::path& resources)
{
    Engine engine;
    const auto access = NewDifficultyMap();
    auto& player = engine.New<Player>("player");
    player.pev->flags |= FL_CLIENT;
    player.m_iAccount = 7500;
    auto& monster = engine.New<Actor>("test_monster");
    difficulty_test::Files files;
    files.contents[difficulty_test::DefaultPath] = difficulty_test::Read(resources / difficulty_test::DefaultPath);
    csol::scenario::DifficultyResources difficulty;
    csol::scenario::ResourceError resourceError;
    Require(difficulty.BeginMap("zs_2gate", 1, files.Api(), resourceError), "real map difficulty loads");
    csol::scenario::RoundTable rounds;
    csol::scenario::ConfigError error;
    const auto bytes = difficulty_test::Read(resources / "maps/zbs_zs_2gate.cso");
    Require(csol::scenario::ReadRoundResource(difficulty_test::Bytes(bytes), rounds, error), "original five round table loads");
    csol::scenario::WallRewardSettings settings;
    Require(csol::scenario::ReadWallRewardSettings(rounds, difficulty, settings, error), "original zero wall rewards load");
    Check(rounds.rows.size() == 5 && settings.threshold == 0 && settings.money == 0 && settings.experience == 0,
        "original 2gate has five rounds and zero threshold/money/XP");
    auto players = std::make_shared<Players>();
    players->state->player = &player;
    players->state->attackLevel = 41;
    players->state->generation = 1;
    auto rewards = CCSOLScenarioWallRewards::Create(rounds, difficulty, players, error);
    Require(bool(rewards), "actual reward owner accepts zero metadata before damage");
    std::vector<CCSOLScenarioBreakable*> actual;
    for (const auto& row : GateWalls)
        actual.push_back(&engine.Wall(row.human, row.monster, row.health, 0, static_cast<Materials>(row.material)));
    CCSOLScenarioWalls walls;
    walls.BindEntityAccess(access);
    Require(walls.Collect(error) && walls.Count() == 27 && walls.BindRules(rewards), "all 27 actual wall classes bind one real reward owner");
    std::size_t damageWalls{}, touchWalls{};
    for (std::size_t index = 0; index < actual.size(); ++index)
    {
        auto& wall = *actual[index];
        const auto& row = GateWalls[index];
        const float health = wall.pev->health;
        const auto messages = engine.messages.size();
        Check(!wall.TakeDamage(player.pev, player.pev, 10000, DMG_BULLET) && wall.pev->health == health,
            "player hit cannot damage a zero-human-ratio map wall");
        Check(rewards->Error().message.empty() && !players->state->accumulatedDamage &&
            player.m_iAccount == 7500 && players->xp.empty() && engine.messages.size() == messages,
            "zero-scaled damage returns before reward credit, Money, XP or effects");
        const float ratio = std::strtof(row.monster, nullptr);
        if (ratio > 0)
        {
            ++damageWalls;
            Check(wall.TakeDamage(monster.pev, monster.pev, 0.125f, DMG_BULLET) &&
                wall.pev->health == health - 0.125f * ratio, "monster hit uses original ratio at manager level one");
            wall.TakeDamage(monster.pev, monster.pev, health + 100, DMG_BULLET);
            Check(wall.pev->health <= 0 && wall.pev->deadflag == DEAD_DEAD &&
                wall.pev->takedamage == DAMAGE_NO && (wall.pev->effects & EF_NODRAW),
                "monster lethal damage breaks the real retained wall entity");
        }
        else
        {
            ++touchWalls;
            Check(!wall.TakeDamage(monster.pev, monster.pev, 10000, DMG_BULLET) && wall.pev->health == health,
                "zero-ratio collision trigger ignores direct damage");
            wall.Touch(&player);
            Check(!(wall.pev->effects & EF_NODRAW), "player collision cannot consume monster trigger wall");
            wall.Touch(&monster);
            Check(wall.pev->deadflag == DEAD_DEAD && (wall.pev->effects & EF_NODRAW),
                "class17 collision executes original unconditional break path");
        }
        wall.Restart();
        Check(wall.pev->health == row.health && wall.pev->deadflag == DEAD_NO &&
            !(wall.pev->effects & EF_NODRAW), "native Restart restores original wall without reallocating it");
    }
    Check(damageWalls == 18 && touchWalls == 9, "actual map distinguishes eighteen damage walls and nine collision triggers");
    for (std::uint64_t round = 1; round <= 5; ++round)
    {
        Require(walls.ApplyRoundDifficulty(5, round, error), "round scaling validates full wall collection");
        for (std::size_t index = 0; index < actual.size(); ++index)
            Check(actual[index]->pev->health == GateWalls[index].health,
                "2gate monster-only and touch walls are excluded from player-wall difficulty scaling");
    }
    Check(rewards->Error().message.empty() && player.m_iAccount == 7500 && players->xp.empty(),
        "complete wall run never consumes zero-threshold player reward loop");
}

void NonpositiveCases()
{
    for (const auto threshold : {0, -1})
    {
        Engine engine; const auto access = NewDifficultyMap();
        auto& player = engine.New<Player>("player");
        player.m_iAccount = 7500;
        RewardFixture fixture(player);
        fixture.rounds.source.records[2].fields[1] = std::to_string(threshold);
        csol::scenario::ConfigError error;
        auto rewards = CCSOLScenarioWallRewards::Create(fixture.rounds, fixture.difficulty, fixture.players, error);
        Require(bool(rewards), "nonpositive original metadata creates a reward owner");
        auto& wall = engine.Wall("1", "0"); wall.BindRules(rewards);
        Check(!wall.TakeDamage(player.pev, player.pev, 1, DMG_BULLET), "reachable nonpositive loop reports failure instead of hanging");
        Check(rewards->Error().message == "scenario wall reward threshold is nonpositive" &&
            player.m_iAccount == 7500 && fixture.players->xp.empty() && wall.pev->health == 1000 &&
            !fixture.players->state->crediting, "host safety failure preserves money, XP and base wall damage and releases credit latch");
    }
}
}
}

int main(int argc, char** argv)
{
    try
    {
        if (argc != 3) throw std::runtime_error("usage: wall-2gate numeric|metadata|2gate|nonpositive frozen-resources");
        const std::string_view group = argv[1];
        if (group == "numeric") wall_test::NumericCases();
        else if (group == "metadata") wall_test::MetadataCases();
        else if (group == "2gate") wall_test::GateCases(argv[2]);
        else if (group == "nonpositive") wall_test::NonpositiveCases();
        else throw std::runtime_error("unknown wall group");
    }
    catch (const std::exception& error)
    { ++difficulty_test::failures; std::cerr << "EXCEPTION " << error.what() << '\n'; }
    std::cout << "checks=" << difficulty_test::checks << " failures=" << difficulty_test::failures << '\n';
    return difficulty_test::failures ? 1 : 0;
}
