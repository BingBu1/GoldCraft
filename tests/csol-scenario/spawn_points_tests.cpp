// SPDX-License-Identifier: GPL-3.0-or-later
// Link the production data layer unchanged. The --oracle protocol lets the
// independent original-instruction runner supply signed narrowing/quota cases.
#include "spawn_points.hpp"

#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>

using namespace csol::scenario;

namespace
{
int checks{};
int failures{};

void Check(bool value, const char* what)
{
    ++checks;
    if (!value)
    {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

SpawnPoint Point(int team, int id, std::size_t entity, float marker)
{
    return {{marker, marker + 1, marker + 2}, {marker + 3, marker + 4, marker + 5},
        team, id, entity};
}

bool SamePoint(const SpawnPoint& a, const SpawnPoint& b)
{
    return a.origin == b.origin && a.angles == b.angles && a.team == b.team &&
        a.id == b.id && a.entityIndex == b.entityIndex;
}

int OracleProtocol()
{
    char command{};
    while (std::cin >> command)
    {
        int team{}, id{};
        if (!(std::cin >> team >> id)) return 2;
        if (command == 'K')
        {
            std::cout << PointKey(team, id) << '\n';
        }
        else if (command == 'Q')
        {
            int maxSpawn{}, exists{};
            std::uint32_t consumed{};
            if (!(std::cin >> maxSpawn >> consumed >> exists)) return 2;
            SpawnPoints points;
            if (exists)
            {
                points.RegisterMonsterPoint(Point(team, id, 99, 10), maxSpawn);
                // The object is not const. This test-only seed exercises signed
                // counter boundaries without billions of attempted spawns.
                auto* point = const_cast<MonsterPoint*>(points.FindMonsterPoint(team, id));
                point->consumed = consumed;
            }
            const bool allowed = points.ConsumePointQuota(team, id);
            const auto* point = points.FindMonsterPoint(team, id);
            std::cout << int(allowed) << ' ' << (point ? point->consumed : consumed)
                << ' ' << points.Monsters().size() << '\n';
        }
        else return 2;
    }
    return 0;
}
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--oracle") return OracleProtocol();
    static_assert(PointKey(0, -1) == -1);
    static_assert(PointKey(127, -1) == -1);
    static_assert(PointKey(1, 32768) == -32768);
    static_assert(PointKey(-128, 1) == -8388607);
    static_assert(PointKey(256, 65536) == 0);

    SpawnPoints points;
    Check(points.Monsters().empty() && points.Players().empty(), "empty registry");
    Check(points.FindMonsterPoint(1, 9) == nullptr, "missing monster lookup");
    Check(points.FindPlayerPoints(2, 9) == nullptr, "missing player lookup");
    Check(points.ConsumePointQuota(1, 9), "missing point passes quota only");
    Check(points.Monsters().empty(), "missing quota check does not insert");

    const auto first = Point(1, 7, 11, 10);
    const auto replacement = Point(1, 7, 42, 20);
    points.RegisterMonsterPoint(first, 2);
    Check(points.ConsumePointQuota(1, 7), "first permitted attempt");
    Check(points.ConsumePointQuota(1, 7), "second permitted attempt");
    Check(!points.ConsumePointQuota(1, 7), "third attempt rejected at limit");
    Check(points.FindMonsterPoint(1, 7)->consumed == 2, "rejection leaves count");
    points.RegisterMonsterPoint(replacement, 1);
    const auto* replaced = points.FindMonsterPoint(1, 7);
    Check(points.Monsters().size() == 1, "duplicate monster keeps one node");
    Check(SamePoint(replaced->point, replacement), "duplicate replaces full payload and source index");
    Check(replaced->maxSpawn == 1 && replaced->consumed == 0, "duplicate replaces quota and resets count");
    Check(points.ConsumePointQuota(1, 7), "replacement has fresh quota");
    Check(!points.ConsumePointQuota(1, 7), "replacement applies new quota");
    const auto team2 = Point(2, 7, 15, 30);
    points.RegisterMonsterPoint(team2, 3);
    Check(points.Monsters().size() == 2, "positive IDs retain separate teams");
    Check(SamePoint(points.FindMonsterPoint(1, 7)->point, replacement), "other team does not overwrite");
    Check(SamePoint(points.FindMonsterPoint(2, 7)->point, team2), "other team lookup");

    const auto collision1 = Point(1, -1, 31, 40);
    const auto collision2 = Point(2, 65535, 32, 50);
    points.RegisterMonsterPoint(collision1, 8);
    Check(points.ConsumePointQuota(1, -1), "negative key initial quota");
    points.RegisterMonsterPoint(collision2, 2);
    Check(points.Monsters().size() == 3, "negative ID sign extension collides across teams");
    Check(SamePoint(points.FindMonsterPoint(127, -1)->point, collision2), "collision uses last traversal payload");
    Check(points.FindMonsterPoint(-128, -1)->consumed == 0, "collision clears prior quota");
    Check(PointKey(257, 65543) == PointKey(1, 7), "wide integers narrow before key composition");

    points.RegisterMonsterPoint(Point(1, 8, 8, 60), 0);
    for (int i = 0; i < 8; ++i) Check(points.ConsumePointQuota(1, 8), "zero max remains unlimited");
    Check(points.FindMonsterPoint(1, 8)->consumed == 0, "unlimited does not count attempts");
    points.RegisterMonsterPoint(Point(1, 9, 9, 70), -1);
    Check(!points.ConsumePointQuota(1, 9), "negative max rejects fresh nonnegative count");
    Check(points.FindMonsterPoint(1, 9)->consumed == 0, "negative max refusal leaves count");
    points.RegisterMonsterPoint(Point(1, 10, 10, 80), std::numeric_limits<int>::min());
    Check(!points.ConsumePointQuota(1, 10), "minimum max rejects fresh count");

    const auto player1 = Point(2, 0, 100, 90);
    const auto player2 = Point(2, 0, 101, 100);
    points.AppendPlayerPoint(player1);
    points.AppendPlayerPoint(player2);
    const auto* players = points.FindPlayerPoints(2, 0);
    Check(points.Players().size() == 1 && players && players->size() == 2, "players with same key append");
    Check(SamePoint((*players)[0], player1) && SamePoint((*players)[1], player2), "player traversal order and payload preserved");
    points.AppendPlayerPoint(Point(1, -1, 102, 110));
    points.AppendPlayerPoint(Point(2, 65535, 103, 120));
    const auto* collidedPlayers = points.FindPlayerPoints(0, -1);
    Check(collidedPlayers && collidedPlayers->size() == 2, "player signed-key collision still appends");
    Check((*collidedPlayers)[0].entityIndex == 102 && (*collidedPlayers)[1].entityIndex == 103, "colliding player insertion order");
    Check(points.FindMonsterPoint(0, -1)->point.entityIndex == 32, "player append never modifies monster table");

    points.Clear();
    Check(points.Monsters().empty() && points.Players().empty(), "clear both tables");
    Check(points.FindMonsterPoint(1, 7) == nullptr && points.FindPlayerPoints(2, 0) == nullptr, "clear invalidates old lookup");
    Check(points.ConsumePointQuota(1, 7), "clear restores absent-point quota behavior");
    points.RegisterMonsterPoint(first, 1);
    Check(points.FindMonsterPoint(1, 7)->consumed == 0 && points.ConsumePointQuota(1, 7), "reuse after clear starts fresh");
    std::cout << "checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
